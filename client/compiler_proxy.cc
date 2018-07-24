// Copyright 2010 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// Compiler proxy reimplemented as asynchronous.

// #define HAVE_HEAP_PROFILER 1
// #define HAVE_CPU_PROFILER 1

#include <stdio.h>
#include <time.h>

#ifndef _WIN32
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#ifdef __MACH__
#include <sys/sysctl.h>
#endif

#include <algorithm>
#include <iostream>
#include <iterator>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>

#include "absl/memory/memory.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "auto_updater.h"
#include "autolock_timer.h"
#include "basictypes.h"
#include "breakpad.h"
#include "callback.h"
#include "compile_service.h"
#include "compile_stats.h"
#include "compiler_flags.h"
#include "compiler_flags_util.h"
#include "compiler_info.h"
#include "compiler_info_cache.h"
#include "compiler_proxy_contentionz_script.h"
#include "compiler_proxy_histogram.h"
#include "compiler_proxy_info.h"
#include "compiler_proxy_status_html5.h"
#include "compiler_proxy_status_script.h"
#include "compiler_proxy_status_style.h"
#include "compiler_specific.h"
#include "compilerz_html.h"
#include "compilerz_script.h"
#include "compilerz_style.h"
#include "counterz.h"
#include "cxx/include_processor/cpp_directive_optimizer.h"
#include "cxx/include_processor/cpp_macro.h"
#include "cxx/include_processor/include_cache.h"
#include "cxx/include_processor/include_file_finder.h"
#include "deps_cache.h"
#include "env_flags.h"
#include "file_hash_cache.h"
#include "file_helper.h"
#include "file_stat_cache.h"
#include "glog/logging.h"
#include "goma_file.h"
#include "goma_file_http.h"
#include "goma_hash.h"
#include "goma_init.h"
#include "hash_rewrite_parser.h"
#include "http.h"
#include "http_init.h"
#include "http_rpc.h"
#include "http_rpc_init.h"
#include "http_util.h"
#include "ioutil.h"
#include "java/jarfile_reader.h"
#include "jquery.min.h"
#include "linker/linker_input_processor/arfile_reader.h"
#include "list_dir_cache.h"
#include "local_output_cache.h"
#include "log_cleaner.h"
#include "log_service_client.h"
#include "multi_http_rpc.h"
#include "mypath.h"
#include "oauth2.h"
#include "oauth2_token.h"
#include "path.h"
#include "platform_thread.h"
#include "rand_util.h"
#include "scoped_fd.h"
#include "settings.h"
#include "socket_factory.h"
#include "subprocess.h"
#include "subprocess_controller.h"
#include "subprocess_controller_client.h"
#include "subprocess_option_setter.h"
#include "subprocess_task.h"
#include "threadpool_http_server.h"
#include "trustedipsmanager.h"
#include "util.h"
#include "watchdog.h"
#include "worker_thread_manager.h"

MSVC_PUSH_DISABLE_WARNING_FOR_PROTO()
#include "prototmp/goma_data.pb.h"
MSVC_POP_WARNING()


#if HAVE_HEAP_PROFILER
#include <gperftools/heap-profiler.h>
#endif
#if HAVE_CPU_PROFILER
#include <gperftools/profiler.h>
#endif

using devtools_goma::CompileService;
using devtools_goma::ExecReq;
using devtools_goma::ExecResp;
using devtools_goma::ScopedFd;
using devtools_goma::ThreadpoolHttpServer;

using std::string;

#ifndef _WIN32
using devtools_goma::Daemonize;
#endif

namespace {

#ifdef _WIN32

string FindLogFile(string log_dir, string base_name, string log_type) {
  // Log file is in log_dir and its name is like
  // <base_name>.<host_name>.<user_name>.log.<log_type>.<timestamp>.<pid>
  const string pid = std::to_string(GetCurrentProcessId());

  string pattern = log_dir;
  pattern.append("\\");
  pattern.append(base_name);
  pattern.append("*");

  string found_file;
  WIN32_FIND_DATAA find_data = {};
  HANDLE find_handle = FindFirstFileA(pattern.c_str(), &find_data);
  if (find_handle != INVALID_HANDLE_VALUE) {
    do {
      if (absl::EndsWith(find_data.cFileName, pid) &&
          strstr(find_data.cFileName, log_type.c_str())) {
        found_file = file::JoinPath(log_dir, find_data.cFileName);
        break;
      }
    } while (FindNextFileA(find_handle, &find_data) != 0);
    FindClose(find_handle);
  }
  return found_file;
}

#endif

}  // anonymous namespace

namespace devtools_goma {

// This class is reused for every request.
class CompilerProxyHttpHandler : public ThreadpoolHttpServer::HttpHandler,
                                 public ThreadpoolHttpServer::Monitor {
 public:
  CompilerProxyHttpHandler(string myname,
                           string setting,
                           string tmpdir,
                           WorkerThreadManager* wm)
      : myname_(std::move(myname)),
        setting_(std::move(setting)),
        service_(wm, FLAGS_COMPILER_INFO_POOL),
        log_cleaner_closure_id_(kInvalidPeriodicClosureId),
        memory_tracker_closure_id_(kInvalidPeriodicClosureId),
        rpc_sent_count_(0),
        tmpdir_(std::move(tmpdir)),
        last_memory_byte_(0)
#if HAVE_HEAP_PROFILER
        ,
        compiler_proxy_heap_profile_file_(file::JoinPathRespectAbsolute(
            tmpdir_,
            FLAGS_COMPILER_PROXY_HEAP_PROFILE_FILE))
#endif
#if HAVE_CPU_PROFILER
        ,
        compiler_proxy_cpu_profile_file_(file::JoinPathRespectAbsolute(
            tmpdir_,
            FLAGS_COMPILER_PROXY_CPU_PROFILE_FILE)),
        cpu_profiling_(false)
#endif
  {
    if (FLAGS_SEND_USER_INFO) {
      service_.AllowToSendUserInfo();
    }
    service_.SetActiveTaskThrottle(FLAGS_MAX_ACTIVE_TASKS);
    service_.SetCompileTaskHistorySize(
        FLAGS_MAX_FINISHED_TASKS,
        FLAGS_MAX_FAILED_TASKS,
        FLAGS_MAX_LONG_TASKS);
    int network_error_margin = 0;
    if (FLAGS_FAIL_FAST) {
      LOG(INFO) << "fail fast mode";
      if (FLAGS_ALLOWED_NETWORK_ERROR_DURATION < 0) {
        FLAGS_ALLOWED_NETWORK_ERROR_DURATION = 60;
        network_error_margin = 30;
        LOG(INFO) << "override GOMA_ALLOWED_NETWORK_ERROR_DURATION to "
                  << FLAGS_ALLOWED_NETWORK_ERROR_DURATION << " secs";
      } else {
        network_error_margin = FLAGS_ALLOWED_NETWORK_ERROR_DURATION / 2;
        LOG(INFO) << "use GOMA_ALLOWED_NETWORK_ERROR_DURATION="
                  << FLAGS_ALLOWED_NETWORK_ERROR_DURATION << " secs";
      }
      if (FLAGS_MAX_ACTIVE_FAIL_FALLBACK_TASKS < 0) {
        // TODO: consider using this for fail fallback caused by
        // remote goma backend's execution failure not network error.
        FLAGS_MAX_ACTIVE_FAIL_FALLBACK_TASKS = FLAGS_BURST_MAX_SUBPROCS;
        LOG(INFO) << "override GOMA_MAX_ACTIVE_FAIL_FALLBACK_TASKS to "
                  << FLAGS_MAX_ACTIVE_FAIL_FALLBACK_TASKS;
        if (FLAGS_ALLOWED_MAX_ACTIVE_FAIL_FALLBACK_DURATION < 0) {
          // Prefer to show network failure to reaching max active fail
          // fallback.  If fail fallback is caused by network error, it is also
          // counted as active fail fallbacks but people can easily understand
          // the reason by seeing network failure.
          FLAGS_ALLOWED_MAX_ACTIVE_FAIL_FALLBACK_DURATION =
              FLAGS_ALLOWED_NETWORK_ERROR_DURATION + 10;
          LOG(INFO) << "override "
                    << "FLAGS_ALLOWED_MAX_ACTIVE_FAIL_FALLBACK_DURATION_IN_SEC "
                    << "to " << FLAGS_ALLOWED_MAX_ACTIVE_FAIL_FALLBACK_DURATION
                    << " secs";
        }
      }
    }
    http_options_.proxy_host_name = FLAGS_PROXY_HOST;
    http_options_.proxy_port = FLAGS_PROXY_PORT;
    HttpClient::Options http_options = http_options_;
    InitHttpClientOptions(&http_options);
    http_options.network_error_margin = network_error_margin;
    if (FLAGS_NETWORK_ERROR_THRESHOLD_PERCENT >= 0 &&
        FLAGS_NETWORK_ERROR_THRESHOLD_PERCENT < 100) {
      http_options.network_error_threshold_percent =
          FLAGS_NETWORK_ERROR_THRESHOLD_PERCENT;
    }
    LOG_IF(ERROR, FLAGS_NETWORK_ERROR_THRESHOLD_PERCENT >= 100)
        << "GOMA_NETWORK_ERROR_THRESHOLD_PERCENT must be less than 100: "
        << FLAGS_NETWORK_ERROR_THRESHOLD_PERCENT;
    if (FLAGS_BACKEND_SOFT_STICKINESS) {
      string cookie;
      if (FLAGS_BACKEND_SOFT_STICKINESS_REFRESH) {
        cookie = GetRandomAlphanumeric(64);
      } else {
        ComputeDataHashKey(service_.username() + "@" + service_.nodename(),
                           &cookie);
      }
      http_options.cookie = "GomaClient=" + cookie;
    }
    std::unique_ptr<HttpClient> client(new HttpClient(
        HttpClient::NewSocketFactoryFromOptions(http_options),
        HttpClient::NewTLSEngineFactoryFromOptions(http_options),
        http_options, wm));
    CHECK_GE(FLAGS_MAX_SUBPROCS, FLAGS_MAX_SUBPROCS_LOW);
    CHECK_GE(FLAGS_MAX_SUBPROCS, FLAGS_MAX_SUBPROCS_HEAVY);
    CHECK_GE(FLAGS_BURST_MAX_SUBPROCS, FLAGS_BURST_MAX_SUBPROCS_LOW);
    CHECK_GE(FLAGS_BURST_MAX_SUBPROCS, FLAGS_BURST_MAX_SUBPROCS_HEAVY);
    std::unique_ptr<SubProcessOptionSetter> option_setter(
        new SubProcessOptionSetter(
            FLAGS_MAX_SUBPROCS,
            FLAGS_MAX_SUBPROCS_LOW,
            FLAGS_MAX_SUBPROCS_HEAVY,
            FLAGS_BURST_MAX_SUBPROCS,
            FLAGS_BURST_MAX_SUBPROCS_LOW,
            FLAGS_BURST_MAX_SUBPROCS_HEAVY));
    client->SetMonitor(
        absl::make_unique<NetworkErrorMonitor>(option_setter.get()));
    service_.SetSubProcessOptionSetter(std::move(option_setter));
    service_.SetMaxCompilerDisabledTasks(FLAGS_MAX_COMPILER_DISABLED_TASKS);
    service_.SetHttpClient(std::move(client));

    HttpRPC::Options http_rpc_options;
    InitHttpRPCOptions(&http_rpc_options);
    service_.SetHttpRPC(
        absl::make_unique<HttpRPC>(service_.http_client(), http_rpc_options));

    service_.SetExecServiceClient(
        absl::make_unique<ExecServiceClient>(service_.http_rpc(), "/e"));

    MultiHttpRPC::Options multi_store_options;
    multi_store_options.max_req_in_call = FLAGS_MULTI_STORE_IN_CALL;
    multi_store_options.req_size_threshold_in_call =
        FLAGS_MULTI_STORE_THRESHOLD_SIZE_IN_CALL;
    multi_store_options.check_interval_ms = FLAGS_MULTI_STORE_PENDING_MS;
    service_.SetMultiFileStore(absl::make_unique<MultiFileStore>(

        service_.http_rpc(), "/s", multi_store_options, wm));
    service_.SetFileServiceHttpClient(absl::make_unique<FileServiceHttpClient>(

        service_.http_rpc(), "/s", "/l", service_.multi_file_store()));
    if (FLAGS_PROVIDE_INFO)
      service_.SetLogServiceClient(absl::make_unique<LogServiceClient>(

          service_.http_rpc(), "/sl", FLAGS_NUM_LOG_IN_SAVE_LOG,
          FLAGS_LOG_PENDING_MS, wm));
    ArFileReader::Register();
    JarFileReader::Register();
    service_.StartIncludeProcessorWorkers(FLAGS_INCLUDE_PROCESSOR_THREADS);
    service_.SetNeedToSendContent(FLAGS_COMPILER_PROXY_STORE_FILE);
    service_.SetNewFileThreshold(FLAGS_COMPILER_PROXY_NEW_FILE_THRESHOLD);
    service_.SetEnableGchHack(FLAGS_ENABLE_GCH_HACK);
    service_.SetUseRelativePathsInArgv(FLAGS_USE_RELATIVE_PATHS_IN_ARGV);
    service_.SetCommandCheckLevel(FLAGS_COMMAND_CHECK_LEVEL);
    if (FLAGS_HERMETIC == "off") {
      service_.SetHermetic(false);
    } else if (FLAGS_HERMETIC == "fallback") {
      service_.SetHermetic(true);
      service_.SetHermeticFallback(true);
    } else if (FLAGS_HERMETIC == "error") {
      service_.SetHermetic(true);
      service_.SetHermeticFallback(false);
    } else {
      LOG(FATAL) << "Unknown hermetic mode: " << FLAGS_HERMETIC
                 << " should be one of \"off\", \"fallback\" or \"error\"";
    }
    service_.SetDontKillSubprocess(FLAGS_DONT_KILL_SUBPROCESS);
    service_.SetMaxSubProcsPending(FLAGS_MAX_SUBPROCS_PENDING);
    service_.SetLocalRunPreference(FLAGS_LOCAL_RUN_PREFERENCE);
    service_.SetLocalRunForFailedInput(FLAGS_LOCAL_RUN_FOR_FAILED_INPUT);
    service_.SetLocalRunDelayMsec(FLAGS_LOCAL_RUN_DELAY_MSEC);
    service_.SetMaxSumOutputSize(
        FLAGS_MAX_SUM_OUTPUT_SIZE_IN_MB * 1024 * 1024);
    service_.SetStoreLocalRunOutput(FLAGS_STORE_LOCAL_RUN_OUTPUT);
    service_.SetEnableRemoteLink(FLAGS_ENABLE_REMOTE_LINK);
    service_.SetShouldFailForUnsupportedCompilerFlag(
        FLAGS_FAIL_FOR_UNSUPPORTED_COMPILER_FLAGS);
    service_.SetTmpDir(tmpdir_);
    service_.SetAllowedNetworkErrorDuration(
        FLAGS_ALLOWED_NETWORK_ERROR_DURATION);
    service_.SetMaxActiveFailFallbackTasks(
        FLAGS_MAX_ACTIVE_FAIL_FALLBACK_TASKS);
    service_.SetAllowedMaxActiveFailFallbackDuration(
        FLAGS_ALLOWED_MAX_ACTIVE_FAIL_FALLBACK_DURATION);

    std::vector<string> timeout_secs_str = ToVector(
        absl::StrSplit(FLAGS_COMPILER_PROXY_RPC_TIMEOUT_SECS,
                       ',',
                       absl::SkipEmpty()));
    std::vector<int> timeout_secs;
    for (const auto& it : timeout_secs_str)
      timeout_secs.push_back(atoi(it.c_str()));
    service_.SetTimeoutSecs(timeout_secs);

    if (FLAGS_LOG_CLEAN_INTERVAL > 0) {
      log_cleaner_.AddLogBasename(myname_);
      log_cleaner_.AddLogBasename(myname_ + "-subproc");
      log_cleaner_.AddLogBasename("gomacc");
      log_cleaner_.AddLogBasename("cc");
      log_cleaner_.AddLogBasename("c++");
      log_cleaner_.AddLogBasename("gcc");
      log_cleaner_.AddLogBasename("g++");
      log_cleaner_.AddLogBasename("clang");
      log_cleaner_.AddLogBasename("clang++");
      log_cleaner_.AddLogBasename("goma_fetch");

      std::unique_ptr<PermanentClosure> closure = NewPermanentCallback(
          this, &CompilerProxyHttpHandler::RunCleanOldLogs);
      closure->Run();
      log_cleaner_closure_id_ = wm->RegisterPeriodicClosure(
          FROM_HERE, FLAGS_LOG_CLEAN_INTERVAL * 1000, std::move(closure));
    } else {
      LOG(INFO) << "log cleaner disabled";
    }

    if (FLAGS_MEMORY_TRACK_INTERVAL > 0) {
      memory_tracker_closure_id_ = wm->RegisterPeriodicClosure(
          FROM_HERE, FLAGS_MEMORY_TRACK_INTERVAL * 1000,
          NewPermanentCallback(this,
                               &CompilerProxyHttpHandler::RunTrackMemory));
    } else {
      LOG(INFO) << "memory tracker disabled";
    }

    InitialPing();

    http_handlers_.insert(
        std::make_pair("/",
                       &CompilerProxyHttpHandler::HandleStatusRequest));
    internal_http_handlers_.insert(
        std::make_pair("/static/jquery.min.js",
                       &CompilerProxyHttpHandler::HandleJQuery));
    internal_http_handlers_.insert(
        std::make_pair("/static/compiler_proxy_status_script.js",
                       &CompilerProxyHttpHandler::HandleStatusJavaScript));
    internal_http_handlers_.insert(
        std::make_pair("/static/compiler_proxy_contentionz_script.js",
                       &CompilerProxyHttpHandler::HandleContentionzJavaScript));
    internal_http_handlers_.insert(
        std::make_pair("/static/compiler_proxy_status_style.css",
                       &CompilerProxyHttpHandler::HandleStatusCSS));
    internal_http_handlers_.insert(
        std::make_pair("/static/compilerz.js",
                       &CompilerProxyHttpHandler::HandleCompilerzScript));
    internal_http_handlers_.insert(
        std::make_pair("/static/compilerz.css",
                       &CompilerProxyHttpHandler::HandleCompilerzStyle));
    internal_http_handlers_.insert(
        std::make_pair("/api/taskz",
                       &CompilerProxyHttpHandler::HandleTaskRequest));
    internal_http_handlers_.insert(
        std::make_pair("/api/accountz",
                       &CompilerProxyHttpHandler::HandleAccountRequest));
    internal_http_handlers_.insert(
        std::make_pair("/api/compilerz",
                       &CompilerProxyHttpHandler::HandleCompilerJSONRequest));
    http_handlers_.insert(
        std::make_pair("/statz",
                       &CompilerProxyHttpHandler::HandleStatsRequest));
    http_handlers_.insert(
        std::make_pair("/compilerz",
                       &CompilerProxyHttpHandler::HandleCompilerzRequest));
    http_handlers_.insert(
        std::make_pair("/histogramz",
                       &CompilerProxyHttpHandler::HandleHistogramRequest));
    http_handlers_.insert(
        std::make_pair("/httprpcz",
                       &CompilerProxyHttpHandler::HandleHttpRpcRequest));
    http_handlers_.insert(
        std::make_pair("/threadz",
                       &CompilerProxyHttpHandler::HandleThreadRequest));
    http_handlers_.insert(
        std::make_pair("/contentionz",
                       &CompilerProxyHttpHandler::HandleContentionRequest));
    http_handlers_.insert(
        std::make_pair("/filecachez",
                       &CompilerProxyHttpHandler::HandleFileCacheRequest));
    http_handlers_.insert(
        std::make_pair("/compilerinfoz",
                       &CompilerProxyHttpHandler::HandleCompilerInfoRequest));
    http_handlers_.insert(
        std::make_pair("/includecachez",
                       &CompilerProxyHttpHandler::HandleIncludeCacheRequest));
    http_handlers_.insert(
        std::make_pair("/flagz",
                       &CompilerProxyHttpHandler::HandleFlagRequest));
    http_handlers_.insert(
        std::make_pair("/versionz",
                       &CompilerProxyHttpHandler::HandleVersionRequest));
    http_handlers_.insert(
        std::make_pair("/healthz",
                       &CompilerProxyHttpHandler::HandleHealthRequest));
    internal_http_handlers_.insert(
        std::make_pair("/portz",
                       &CompilerProxyHttpHandler::HandlePortRequest));
    http_handlers_.insert(
        std::make_pair("/logz",
                       &CompilerProxyHttpHandler::HandleLogRequest));
    http_handlers_.insert(
        std::make_pair("/errorz",
                       &CompilerProxyHttpHandler::HandleErrorStatusRequest));
#if HAVE_COUNTERZ
    http_handlers_.insert(
        std::make_pair("/counterz",
                       &CompilerProxyHttpHandler::HandleCounterRequest));
#endif
#if HAVE_HEAP_PROFILER
    http_handlers_.insert(
        std::make_pair("/heapz",
                       &CompilerProxyHttpHandler::HandleHeapRequest));
#endif
#if HAVE_CPU_PROFILER
    http_handlers_.insert(
        std::make_pair("/profilez",
                       &CompilerProxyHttpHandler::HandleProfileRequest));
#endif
  }

  ~CompilerProxyHttpHandler() override {
  }

  // TODO: better handling of HTTP errors.
  //                    might be ok to retry soon on timeout but might not be
  //                    good to retry soon for 4xx or 5xx status code.
  bool InitialPing() {
    int http_status_code = -1;
    time_t ping_end_time = time(nullptr) + FLAGS_PING_TIMEOUT_SEC;
    int num_retry = 0;
    int backoff_ms = service_.http_client()->options().min_retry_backoff_ms;
    while (time(nullptr) < ping_end_time) {
      HttpRPC::Status status;
      status.timeout_secs.push_back(FLAGS_PING_RETRY_INTERVAL);
      status.trace_id = "ping";
      http_status_code = service_.http_rpc()->Ping(service_.wm(),
                                                   "/ping", &status);
      if ((http_status_code != -1 && http_status_code != 0 &&
           http_status_code != 401 && http_status_code != 408 &&
           http_status_code / 100 != 5) ||
          // Since SocketPool retries connections and it should be natural
          // to assume that IP address that did not respond well would not
          // respond well for a while, we can think connection failure
          // as non-retryable error.
          !status.connect_success) {
        LOG(INFO) << "will not retry."
                  << " http_status_code=" << http_status_code
                  << " connect_success=" << status.connect_success
                  << " finished=" << status.finished
                  << " err=" << status.err;
        break;
      }
      // Retry for HTTP status 401 only if OAuth2 is valid.
      // When OAuth2 is enabled, but not valid (i.e. no refresh token),
      // it would fail with 401 and no need to retry.
      // b/68980193
      if (http_status_code == 401 &&
          !service_.http_client()->options().oauth2_config.valid()) {
        LOG(INFO) << "will not retry for auth failure without valid OAuth2."
                  << " http_status_code=" << http_status_code
                  << " connect_success=" << status.connect_success
                  << " finished=" << status.finished
                  << " err=" << status.err;
        break;
      }
      if (http_status_code == 401 || http_status_code / 100 == 5) {
        // retry after backoff_ms.
        backoff_ms = HttpClient::BackoffMsec(
            service_.http_client()->options(),
            backoff_ms, true);
        LOG(INFO) << "backoff " << backoff_ms << " msec"
                  << " because of http_status_code=" << http_status_code;
        absl::SleepFor(absl::Milliseconds(backoff_ms));
      }
      LOG(ERROR) << "Going to retry ping."
                 << " http_status_code=" << http_status_code
                 << " num_retry=" << num_retry;
      num_retry++;
    }
    if (http_status_code != 200) {
      LOG(ERROR) << "HTTP error=" << http_status_code
                 << ": Cannot connect to server at "
                 << service_.http_client()->options().RequestURL("/ping")
                 << " num_retry=" << num_retry;
      if (http_status_code == 401) {
        // TODO: fix this message for external users.
        LOG(ERROR)
            << "Please use OAuth2 to access from non-corp network.";
      }
      return false;
    }
    return true;
  }

  void HandleHttpRequest(
      ThreadpoolHttpServer::HttpServerRequest* http_server_request) override {
    const string& path = http_server_request->req_path();
    if (service_.compiler_proxy_id_prefix().empty()) {
      std::ostringstream ss;
      ss << service_.username() << "@" << service_.nodename() << ":"
         << http_server_request->server().port()
         << "/" << service_.start_time() << "/";
      if (FLAGS_SEND_USER_INFO) {
        service_.SetCompilerProxyIdPrefix(ss.str());
      } else {
        string hash;
        ComputeDataHashKey(ss.str(), &hash);
        std::ostringstream sss;
        sss << "anonymous@" << hash << ":8088/" << service_.start_time() << "/";
        service_.SetCompilerProxyIdPrefix(sss.str());
      }
    }
#ifdef _WIN32
    if (path == "/me") {
      if (!http_server_request->CheckCredential()) {
        SendErrorMessage(http_server_request, 401, "Unauthorized");
        return;
      }
      CompileService::MultiRpcController* rpc
          = new CompileService::MultiRpcController(
              service_.wm(), http_server_request);
      MultiExecReq multi_exec;
      if (!rpc->ParseRequest(&multi_exec)) {
        delete rpc;
        SendErrorMessage(http_server_request, 404, "Bad request");
        return;
      }
      for (int i = 0; i < multi_exec.req_size(); ++i) {
        if (ShouldTrace()) {
          VLOG(1) << "Setting Trace on this request";
          multi_exec.mutable_req(i)->set_trace(true);
        } else {
          multi_exec.mutable_req(i)->set_trace(false);
        }
        service_.Exec(rpc->rpc(i), &multi_exec.req(i), rpc->mutable_resp(i),
                      NewCallback(
                          this,
                          &CompilerProxyHttpHandler::ExecDoneInMulti, rpc, i));
      }
      return;
    }
#endif
    if (path == "/e") {
      if (!http_server_request->CheckCredential()) {
        SendErrorMessage(http_server_request, 401, "Unauthorized");
        return;
      }
      CompileService::RpcController* rpc
          = new CompileService::RpcController(http_server_request);
      ExecReq req;
      if (!rpc->ParseRequest(&req)) {
        delete rpc;
        SendErrorMessage(http_server_request, 404, "Bad request");
        return;
      }
      if (ShouldTrace()) {
        VLOG(1) << "Setting Trace on this request";
        req.set_trace(true);
      } else {
        req.set_trace(false);
      }

      ExecResp* resp = new ExecResp;
      // rpc and resp will be deleted in ExecDone.
      service_.Exec(rpc, &req, resp,
                    devtools_goma::NewCallback(
                        this, &CompilerProxyHttpHandler::ExecDone, rpc, resp));
      return;
    }

    // Most paths will be accessed by browser, so checked by IsTrusted().
    if (http_server_request->IsTrusted()) {
      HttpHandlerMethod handler = nullptr;
      std::map<string, HttpHandlerMethod>::const_iterator found =
          internal_http_handlers_.find(path);
      if (found != internal_http_handlers_.end()) {
        handler = found->second;
      } else if ((found = http_handlers_.find(path)) != http_handlers_.end()) {
        handler = found->second;
        // Users are checking the console... This would be a good
        // timing for flushing logs.
        devtools_goma::FlushLogFiles();
      }
      if (handler != nullptr) {
        string response;
        int responsecode = (this->*handler)(*http_server_request,
                                            &response);
        if (response.empty()) {
          if (responsecode == 404) {
            response = "HTTP/1.1 404 Not Found\r\n\r\n";
          } else {
            LOG(FATAL) << "Response is empty and unknown response code: "
                       << responsecode;
          }
        }
        http_server_request->SendReply(response);
        http_server_request = nullptr;
      } else if (path == "/quitquitquit") {
        DumpStatsToInfoLog();
        service_.wm()->DebugLog();
        DumpHistogramToInfoLog();
        DumpIncludeCacheLogToInfoLog();
        DumpContentionLogToInfoLog();
        DumpStatsProto();
        DumpCounterz();
        DumpDirectiveOptimizer();
        LOG(INFO) << "Dump done.";
        devtools_goma::FlushLogFiles();
        http_server_request->SendReply("HTTP/1.1 200 OK\r\n\r\nquit!");
        http_server_request = nullptr;
        service_.Quit();
      } else if (path == "/abortabortabort") {
        http_server_request->SendReply("HTTP/1.1 200 OK\r\n\r\nquit!");
        http_server_request = nullptr;
        service_.ClearTasks();
        exit(1);
      } else {
        http_server_request->SendReply("HTTP/1.1 404 Not found\r\n\r\n");
        http_server_request = nullptr;
      }
    } else {
      http_server_request->SendReply("HTTP/1.1 404 Not found\r\n\r\n");
      http_server_request = nullptr;
    }
  }

  bool shutting_down() override {
    return service_.quit();
  }

  void FinishHandle(const ThreadpoolHttpServer::Stat& stat) override {
    service_.histogram()->UpdateThreadpoolHttpServerStat(stat);
  }

  void Wait() {
    if (memory_tracker_closure_id_ != kInvalidPeriodicClosureId) {
      service_.wm()->UnregisterPeriodicClosure(memory_tracker_closure_id_);
      memory_tracker_closure_id_ = kInvalidPeriodicClosureId;
    }
    if (log_cleaner_closure_id_ != kInvalidPeriodicClosureId) {
      service_.wm()->UnregisterPeriodicClosure(log_cleaner_closure_id_);
      log_cleaner_closure_id_ = kInvalidPeriodicClosureId;
    }
    service_.Wait();
  }

  // Takes ownership of auto_upadter.
  void SetAutoUpdater(std::unique_ptr<AutoUpdater> auto_updater) {
    service_.SetAutoUpdater(std::move(auto_updater));
  }

  // Takes ownership of watchdog.
  void SetWatchdog(std::unique_ptr<Watchdog> watchdog,
                   const std::vector<string>& goma_ipc_env,
                   ThreadpoolHttpServer* server,
                   int count) {
    service_.SetWatchdog(std::move(watchdog), goma_ipc_env);
    service_.WatchdogStart(server, count);
  }

  void TrackMemoryOneshot() {
    TrackMemory();
  }

 private:
  typedef ThreadpoolHttpServer::HttpServerRequest HttpServerRequest;

  typedef int (CompilerProxyHttpHandler::*HttpHandlerMethod)(
      const HttpServerRequest& request, string* response);

  void OutputOkHeader(const char* content_type, std::ostringstream* ss) {
    *ss << "HTTP/1.1 200 OK\r\n"
        << "Content-Type: " << content_type << "\r\n\r\n";
  }

  int Redirect(const string& url, string* response) {
    std::ostringstream ss;
    ss << "HTTP/1.1 302 Found\r\n"
       << "Location: " << url << "\r\n"
       << "\r\n";
    *response = ss.str();
    return 302;
  }

  int BadRequest(string* response) {
    *response = "HTTP/1.1 400 Bad Request\r\n\r\n";
    return 400;
  }

  void OutputOkHeaderAndBody(const char* content_type,
                             absl::string_view content,
                             std::ostringstream* ss) {
    *ss << "HTTP/1.1 200 OK\r\n"
        << "Content-Type: " << content_type << "\r\n"
        << "Content-Length: " << content.size() << "\r\n\r\n"
        << content;
  }

  int HandleStatusRequest(const HttpServerRequest& request, string* response) {
    return HandleStatusRequestHtml(
        request,
        string(compiler_proxy_status_html5_html_start,
               compiler_proxy_status_html5_html_size),
        response);
  }

  int HandleCompilerzRequest(const HttpServerRequest& request,
                             string* response) {
    std::ostringstream ss;
    OutputOkHeaderAndBody("text/html; charset=utf-8",
                          absl::string_view(compilerz_html_html_start,
                                      compilerz_html_html_size),
                          &ss);
    *response = ss.str();
    return 200;
  }

  int HandleCompilerzScript(const HttpServerRequest& request,
                            string* response) {
    std::ostringstream ss;
    OutputOkHeaderAndBody("text/javascript; charset=utf-8",
                          absl::string_view(compilerz_script_js_start,
                                      compilerz_script_js_size),
                          &ss);
    *response = ss.str();
    return 200;
  }

  int HandleCompilerzStyle(const HttpServerRequest& request,
                           string* response) {
    std::ostringstream ss;
    OutputOkHeaderAndBody("text/css; charset=utf-8",
                          absl::string_view(compilerz_style_css_start,
                                      compilerz_style_css_size),
                          &ss);
    *response = ss.str();
    return 200;
  }

  int HandleJQuery(const HttpServerRequest& request,
                   string* response) {
    std::ostringstream ss;
    OutputOkHeaderAndBody("text/javascript; charset=utf-8",
                          absl::string_view(jquery_min_js_start,
                                      jquery_min_js_size),
                          &ss);

    *response = ss.str();
    return 200;
  }


  int HandleStatusJavaScript(const HttpServerRequest& request,
                             string* response) {
      std::ostringstream ss;
      OutputOkHeaderAndBody(
          "text/javascript; charset=utf-8",
          absl::string_view(compiler_proxy_status_script_js_start,
                            compiler_proxy_status_script_js_size),
          &ss);
      *response = ss.str();
      return 200;
  }

  int HandleContentionzJavaScript(const HttpServerRequest& request,
                                  string* response) {
      std::ostringstream ss;
      OutputOkHeaderAndBody("text/javascript; charset=utf-8",
                            absl::string_view(
                                compiler_proxy_contentionz_script_js_start,
                                compiler_proxy_contentionz_script_js_size),
                            &ss);
      *response = ss.str();
      return 200;
  }

  int HandleStatusCSS(const HttpServerRequest& request,
                      string* response) {
      std::ostringstream ss;
      OutputOkHeaderAndBody(
          "text/css; charset=utf-8",
          absl::string_view(compiler_proxy_status_style_css_start,
                            compiler_proxy_status_style_css_size),
          &ss);
      *response = ss.str();
      return 200;
  }

  // Helper function for HandleStatusRequest() and HandleStatusRequestOld().
  int HandleStatusRequestHtml(const HttpServerRequest& request,
                              const string& original_status, string* response) {
    std::ostringstream endpoints;
    GetEndpoints(&endpoints);
    std::ostringstream global_info;
    GetGlobalInfo(request, &global_info);
    string status = absl::StrReplaceAll(
        original_status,
        {{ "{{ENDPOINTS}}", endpoints.str() },
         { "{{GLOBAL_INFO}}", global_info.str() },
        }
    );

    std::ostringstream ss;
    ss << "HTTP/1.1 200 OK\r\n";
    ss << "Content-Type: text/html; charset=utf-8\r\n";
    ss << "Content-Length: " << status.size() << "\r\n";
    ss << "\r\n";

    ss << status;
    *response = ss.str();
    return 200;
  }

  void GetEndpoints(std::ostringstream* ss) {
    for (const auto& iter : http_handlers_) {
      if (absl::StartsWith(iter.first, "/api/"))
        continue;
      *ss << "<a href='" << iter.first << "'>" << iter.first << "</a>";
      *ss << " ";
    }
  }

  void GetGlobalInfo(const HttpServerRequest& request, std::ostringstream* ss) {
    static const char kBr[] = "<br>";

    *ss << "<table width=100%>";
    *ss << "<tr>";

    *ss << "<td>";
    *ss << "CompilerProxyIdPrefix: " << service_.compiler_proxy_id_prefix()
        << kBr;

    char ctime_buf[30];
    const time_t start_time = service_.start_time();
#ifndef _WIN32
    ctime_r(&start_time, ctime_buf);
#else
    ctime_s(ctime_buf, 30, &start_time);
#endif
    int uptime = static_cast<int>(time(nullptr) - start_time);
    int upsec = uptime % 60;
    int upmin = (uptime / 60) % 60;
    int uphour = (uptime / 60 / 24);
    *ss << "Started: " << ctime_buf << " -- up "
        << uphour << " hr " << upmin << " min "<< upsec << " sec" << kBr;

    *ss << "Built on " << kBuiltTimeString << kBr;

    *ss << "Built at " << kBuiltUserNameString << '@'
        << kBuiltHostNameString << ':' << kBuiltDirectoryString << kBr;

    *ss << "Built from changelist " << kBuiltRevisionString << kBr;
#ifndef NDEBUG
    *ss << "WARNING: DEBUG BINARY -- Performance may suffer" << kBr;
#endif
#ifdef ADDRESS_SANITIZER
    *ss << "WARNING: ASAN BINARY -- Performance may suffer" << kBr;
#endif
#ifdef THREAD_SANITIZER
    *ss << "WARNING: TSAN BINARY -- Performance may suffer" << kBr;
#endif
#ifdef MEMORY_SANITIZER
    *ss << "WARNING: MSAN BINARY -- Performance may suffer" << kBr;
#endif

    *ss << "PID is " << Getpid() << kBr;

    *ss << "</td>";

    *ss << "<td align=right valign=top>";

    *ss << "Running on "
        << service_.username() << "@" << service_.nodename() << ":"
        << request.server().port();
    if (!request.server().un_socket_name().empty()) {
      *ss << " + " << request.server().un_socket_name();
    }
    *ss << kBr;

    *ss << "Running at " << GetCurrentDirNameOrDie() << kBr;

    // TODO: Process size from /proc/self/stat for linux.

    // TODO: Links to /proc.

    *ss << "Log files: "
        << "<a href=\"/logz?INFO\">INFO</a> "
        << "<a href=\"/logz?WARNING\">WARNING</a> "
        << "<a href=\"/logz?ERROR\">ERROR</a>" << kBr;
#ifndef _WIN32
    *ss << "Log files(subproc): "
        << "<a href=\"/logz?subproc-INFO\">INFO</a> "
        << "<a href=\"/logz?subproc-WARNING\">WARNING</a> "
        << "<a href=\"/logz?subproc-ERROR\">ERROR</a>" << kBr;
#endif


    *ss << "</td>";

    *ss << "</tr>";
    *ss << "</table>";
  }

  int HandleTaskRequest(const HttpServerRequest& request, string* response) {
    std::ostringstream ss;
    if (request.method() != "POST") {
      // Check for cross-site script inclusion (XSSI).
      const string content =
          "unacceptable http method:" + request.method() + "\r\n";
      ss << "HTTP/1.1 405 Method Not Allowed\r\n";
      ss << "Content-Type: text/plain\r\n";
      ss << "Content-Length: " << content.size() << "\r\n";
      ss << "\r\n";
      ss << content;
      *response = ss.str();
      return 405;
    }
    if (!FLAGS_API_TASKZ_FILE_FOR_TEST.empty()) {
      string content;
      CHECK(ReadFileToString(FLAGS_API_TASKZ_FILE_FOR_TEST, &content))
          << FLAGS_API_TASKZ_FILE_FOR_TEST;
      OutputOkHeaderAndBody("application/json", content, &ss);
      *response = ss.str();
      return 200;
    }
    const string& query = request.query();
    std::map<string, string> params = ParseQuery(query);
    auto p = params.find("id");
    if (p != params.end()) {
      const string& task_id_str = p->second;
      int task_id = atoi(task_id_str.c_str());

      if (params["dump"] == "req") {
        if (!service_.DumpTaskRequest(task_id)) {
          ss << "HTTP/1.1 404 Not found\r\n";
          ss << "\r\n";
          *response = ss.str();
          return 404;
        }
        OutputOkHeader("text/plain", &ss);
        *response = ss.str();
        return 200;
      }

      string json;
      if (!service_.DumpTask(task_id, &json)) {
        ss << "HTTP/1.1 404 Not found\r\n";
        ss << "\r\n";
        *response = ss.str();
        return 404;
      }
      OutputOkHeaderAndBody("application/json", json, &ss);
      *response = ss.str();
      return 200;
    }
    long long after = 0;
    p = params.find("after");
    if (p != params.end()) {
      const string& after_str = p->second;
#ifndef _WIN32
      after = strtoll(after_str.c_str(), nullptr, 10);
#else
      after = _atoi64(after_str.c_str());
#endif
    }
    OutputOkHeader("application/json", &ss);
    Json::Value json;
    service_.DumpToJson(&json, after);
    ss << json;
    *response = ss.str();
    return 200;
  }

  int HandleAccountRequest(const HttpServerRequest& /* req */,
                           string* response) {
    std::ostringstream ss;
    OutputOkHeader("application/json", &ss);
    ss << "{";
    ss << "\"status\": "
       << EscapeString(service_.http_client()->GetHealthStatusMessage());
    const string& account = service_.http_client()->GetAccount();
    if (!account.empty()) {
      ss << ", \"account\": " << EscapeString(account);
    }
    if (account.empty()) {
      ss << ", \"text\": \"not logged in\"";
    }
    ss << "}";
    *response = ss.str();
    return 200;
  }


  int HandleStatsRequest(const HttpServerRequest& request,
                         string* response) {
    bool emit_json = false;
    for (const auto& s : absl::StrSplit(request.query(), '&')) {
      if (s == "format=json") {
        emit_json = true;
        break;
      }
    }

    std::ostringstream ss;
    if (emit_json) {
      OutputOkHeader("text/json", &ss);
      string json_string;
      service_.DumpStatsJson(&json_string, CompileService::kHumanReadable);
      ss << json_string;
    } else {
      OutputOkHeader("text/plain", &ss);
      service_.DumpStats(&ss);
    }

    *response = ss.str();
    return 200;
  }

  int HandleHistogramRequest(const HttpServerRequest& request,
                             string* response) {
    const string& query = request.query();
    bool reset = strstr(query.c_str(), "reset") != nullptr;
    std::ostringstream ss;
    OutputOkHeader("text/plain", &ss);
    service_.histogram()->DumpString(&ss);
    if (reset) {
      service_.histogram()->Reset();
      ss << "Reset done\n";
    }
    *response = ss.str();
    return 200;
  }

  int HandleHttpRpcRequest(const HttpServerRequest& /* request */,
                           string* response) {
    std::ostringstream ss;
    OutputOkHeader("text/plain", &ss);
    ss << "[http configuration]\n\n" << service_.http_client()->DebugString();
    ss << "\n\n";
    ss << "[http rpc]\n\n" << service_.http_rpc()->DebugString();
    ss << "\n\n";
    ss << "[multi store]\n\n"
       << service_.file_service()->multi_file_store()->DebugString();
    *response = ss.str();
    return 200;
  }

  int HandleThreadRequest(const HttpServerRequest& /* request */,
                          string* response) {
    std::ostringstream ss;
    OutputOkHeader("text/plain", &ss);
    ss << "[worker threads]\n\n" << service_.wm()->DebugString();
    ss << "[subprocess]\n\n"
       << SubProcessControllerClient::Get()->DebugString();
    *response = ss.str();
    return 200;
  }

  int HandleContentionRequest(const HttpServerRequest& request,
                              string* response) {
    std::ostringstream ss;

    if (g_auto_lock_stats) {
      std::unordered_set<string> skip_name = {
        "descriptor_poller::PollEvents",
        "worker_thread::NextClosure",
      };

      for (const auto& s : absl::StrSplit(request.query(), '&')) {
        if (s == "detailed=1") {
          skip_name.clear();
          break;
        }
      }

      OutputOkHeader("text/html", &ss);
      g_auto_lock_stats->Report(&ss, skip_name);
    } else {
      OutputOkHeader("text/plain", &ss);
#ifdef NO_AUTOLOCK_STAT
      ss << "disabled (built with NO_AUTOLOCK_STAT)";
#else
      ss << "disabled.  to turn on contentionz, GOMA_ENABLE_CONTENTIONZ=true";
#endif
    }
    *response = ss.str();
    return 200;
  }

  int HandleFileCacheRequest(const HttpServerRequest& /* request */,
                             string* response) {
    std::ostringstream ss;
    OutputOkHeader("text/plain", &ss);
    ss << "[file hash cache]\n\n" << service_.file_hash_cache()->DebugString();
    *response = ss.str();
    return 200;
  }

  int HandleCompilerInfoRequest(const HttpServerRequest& /* request */,
                                string* response) {
    std::ostringstream ss;
    OutputOkHeader("text/plain", &ss);
    service_.DumpCompilerInfo(&ss);
    *response = ss.str();
    return 200;
  }

  int HandleCompilerJSONRequest(const HttpServerRequest& /* request */,
                                string* response) {
    std::ostringstream ss;
    OutputOkHeader("application/json", &ss);

    Json::Value json;
    CompilerInfoCache::instance()->DumpCompilersJSON(&json);
    ss << json.toStyledString() << std::endl;
    *response = ss.str();

    return 200;
  }

  int HandleIncludeCacheRequest(const HttpServerRequest& /* request */,
                                string* response) {
    std::ostringstream ss;
    OutputOkHeader("text/plain", &ss);
    IncludeCache::DumpAll(&ss);
    *response = ss.str();
    return 200;
  }

  int HandleFlagRequest(const HttpServerRequest& /* request */,
                        string* response) {
    std::ostringstream ss;
    OutputOkHeader("text/plain", &ss);
    DumpEnvFlag(&ss);
    *response = ss.str();
    return 200;
  }

  int HandleVersionRequest(const HttpServerRequest& /* request */,
                           string* response) {
    std::ostringstream ss;
    OutputOkHeader("text/plain", &ss);
    ss << kBuiltRevisionString;
    *response = ss.str();
    return 200;
  }

  int HandleHealthRequest(const HttpServerRequest& request,
                          string* response) {
    const string& query = request.query();
    const string health_status =
        service_.http_client()->GetHealthStatusMessage();
    *response = "HTTP/1.1 200 OK\r\n\r\n" + health_status;
    if (!setting_.empty()) {
      *response += "\nsetting=" + setting_;
    }
    LOG(INFO) << "I am healthy:" << health_status
              << " to pid:" << request.peer_pid()
              << " query:" << query;
    // gomacc checkhealth use ?pid=<pid> as query.
    // note that: build_nexe.py also checks /healthz.
    if (request.peer_pid() != 0 || !query.empty()) {
      service_.wm()->DebugLog();
    }
    return 200;
  }

  int HandlePortRequest(const HttpServerRequest& request,
                        string* response) {
    LOG(INFO) << "handle portz port=" << request.server().port();
    HttpPortResponse resp;
    resp.set_port(request.server().port());
    string serialized_resp;
    resp.SerializeToString(&serialized_resp);

    std::ostringstream oss;
    oss << "HTTP/1.1 200 OK\r\n"
        << "Content-Type: binary/x-protocol-buffer\r\n"
        << "Content-Length: " << serialized_resp.size() << "\r\n\r\n"
        << serialized_resp;
    *response = oss.str();
    return 200;
  }

  int HandleLogRequest(const HttpServerRequest& request,
                       string* response) {
    std::ostringstream oss;
    const string& log_request = request.query();
    if (log_request.empty()) {
      string content = ("<a href=\"?INFO\">INFO</a> /"
                        "<a href=\"?WARNING\">WARNING</a> /"
                        "<a href=\"?ERROR\">ERROR</a>"
#ifndef _WIN32
                        "<br />"
                        "<a href=\"?subproc-INFO\">subproc-INFO</a> /"
                        "<a href=\"?subproc-WARNING\">subproc-WARNING</a> /"
                        "<a href=\"?subproc-ERROR\">subproc-ERROR</a>"
#endif
                        "<br />");
      oss << "HTTP/1.1 200 OK\r\n"
          << "Content-Type: text/html\r\n"
          << "Content-Length: " << content.size() << "\r\n\r\n"
          << content;
    } else {
      const std::vector<string>& log_dirs = google::GetLoggingDirectories();
      if (log_dirs.empty()) {
        LOG(ERROR) << "No logging directories";
        return 404;
      }
      string log_suffix;
      string log_type = log_request;
      if (log_request.find("subproc-") == 0) {
        log_suffix = "-subproc";
        log_type = log_request.substr(strlen("subproc-"));
      }
      if (log_type != "INFO" && log_type != "WARNING" &&
          log_type != "ERROR" && log_type != "FATAL") {
        LOG(WARNING) << "Unknown log type: " << log_type;
        return 404;
      }
      string log_filename =
          file::JoinPath(log_dirs[0], myname_ + log_suffix + "." + log_type);
#ifdef _WIN32
      const string& original_log = FindLogFile(log_dirs[0], myname_, log_type);
      // Workaround GLOG not opening file in share read.
      if (!CopyFileA(original_log.c_str(), log_filename.c_str(), FALSE)) {
        log_filename = original_log;  // Can't copy, let's just try share read.
      }
#endif
      string log;
      if (!ReadFileToString(log_filename.c_str(), &log)) {
        return 404;
      }
      oss << "HTTP/1.1 200 OK\r\n"
          << "Content-Type: text/plain\r\n"
          << "Content-Length: " << log.size() << "\r\n\r\n"
          << log;
    }

    *response = oss.str();
    return 200;
  }

  int HandleErrorStatusRequest(const HttpServerRequest&, string* response) {
    std::ostringstream ss;
    OutputOkHeader("application/json", &ss);
    service_.DumpErrorStatus(&ss);
    *response = ss.str();
    return 200;
  }

#ifdef HAVE_COUNTERZ
  int HandleCounterRequest(const HttpServerRequest&, string* response) {
    // TODO: implement better view using javascript if necessary.
    std::ostringstream ss;
    OutputOkHeader("application/json", &ss);
    Json::Value json;
    if (Counterz::Instance() != nullptr) {
      Counterz::Instance()->DumpToJson(&json);
    } else {
      LOG(ERROR) << "counterz is used before Init().";
      json = "counterz is used before Init().";
    }

    ss << json.toStyledString() << std::endl;
    *response = ss.str();
    return 200;
  }
#endif

#ifdef _WIN32
  void ExecDoneInMulti(CompileService::MultiRpcController* rpc, int i) {
    if (rpc->ExecDone(i)) {
      std::unique_ptr<CompileService::MultiRpcController> rpc_autodeleter(rpc);
      rpc->SendReply();
    }
  }
#endif

  void ExecDone(CompileService::RpcController* rpc, ExecResp* resp) {
    std::unique_ptr<CompileService::RpcController> rpc_autodeleter(rpc);
    std::unique_ptr<ExecResp> resp_autodeleter(resp);

    rpc->SendReply(*resp);
  }

  void SendErrorMessage(
      ThreadpoolHttpServer::HttpServerRequest* http_server_request,
      int response_code, const string& status_message) {
    std::ostringstream http_response_message;
    http_response_message
        << "HTTP/1.1 " << response_code << " " << status_message << "\r\n\r\n";
    http_server_request->SendReply(http_response_message.str());
  }

  void RunCleanOldLogs() {
    if (FLAGS_LOG_CLEAN_INTERVAL <= 0) {
      LOG(WARNING) << "log clean interval <= 0, "
                   << "but attempted cleaning old logs";
      return;
    }
    // Switch from alarm worker to normal worker.
    service_.wm()->RunClosure(
        FROM_HERE,
        NewCallback(
            this, &CompilerProxyHttpHandler::CleanOldLogs),
        WorkerThreadManager::PRIORITY_LOW);
  }

  void CleanOldLogs() {
    if (FLAGS_LOG_CLEAN_INTERVAL <= 0)
      return;
    log_cleaner_.CleanOldLogs(
        absl::Now() - absl::Seconds(FLAGS_LOG_CLEAN_INTERVAL));
  }

  void RunTrackMemory() {
    if (FLAGS_MEMORY_TRACK_INTERVAL <= 0) {
      LOG(WARNING) << "memory track interval <= 0, "
                   << "but attempted tracking memory";
      return;
    }

    // Switch from alarm worker to normal worker.
    service_.wm()->RunClosure(
        FROM_HERE,
        NewCallback(
            this, &CompilerProxyHttpHandler::TrackMemory),
        WorkerThreadManager::PRIORITY_LOW);
  }

  void TrackMemory() LOCKS_EXCLUDED(memory_mu_) {
    int64_t memory_byte = GetConsumingMemoryOfCurrentProcess();

    {
      AUTOLOCK(lock, &memory_mu_);

      // When compiler_proxy is idle, the consumed memory size won't change
      // so much. To reduce log size, we don't do anything. b/110089630
      // On Linux, memory size looks stable, but not for the other platforms.
      // We allow 1MB margin.
      int64_t diff = memory_byte - last_memory_byte_;
      if (-1024 * 1024 < diff && diff < 1024 * 1024) {
        return;
      }

      last_memory_byte_ = memory_byte;
    }

    int64_t warning_threshold =
        static_cast<int64_t>(FLAGS_MEMORY_WARNING_THRESHOLD_IN_MB) *
        1024 * 1024;
    if (memory_byte >= warning_threshold) {
      LOG(WARNING) << "memory tracking: consuming memory = "
                   << memory_byte << " bytes, which is higher than "
                   << "warning threshold "
                   << warning_threshold << " bytes";
    } else {
      LOG(INFO) << "memory tracking: consuming memory = "
                << memory_byte << " bytes";
    }

    if (service_.log_service()) {
      MemoryUsageLog memory_usage_log;
      memory_usage_log.set_compiler_proxy_start_time(service_.start_time());
      memory_usage_log.set_compiler_proxy_user_agent(kUserAgentString);
      if (FLAGS_SEND_USER_INFO) {
        memory_usage_log.set_username(service_.username());
        memory_usage_log.set_nodename(service_.nodename());
      }

      time_t current_time;
      time(&current_time);
      memory_usage_log.set_memory(memory_byte);
      memory_usage_log.set_time(current_time);

      service_.log_service()->SaveMemoryUsageLog(memory_usage_log);
    }
  }

  void DumpStatsToInfoLog() {
    // TODO: Remove this after diagnose_goma_log.py and
    // diagnose_goma_log_server reads json format stats.
    {
      std::ostringstream ss;
      service_.DumpStats(&ss);
      LOG(INFO) << "Dumping stats...\n"
                << ss.str();
    }

    // Also dump json format. Using FastWriter for compaction.
    {
      std::string json_string;
      service_.DumpStatsJson(&json_string,
                             CompileService::kFastHumanUnreadable);
      LOG(INFO) << "Dumping json stats...\n"
                << json_string;
    }
  }

  void DumpHistogramToInfoLog() {
    std::ostringstream ss;
    service_.histogram()->DumpString(&ss);

    LOG(INFO) << "Dumping histogram...\n"
              << ss.str();
  }

  void DumpIncludeCacheLogToInfoLog() {
    std::ostringstream ss;
    IncludeCache::DumpAll(&ss);

    LOG(INFO) << "Dumping include cache...\n"
              << ss.str();
  }

  void DumpContentionLogToInfoLog() {
    std::ostringstream ss;
    g_auto_lock_stats->TextReport(&ss);
    LOG(INFO) << "Dumping contention...\n"
              << ss.str();
  }

  void DumpStatsProto() {
    if (FLAGS_DUMP_STATS_FILE.empty())
      return;

    service_.DumpStatsToFile(FLAGS_DUMP_STATS_FILE);
  }

  void DumpCounterz() {
#ifdef HAVE_COUNTERZ
    if (FLAGS_DUMP_COUNTERZ_FILE.empty())
      return;

    Counterz::Dump(FLAGS_DUMP_COUNTERZ_FILE);
#endif  // HAVE_COUNTERZ
  }

  void DumpDirectiveOptimizer() {
    std::ostringstream ss;
    CppDirectiveOptimizer::DumpStats(&ss);
    LOG(INFO) << "Dumping directive optimizer...\n"
              << ss.str();
  }

#if HAVE_HEAP_PROFILER
  int HandleHeapRequest(const HttpServerRequest& request,
                        string* response) {
    *response = "HTTP/1.1 200 OK\r\n\r\n";
    if (IsHeapProfilerRunning()) {
      HeapProfilerDump("requested by /heapz");
      HeapProfilerStop();
      *response += "heap profiler stopped. see " +
          compiler_proxy_heap_profile_file_ + ".*.heap";
    } else {
      HeapProfilerStart(compiler_proxy_heap_profile_file_.c_str());
      *response += "heap profiler starts.";
    }
    return 200;
  }
#endif
#if HAVE_CPU_PROFILER
  int HandleProfileRequest(const HttpServerRequest& request,
                           string* response) {
    *response = "HTTP/1.1 200 OK\r\n\r\n";
    if (cpu_profiling_) {
      ProfilerStop();
      cpu_profiling_ = false;
      *response += "cpu profiler stopped. see " +
          compiler_proxy_cpu_profile_file_;
    } else {
      ProfilerStart(compiler_proxy_cpu_profile_file_.c_str());
      cpu_profiling_ = true;
      *response += "cpu profiler starts.";
    }
    return 200;
  }
#endif

  void NewLoginState(int port, string* login_state, string* redirect_uri) {
    *login_state = GetRandomAlphanumeric(32);
    std::ostringstream ss;
    ss << "http://localhost:" << port << "/api/authz";
    *redirect_uri = ss.str();
    AUTOLOCK(lock, &login_state_mu_);
    oauth2_login_state_ = *login_state;
    oauth2_redirect_uri_ = *redirect_uri;
  }

  bool CheckLoginState(const string& state) const {
    AUTOLOCK(lock, &login_state_mu_);
    return oauth2_login_state_ == state;
  }

  string GetRedirectURI() const {
    AUTOLOCK(lock, &login_state_mu_);
    return oauth2_redirect_uri_;
  }

  bool ShouldTrace() {
    if (FLAGS_RPC_TRACE_PERIOD < 1) {
      return false;
    }
    AUTOLOCK(lock, &rpc_sent_count_mu_);
    return rpc_sent_count_++ % FLAGS_RPC_TRACE_PERIOD == 0;
  }

  const string myname_;
  const string setting_;
  CompileService service_;
  LogCleaner log_cleaner_;
  PeriodicClosureId log_cleaner_closure_id_;
  PeriodicClosureId memory_tracker_closure_id_;
  mutable Lock rpc_sent_count_mu_;
  uint64_t rpc_sent_count_ GUARDED_BY(rpc_sent_count_mu_);

  std::map<string, HttpHandlerMethod> http_handlers_;
  std::map<string, HttpHandlerMethod> internal_http_handlers_;

  const string tmpdir_;

  mutable Lock memory_mu_;
  int64_t last_memory_byte_ GUARDED_BY(memory_mu_);

#if HAVE_HEAP_PROFILER
  const string compiler_proxy_heap_profile_file_;
#endif
#if HAVE_CPU_PROFILER
  const string compiler_proxy_cpu_profile_file_;
  bool cpu_profiling_;
#endif

  // Default http_options_ for any http clients in compiler_proxy
  // such as oauth2 etc.
  HttpClient::Options http_options_;

  mutable Lock login_state_mu_;
  string oauth2_login_state_;
  string oauth2_redirect_uri_;

  DISALLOW_COPY_AND_ASSIGN(CompilerProxyHttpHandler);
};

}  // namespace devtools_goma

#ifndef _WIN32
bool CheckFileOwnedByMyself(const string& filename, uid_t uid) {
  struct stat st;
  if (stat(filename.c_str(), &st) == -1)
    return true;
  if (st.st_uid == uid)
    return true;

  std::cerr << "GOMA: compiler_proxy:"
            << " other user (" << st.st_uid << ") owns " << filename
            << ", so you (" << uid << ") can not run compiler_proxy. "
            << std::endl;
  std::cerr << "GOMA: remove " << filename << std::endl;
  return false;
}

ScopedFd LockMyself(const string& filename, int port) {
  // Open myself and lock it during execution.
  std::ostringstream filename_buf;
  filename_buf << filename << "." << port;
  string lock_filename = filename_buf.str();
  if (!CheckFileOwnedByMyself(lock_filename.c_str(), getuid())) {
    exit(1);
  }
  ScopedFd fd(open(lock_filename.c_str(), O_RDONLY|O_CREAT, S_IRUSR));
  if (!fd.valid()) {
    std::cerr << "GOMA: compiler_proxy: "
              << "failed to open lock file:" << lock_filename << std::endl;
    exit(1);
  }
  int ret = flock(fd.fd(), LOCK_EX | LOCK_NB);
  if (ret == -1 && errno == EWOULDBLOCK) {
    std::cerr << "GOMA: compiler_proxy: "
              << "there is already someone else with lock" << std::endl;
    exit(1);
  }
  return fd;
}
#endif

void InitResourceLimits(int* nfile) {
#ifndef _WIN32
  struct rlimit lim;
  PCHECK(getrlimit(RLIMIT_NOFILE, &lim) == 0);
  *nfile = static_cast<int>(lim.rlim_cur);
  const rlim_t prev = lim.rlim_cur;
  rlim_t open_max = static_cast<rlim_t>(sysconf(_SC_OPEN_MAX));
#ifdef OPEN_MAX
  open_max = std::max(open_max, static_cast<rlim_t>(OPEN_MAX));
#endif
  open_max = std::max(open_max, lim.rlim_cur);
#ifdef __MACH__
  // Choose smaller size from sysctl.  (b/9548636)
  int mib[2] = {CTL_KERN, -1};
  static const int kSecondMibs[] = {KERN_MAXFILES, KERN_MAXFILESPERPROC};
  for (const auto& it : kSecondMibs) {
    rlim_t tmp;
    size_t length = sizeof(tmp);
    mib[1] = it;
    PCHECK(sysctl(mib, 2, &tmp, &length, nullptr, 0) == 0) << it;
    open_max = std::min(tmp, open_max);
  }
  // setrlimit(3) will fail with EINVAL if launchctl sets smaller limit,
  // which default is 256.  b/11596636
#endif
  lim.rlim_cur = std::min(open_max, lim.rlim_max);
  if (setrlimit(RLIMIT_NOFILE, &lim) != 0) {
    // we might get EPERM or EINVAL if we try to increase RLIMIT_NOFILE above
    // the current kernel maxium.
    PLOG(ERROR) << "setrlimit(RLIMIT_NOFILE, &lim) != 0"
                << " rlim_cur:" << lim.rlim_cur
                << " rlim_max:" << lim.rlim_max
                << " rlim_cur would remain " << prev;
    lim.rlim_cur = prev;
  } else {
    LOG(INFO) << "setrlimit RLIMIT_NOFILE " << prev << " -> " << lim.rlim_cur;
  }
  *nfile = static_cast<int>(lim.rlim_cur);
#else
  *nfile = FLAGS_COMPILER_PROXY_MAX_SOCKETS;
#endif
}

void InitTrustedIps(devtools_goma::TrustedIpsManager* trustedipsmanager) {
  for (auto&& ip : absl::StrSplit(FLAGS_COMPILER_PROXY_TRUSTED_IPS,
                                  ',',
                                  absl::SkipEmpty())) {
    trustedipsmanager->AddAllow(string(ip));
  }
}

namespace devtools_goma {

void DepsCacheInit() {
  string cache_filename;
  if (!FLAGS_DEPS_CACHE_FILE.empty()) {
    cache_filename = file::JoinPathRespectAbsolute(GetCacheDirectory(),
                                                   FLAGS_DEPS_CACHE_FILE);
  }

  DepsCache::Init(cache_filename,
                  FLAGS_DEPS_CACHE_IDENTIFIER_ALIVE_DURATION,
                  FLAGS_DEPS_CACHE_TABLE_THRESHOLD,
                  FLAGS_DEPS_CACHE_MAX_PROTO_SIZE_IN_MB);
}

void CompilerInfoCacheInit() {
  CompilerInfoCache::Init(GetCacheDirectory(), FLAGS_COMPILER_INFO_CACHE_FILE,
                          FLAGS_COMPILER_INFO_CACHE_HOLDING_TIME_SEC);
}

}  // namespace devtools_goma

int main(int argc, char* argv[], const char* envp[]) {
  devtools_goma::Init(argc, argv, envp);

#if HAVE_COUNTERZ
  if (FLAGS_ENABLE_COUNTERZ) {
    devtools_goma::Counterz::Init();
  }
#endif

  if (FLAGS_ENABLE_GLOBAL_FILE_STAT_CACHE ||
      FLAGS_ENABLE_GLOBAL_FILE_ID_CACHE) {
    devtools_goma::GlobalFileStatCache::Init();
  }

  const string tmpdir = FLAGS_TMP_DIR;
#ifndef _WIN32
  const string compiler_proxy_addr = file::JoinPathRespectAbsolute(
      tmpdir,
      FLAGS_COMPILER_PROXY_SOCKET_NAME);

  if (!CheckFileOwnedByMyself(compiler_proxy_addr, getuid())) {
    exit(1);
  }

  const string lock_filename = file::JoinPathRespectAbsolute(
      tmpdir,
      FLAGS_COMPILER_PROXY_LOCK_FILENAME);
  ScopedFd lockfd(
      LockMyself(lock_filename,
                 FLAGS_COMPILER_PROXY_PORT));
  if (FLAGS_COMPILER_PROXY_DAEMON_MODE) {
    int fd[2];
    PCHECK(pipe(fd) == 0);
    pid_t pid;
    if ((pid = fork())) {
      PCHECK(pid > 0);
      // Get pid from daemonized process
      close(fd[1]);
      pid_t server_pid;
      PCHECK(read(fd[0], &server_pid, sizeof(server_pid)) ==
             sizeof(server_pid));
      std::cout << server_pid << std::endl;
      exit(0);
    }
    close(fd[0]);
    std::set<int> preserve_fds;
    preserve_fds.insert(lockfd.fd());
    Daemonize(
        file::JoinPathRespectAbsolute(tmpdir,
                                      FLAGS_COMPILER_PROXY_DAEMON_STDERR),
        fd[1],
        preserve_fds);
  }

  // Initialize rand.
  srand(static_cast<unsigned int>(time(nullptr)));

  // Do not die with a SIGHUP and SIGPIPE.
  signal(SIGHUP, SIG_IGN);
  signal(SIGPIPE, SIG_IGN);
#else
  const string compiler_proxy_addr = FLAGS_COMPILER_PROXY_SOCKET_NAME;
  WinsockHelper wsa;
  devtools_goma::ScopedFd lock_fd;
  std::ostringstream filename_buf;
  filename_buf << "Global\\" << FLAGS_COMPILER_PROXY_LOCK_FILENAME << "."
               << FLAGS_COMPILER_PROXY_PORT;
  string lock_filename = filename_buf.str();

  lock_fd.reset(CreateEventA(nullptr, TRUE, FALSE, lock_filename.c_str()));
  DWORD last_error = GetLastError();
  if (last_error == ERROR_ALREADY_EXISTS) {
    std::cerr << "GOMA: compiler proxy: already existed" << std::endl;
    exit(1);
  }
  if (!lock_fd.valid()) {
    LOG(ERROR) << "Cannot acquire global named object: " << last_error;
    exit(1);
  }

#ifdef NDEBUG
  // Sets error mode to SEM_FAILCRITICALERRORS and SEM_NOGPFAULTERRORBOX
  // to prevent from popping up message box on error.
  // We don't use CREATE_DEFAULT_ERROR_MODE for dwCreationFlags in
  // CreateProcess function.
  // http://msdn.microsoft.com/en-us/library/windows/desktop/ms680621(v=vs.85).aspx
  UINT old_error_mode = SetErrorMode(
      SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
  LOG(INFO) << "Set error mode from " << old_error_mode
            << " to " << GetErrorMode();
#endif
#endif
  devtools_goma::SubProcessController::Options subproc_options;
  subproc_options.max_subprocs = FLAGS_MAX_SUBPROCS;
  subproc_options.max_subprocs_low_priority = FLAGS_MAX_SUBPROCS_LOW;
  subproc_options.max_subprocs_heavy_weight = FLAGS_MAX_SUBPROCS_HEAVY;
  subproc_options.dont_kill_subprocess = FLAGS_DONT_KILL_SUBPROCESS;
  if (!FLAGS_DONT_KILL_COMMANDS.empty()) {
    for (auto&& cmd_view : absl::StrSplit(FLAGS_DONT_KILL_COMMANDS,
                                          ',',
                                          absl::SkipEmpty())) {
      string cmd(cmd_view);
#ifdef _WIN32
      absl::AsciiStrToLower(&cmd);
#endif
      subproc_options.dont_kill_commands.insert(cmd);
    }
  }
  devtools_goma::SubProcessController::Initialize(argv[0], subproc_options);

  devtools_goma::InitLogging(argv[0]);
  if (FLAGS_COMPILER_PROXY_ENABLE_CRASH_DUMP) {
    devtools_goma::InitCrashReporter(devtools_goma::GetCrashDumpDirectory());
    LOG(INFO) << "breakpad is enabled";
  }

  std::unique_ptr<devtools_goma::AutoUpdater> auto_updater;
  if (FLAGS_ENABLE_AUTO_UPDATE) {
    auto_updater =
        absl::make_unique<devtools_goma::AutoUpdater>(FLAGS_CTL_SCRIPT_NAME);
    if (auto_updater->my_version() > 0) {
      LOG(INFO) << "goma version:" << auto_updater->my_version();
    }
    auto_updater->SetEnv(envp);
  } else {
    LOG(INFO) << "auto updater is disabled";
  }

  int max_nfile = 0;
  InitResourceLimits(&max_nfile);
  CHECK_GT(max_nfile, 0);
  int max_num_sockets = max_nfile;
  LOG(INFO) << "max_num_sockets=" << max_num_sockets
            << " max_nfile=" << max_nfile;

  devtools_goma::WorkerThreadManager wm;
  wm.Start(FLAGS_COMPILER_PROXY_THREADS);

  devtools_goma::SubProcessControllerClient::Initialize(&wm, tmpdir);

  devtools_goma::InstallReadCommandOutputFunc(
      &devtools_goma::SubProcessTask::ReadCommandOutput);

  devtools_goma::IncludeFileFinder::Init(FLAGS_ENABLE_GCH_HACK);

  devtools_goma::IncludeCache::Init(
      FLAGS_MAX_INCLUDE_CACHE_SIZE, !FLAGS_DEPS_CACHE_FILE.empty());
  devtools_goma::ListDirCache::Init(FLAGS_MAX_LIST_DIR_CACHE_ENTRY_NUM);

  std::unique_ptr<devtools_goma::WorkerThreadRunner> init_deps_cache(
      new devtools_goma::WorkerThreadRunner(
          &wm, FROM_HERE,
          devtools_goma::NewCallback(devtools_goma::DepsCacheInit)));
  std::unique_ptr<devtools_goma::WorkerThreadRunner> init_compiler_info_cache(
      new devtools_goma::WorkerThreadRunner(
          &wm, FROM_HERE,
          devtools_goma::NewCallback(devtools_goma::CompilerInfoCacheInit)));

  devtools_goma::TrustedIpsManager trustedipsmanager;
  InitTrustedIps(&trustedipsmanager);

  string setting;
  if (!FLAGS_SETTINGS_SERVER.empty()) {
    setting = ApplySettings(FLAGS_SETTINGS_SERVER, FLAGS_ASSERT_SETTINGS, &wm);
  }
  std::unique_ptr<devtools_goma::CompilerProxyHttpHandler> handler(
      new devtools_goma::CompilerProxyHttpHandler(
          string(file::Basename(argv[0])), setting, tmpdir, &wm));

  ThreadpoolHttpServer server(FLAGS_COMPILER_PROXY_LISTEN_ADDR,
                              FLAGS_COMPILER_PROXY_PORT,
                              FLAGS_COMPILER_PROXY_NUM_FIND_PORTS,
                              &wm,
                              FLAGS_COMPILER_PROXY_HTTP_THREADS,
                              handler.get(),
                              max_num_sockets);
  server.SetMonitor(handler.get());
  server.SetTrustedIpsManager(&trustedipsmanager);
  CHECK(!compiler_proxy_addr.empty())
      << "broken compiler_proxy_addr configuration. "
      << "set GOMA_COMPILER_PROXY_SOCKET_NAME"
      << " for compiler_proxy ipc addr";
  server.StartIPC(compiler_proxy_addr,
                  FLAGS_COMPILER_PROXY_THREADS,
                  FLAGS_MAX_OVERCOMMIT_INCOMING_SOCKETS);
  // TCP serves only status pages, no limit.
  if (auto_updater) {
    auto_updater->Start(&server, FLAGS_AUTO_UPDATE_IDLE_COUNT);
    handler->SetAutoUpdater(std::move(auto_updater));
  }
  if (FLAGS_WATCHDOG_TIMER > 0) {
    std::unique_ptr<devtools_goma::Watchdog> watchdog(
        new devtools_goma::Watchdog);
    std::vector<string> env;
    env.push_back("GOMA_COMPILER_PROXY_SOCKET_NAME=" + compiler_proxy_addr);
    env.push_back("PATH=" + devtools_goma::GetEnv("PATH"));
    env.push_back("PATHEXT=" + devtools_goma::GetEnv("PATHEXT"));
    env.push_back("USER=" + devtools_goma::GetUsername());
    env.push_back("GOMA_TMP_DIR=" + FLAGS_TMP_DIR);
    handler->SetWatchdog(std::move(watchdog), env,
                         &server, FLAGS_WATCHDOG_TIMER);
  }

  devtools_goma::LocalOutputCache::Init(
      FLAGS_LOCAL_OUTPUT_CACHE_DIR,
      &wm,
      FLAGS_LOCAL_OUTPUT_CACHE_MAX_CACHE_AMOUNT_IN_MB,
      FLAGS_LOCAL_OUTPUT_CACHE_THRESHOLD_CACHE_AMOUNT_IN_MB,
      FLAGS_LOCAL_OUTPUT_CACHE_MAX_ITEMS,
      FLAGS_LOCAL_OUTPUT_CACHE_THRESHOLD_ITEMS);

  init_deps_cache.reset();
  init_compiler_info_cache.reset();
  // Show memory just before server loop to understand how much memory is
  // used for initialization.
  handler->TrackMemoryOneshot();

  LOG(INFO) << "server loop start";
  if (server.Loop() != 0) {
    LOG(ERROR) << "Server failed";
    exit(1);
  }
  LOG(INFO) << "server loop end";
  devtools_goma::FlushLogFiles();
  server.StopIPC();
#ifndef _WIN32
  flock(lockfd.fd(), LOCK_UN);
  lockfd.reset(-1);
#else
  lock_fd.Close();
#endif
  LOG(INFO) << "unlock compiler_proxy";
  devtools_goma::FlushLogFiles();
  devtools_goma::SubProcessControllerClient::Get()->Quit();
  devtools_goma::LocalOutputCache::Quit();
  server.Wait();
  handler->Wait();
  devtools_goma::CompilerInfoCache::Quit();
  devtools_goma::DepsCache::Quit();
  devtools_goma::IncludeCache::Quit();
  devtools_goma::ListDirCache::Quit();
  devtools_goma::SubProcessControllerClient::Get()->Shutdown();

  handler.reset();
  wm.Finish();
#ifndef _WIN32
  // compiler_proxy only creates subprocess controller server as child process,
  // so waits for the status of it;
  int status;
  PCHECK(wait(&status) > 0);
  LOG(INFO) << "wait:" << status;
#endif

  if (FLAGS_ENABLE_GLOBAL_FILE_STAT_CACHE ||
      FLAGS_ENABLE_GLOBAL_FILE_ID_CACHE) {
    devtools_goma::GlobalFileStatCache::Quit();
  }

#if HAVE_COUNTERZ
  if (FLAGS_ENABLE_COUNTERZ) {
    devtools_goma::Counterz::Quit();
  }
#endif

  return 0;
}
