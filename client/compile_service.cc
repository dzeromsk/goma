// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "compile_service.h"

#ifndef _WIN32
#include <sys/types.h>
#else
#include "config_win.h"
#endif

#include <algorithm>
#include <deque>
#include <fstream>
#include <memory>
#include <sstream>

#include <json/json.h>

#include "absl/strings/match.h"
#include "absl/strings/str_join.h"
#include "atomic_stats_counter.h"
#include "autolock_timer.h"
#include "auto_updater.h"
#include "callback.h"
#include "compile_stats.h"
#include "compile_task.h"
#include "compiler_flags.h"
#include "compiler_proxy_histogram.h"
#include "compiler_proxy_info.h"
#include "deps_cache.h"
#include "file_hash_cache.h"
#include "file_helper.h"
#include "file_id.h"
#include "glog/logging.h"
#include "goma_file_http.h"
#include "goma_hash.h"
#include "google/protobuf/util/json_util.h"
#include "http.h"
#include "http_rpc.h"
#include "include_cache.h"
#include "include_processor.h"
#include "ioutil.h"
#include "local_output_cache.h"
#include "lockhelper.h"
#include "log_service_client.h"
#include "machine_info.h"
#include "multi_http_rpc.h"
#include "mypath.h"
#include "path.h"
#include "path_resolver.h"
MSVC_PUSH_DISABLE_WARNING_FOR_PROTO()
#include "prototmp/error_notice.pb.h"
#include "prototmp/goma_stats.pb.h"
#include "prototmp/goma_statz_stats.pb.h"
MSVC_POP_WARNING()
#include "timestamp.h"
#include "util.h"
#include "watchdog.h"
#include "worker_thread_manager.h"

#ifdef _WIN32
# include "file_helper.h"
#endif

namespace {
// Path separators are platform dependent
#ifndef _WIN32
const char* kSep = ":";
const char* kCurrentDir = ":.:";
#else
const char* kSep = ";";
const char* kCurrentDir = ";.;";
#endif

}  // anonymous namespace

namespace devtools_goma {

class CompareTaskHandlerTime {
 public:
  bool operator()(CompileTask* a, CompileTask* b) const {
    return a->stats().handler_time() > b->stats().handler_time();
  }
};

CompileService::RpcController::RpcController(
     ThreadpoolHttpServer::HttpServerRequest* http_server_request)
  : http_server_request_(http_server_request),
    server_port_(http_server_request->server().port()),
#ifdef _WIN32
    multi_rpc_(nullptr),
#endif
    gcc_req_size_(0),
    gcc_resp_size_(nullptr) {
  DCHECK(http_server_request_ != nullptr);
}

CompileService::RpcController::~RpcController() {
  DCHECK(http_server_request_ == nullptr);
}

#ifdef _WIN32
void CompileService::RpcController::AttachMultiRpcController(
    CompileService::MultiRpcController* multi_rpc) {
  CHECK_EQ(gcc_req_size_, 0U);
  multi_rpc_ = multi_rpc;
  http_server_request_ = nullptr;
}
#endif

// Returns true if header looks like a request coming from browser.
// see also goma_ipc.cc:GomaIPC::SendRequest.
bool IsBrowserRequest(absl::string_view header) {
  if (header.find("\r\nHost: 0.0.0.0\r\n") != absl::string_view::npos) {
    return false;
  }
  // TODO: check it doesn't contain Origin header etc?
  return true;
}

bool CompileService::RpcController::ParseRequest(ExecReq* req) {
  absl::string_view header = http_server_request_->header();
  if (http_server_request_->request_content_length() <= 0) {
    LOG(WARNING) << "Invalid request from client (no content-length):"
                 << header;
    return false;
  }
  // it won't protect request by using network communications API.
  // https://developer.chrome.com/apps/app_network
  if (IsBrowserRequest(header)) {
    LOG(WARNING) << "Unallowed request from browser:" << header;
    return false;
  }
  if (header.find("\r\nContent-Type: binary/x-protocol-buffer\r\n") ==
      absl::string_view::npos) {
    LOG(WARNING) << "Invalid request from client (invalid content-type):"
                 << header;
    return false;
  }

  gcc_req_size_ = http_server_request_->request_content_length();
  return req->ParseFromArray(
        http_server_request_->request_content(),
        http_server_request_->request_content_length());
}

void CompileService::RpcController::SendReply(const ExecResp& resp) {
  CHECK(http_server_request_ != nullptr);

  size_t gcc_resp_size = resp.ByteSize();
  std::ostringstream http_response_message;
  http_response_message
    << "HTTP/1.1 200 OK\r\n"
    << "Content-Type: binary/x-protocol-buffer\r\n"
    << "Content-Length: " << gcc_resp_size << "\r\n\r\n";
  string response_string = http_response_message.str();
  int header_size = response_string.size();
  response_string.resize(header_size + gcc_resp_size);
  resp.SerializeToArray(&response_string[header_size], gcc_resp_size);
  http_server_request_->SendReply(response_string);
  http_server_request_ = nullptr;
  if (gcc_resp_size_ != nullptr)
    *gcc_resp_size_ = gcc_resp_size;
}

void CompileService::RpcController::NotifyWhenClosed(OneshotClosure* callback) {
#ifdef _WIN32
  if (multi_rpc_) {
    multi_rpc_->NotifyWhenClosed(callback);
    return;
  }
#endif
  CHECK(http_server_request_ != nullptr);
  http_server_request_->NotifyWhenClosed(callback);
}

#ifdef _WIN32
CompileService::MultiRpcController::MultiRpcController(
    WorkerThreadManager* wm,
    ThreadpoolHttpServer::HttpServerRequest* http_server_request)
    : wm_(wm),
      caller_thread_id_(wm->GetCurrentThreadId()),
      http_server_request_(http_server_request),
      resp_(new MultiExecResp),
      ALLOW_THIS_IN_INITIALIZER_LIST(
          closed_callback_(NewCallback(
              this, &CompileService::MultiRpcController::RequestClosed))),
      gcc_req_size_(0) {
  DCHECK(http_server_request_ != nullptr);
  http_server_request_->NotifyWhenClosed(closed_callback_);
}

CompileService::MultiRpcController::~MultiRpcController() {
  DCHECK(http_server_request_ == nullptr);
  CHECK(rpcs_.empty());
  CHECK_EQ(caller_thread_id_, wm_->GetCurrentThreadId());
}

bool CompileService::MultiRpcController::ParseRequest(MultiExecReq* req) {
  CHECK_EQ(caller_thread_id_, wm_->GetCurrentThreadId());
  if (http_server_request_->request_content_length() <= 0) {
    LOG(WARNING) << "Invalid request from client (no content-length):"
                 << http_server_request_->request();
    return false;
  }
  gcc_req_size_ = http_server_request_->request_content_length();
  bool ok = req->ParseFromArray(
        http_server_request_->request_content(),
        http_server_request_->request_content_length());
  if (ok) {
    for (int i = 0; i < req->req_size(); ++i) {
      CompileService::RpcController* rpc =
          new CompileService::RpcController(http_server_request_);
      rpc->AttachMultiRpcController(this);
      rpcs_.push_back(rpc);
      resp_->add_response();
    }
    CHECK_EQ(req->req_size(), static_cast<int>(rpcs_.size()));
    CHECK_EQ(req->req_size(), resp_->response_size());
  }
  return ok;
}

CompileService::RpcController* CompileService::MultiRpcController::rpc(
    int i) const {
  CHECK_EQ(caller_thread_id_, wm_->GetCurrentThreadId());
  DCHECK_GE(i, 0);
  DCHECK_LT(i, static_cast<int>(rpcs_.size()));
  return rpcs_[i];
}

ExecResp* CompileService::MultiRpcController::mutable_resp(int i) const {
  CHECK_EQ(caller_thread_id_, wm_->GetCurrentThreadId());
  DCHECK_GE(i, 0);
  DCHECK_LT(i, resp_->response_size());
  return resp_->mutable_response(i)->mutable_resp();
}

bool CompileService::MultiRpcController::ExecDone(int i) {
  CHECK_EQ(caller_thread_id_, wm_->GetCurrentThreadId());
  DCHECK_GE(i, 0);
  DCHECK_LT(i, static_cast<int>(rpcs_.size()));
  DCHECK(rpcs_[i] != nullptr);
  delete rpcs_[i];
  rpcs_[i] = nullptr;
  for (const auto* rpc : rpcs_) {
    if (rpc != nullptr)
      return false;
  }
  rpcs_.clear();
  return true;
}

void CompileService::MultiRpcController::SendReply() {
  CHECK_EQ(caller_thread_id_, wm_->GetCurrentThreadId());
  CHECK(http_server_request_ != nullptr);
  CHECK(rpcs_.empty());

  size_t gcc_resp_size = resp_->ByteSize();
  std::ostringstream http_response_message;
  http_response_message
    << "HTTP/1.1 200 OK\r\n"
    << "Content-Type: binary/x-protocol-buffer\r\n"
    << "Content-Length: " << gcc_resp_size << "\r\n\r\n";
  string response_string = http_response_message.str();
  int header_size = response_string.size();
  response_string.resize(header_size + gcc_resp_size);
  resp_->SerializeToArray(&response_string[header_size], gcc_resp_size);
  http_server_request_->SendReply(response_string);
  http_server_request_ = nullptr;
}

void CompileService::MultiRpcController::NotifyWhenClosed(
    OneshotClosure* callback) {
  // This might be called on the different thread than caller_thread_id_.
  {
    AUTOLOCK(lock, &mu_);
    if (closed_callback_ != nullptr) {
      closed_callbacks_.emplace_back(wm_->GetCurrentThreadId(), callback);
      return;
    }
  }
  // closed_callback_ has been called, that is, http_server_request_
  // was already closed, so runs callback now on the same thread.
  wm_->RunClosureInThread(
      FROM_HERE,
      wm_->GetCurrentThreadId(), callback,
      WorkerThreadManager::PRIORITY_IMMEDIATE);
}

void CompileService::MultiRpcController::RequestClosed() {
  std::vector<std::pair<WorkerThreadManager::ThreadId,
                        OneshotClosure*>> callbacks;
  {
    AUTOLOCK(lock, &mu_);
    closed_callback_ = nullptr;
    callbacks.swap(closed_callbacks_);
  }
  for (const auto& callback : callbacks) {
    wm_->RunClosureInThread(FROM_HERE, callback.first,
                            callback.second,
                            WorkerThreadManager::PRIORITY_IMMEDIATE);
  }
}
#endif

CompileService::CompileService(WorkerThreadManager* wm)
    : wm_(wm),
      quit_(false),
      task_id_(0),
      max_active_tasks_(1000),
      max_finished_tasks_(1000),
      max_failed_tasks_(1000),
      max_long_tasks_(50),
      username_(GetUsername()),
      nodename_(GetNodename()),
      start_time_(time(nullptr)),
      compiler_info_builder_(new CompilerInfoBuilder),
      compiler_info_pool_(wm_->StartPool(1, "compiler_info")),
      file_hash_cache_(new FileHashCache),
      include_processor_pool_(WorkerThreadManager::kFreePool),
      histogram_(new CompilerProxyHistogram),
      need_to_send_content_(false),
      new_file_threshold_(60),
      enable_gch_hack_(true),
      use_relative_paths_in_argv_(false),
      hermetic_(false),
      hermetic_fallback_(false),
      dont_kill_subprocess_(false),
      max_subprocs_pending_(0),
      local_run_preference_(0),
      local_run_for_failed_input_(false),
      local_run_delay_msec_(0),
      store_local_run_output_(false),
      enable_remote_link_(false),
      num_exec_request_(0),
      num_exec_success_(0),
      num_exec_failure_(0),
      num_exec_compiler_proxy_failure_(0),
      num_exec_goma_finished_(0),
      num_exec_goma_cache_hit_(0),
      num_exec_goma_local_cache_hit_(0),
      num_exec_goma_aborted_(0),
      num_exec_goma_retry_(0),
      num_exec_local_run_(0),
      num_exec_local_killed_(0),
      num_exec_local_finished_(0),
      num_exec_fail_fallback_(0),
      num_file_requested_(0),
      num_file_uploaded_(0),
      num_file_missed_(0),
      num_file_output_(0),
      num_file_rename_output_(0),
      num_file_output_buf_(0),
      num_include_processor_total_files_(0),
      num_include_processor_skipped_files_(0),
      include_processor_total_wait_time_(0),
      include_processor_total_run_time_(0),
      cur_sum_output_size_(0),
      max_sum_output_size_(0),
      req_sum_output_size_(0),
      peak_req_sum_output_size_(0),
      can_send_user_info_(false),
      allowed_network_error_duration_in_sec_(-1),
      num_active_fail_fallback_tasks_(0),
      max_active_fail_fallback_tasks_(-1),
      allowed_max_active_fail_fallback_duration_in_sec_(-1),
      reached_max_active_fail_fallback_time_(0),
      num_forced_fallback_in_setup_{},
      max_compiler_disabled_tasks_(-1) {
  if (username_.empty() || username_ == "unknown") {
    LOG(WARNING) << "Failed to obtain username:" << username_;
  }
  tmp_dir_ = GetGomaTmpDir();
}

CompileService::~CompileService() {
  ClearTasksUnlocked();
}

void CompileService::SetActiveTaskThrottle(int max_active_tasks) {
  AUTOLOCK(lock, &mu_);
  max_active_tasks_ = max_active_tasks;
}

void CompileService::SetCompileTaskHistorySize(
    int max_finished_tasks, int max_failed_tasks, int max_long_tasks) {
  AUTOLOCK(lock, &mu_);
  max_finished_tasks_ = max_finished_tasks;
  max_failed_tasks_ = max_failed_tasks;
  max_long_tasks_ = max_long_tasks;
}

void CompileService::SetCompilerProxyIdPrefix(const string& prefix) {
  AUTOLOCK(lock, &mu_);
  if (!compiler_proxy_id_prefix_.empty()) {
    LOG_IF(WARNING, compiler_proxy_id_prefix_ != prefix)
        << "different compiler_proxy_id_prefix:"
        << compiler_proxy_id_prefix_
        << " " << prefix;
    return;
  }
  compiler_proxy_id_prefix_ = prefix;
  LOG(INFO) << "compiler_proxy_id_prefix:" << compiler_proxy_id_prefix_;
}

void CompileService::SetSubProcessOptionSetter(
    std::unique_ptr<SubProcessOptionSetter> option_setter) {
  subprocess_option_setter_ = std::move(option_setter);
}

void CompileService::SetHttpClient(std::unique_ptr<HttpClient> http_client) {
  http_client_ = std::move(http_client);
}

void CompileService::SetHttpRPC(std::unique_ptr<HttpRPC> http_rpc) {
  http_rpc_ = std::move(http_rpc);
}

void CompileService::SetExecServiceClient(
    std::unique_ptr<ExecServiceClient> exec_service_client) {
  exec_service_client_ = std::move(exec_service_client);
}

void CompileService::SetMultiFileStore(
    std::unique_ptr<MultiFileStore> multi_file_store) {
  multi_file_store_ = std::move(multi_file_store);
}

void CompileService::SetFileServiceHttpClient(
    std::unique_ptr<FileServiceHttpClient> file_service) {
  file_service_ = std::move(file_service);
}

void CompileService::StartIncludeProcessorWorkers(int num_threads) {
  if (num_threads <= 0) {
    return;
  }
  include_processor_pool_ = wm_->StartPool(num_threads, "include_processor");
  LOG(INFO) << "include_processor_pool=" << include_processor_pool_
            << " num_thread=" << num_threads;
}

void CompileService::SetLogServiceClient(
    std::unique_ptr<LogServiceClient> log_service_client) {
  log_service_client_ = std::move(log_service_client);
}

void CompileService::SetAutoUpdater(std::unique_ptr<AutoUpdater> auto_updater) {
  auto_updater_ = std::move(auto_updater);
}

void CompileService::SetWatchdog(std::unique_ptr<Watchdog> watchdog,
                                 const std::vector<string>& goma_ipc_env) {
  watchdog_ = std::move(watchdog);
  watchdog_->SetTarget(this, goma_ipc_env);
}

void CompileService::SetTimeoutSecs(const std::vector<int>& timeout_secs) {
  copy(timeout_secs.begin(), timeout_secs.end(), back_inserter(timeout_secs_));
}

void CompileService::Exec(
    RpcController* rpc,
    const ExecReq* req, ExecResp* resp,
    OneshotClosure* done) {
  CompileTask* task = nullptr;
  // done will be called on this thread when Exec done.
  OneshotClosure* callback = NewCallback(this, &CompileService::ExecDone,
                                         wm_->GetCurrentThreadId(), done);
  {
    int task_id = 0;
    {
      AUTOLOCK(lock, &task_id_mu_);
      task_id = task_id_++;
    }

    task = new CompileTask(this, task_id);
    task->mutable_stats()->gcc_req_size = rpc->gcc_req_size_;
    rpc->gcc_resp_size_ = &task->mutable_stats()->gcc_resp_size;
    task->Init(rpc, req, resp, callback);

    AUTOLOCK(lock, &mu_);
    if (static_cast<int>(active_tasks_.size()) >= max_active_tasks_) {
      LOG(INFO) << task->trace_id() << " pending";
      pending_tasks_.push_back(task);
      return;
    }
    active_tasks_.insert(task);
    ++num_exec_request_;
  }
  // Starts handling RPC requests.
  // When response to gomacc is ready, ExecDone will be called on tasks' thread
  // and done callback will be called on this thread.
  // When all activities of task is finished, CompileTaskDone will be called
  // on task's thread.
  wm_->RunClosure(
      FROM_HERE,
      NewCallback(task, &CompileTask::Start),
      WorkerThreadManager::PRIORITY_LOW);
}

void CompileService::ExecDone(WorkerThreadManager::ThreadId thread_id,
                              OneshotClosure* done) {
  wm_->RunClosureInThread(
      FROM_HERE,
      thread_id,
      NewCallback(static_cast<Closure*>(done), &Closure::Run),
      WorkerThreadManager::PRIORITY_HIGH);
}

void CompileService::CompileTaskDone(CompileTask* task) {
  task->SetFrozenTimestampMs(GetCurrentTimestampMs());
  histogram_->UpdateCompileStat(task->stats());
  if (log_service_client_.get())
    log_service_client_->SaveExecLog(task->stats());

  std::vector<CompileTask*> start_tasks;
  std::vector<CompileTask*> deref_tasks;
  {
    AUTOLOCK(lock, &mu_);

    active_tasks_.erase(task);
    int num_start_tasks =
        max_active_tasks_ - static_cast<int>(active_tasks_.size());
    if (!pending_tasks_.empty()) {
      LOG(INFO) << "Run at most " << num_start_tasks << " pending_tasks "
                << "(active=" << active_tasks_.size()
                << " max=" << max_active_tasks_
                << " pending=" << pending_tasks_.size() << ")";
    }
    for (int i = 0; i < num_start_tasks && !pending_tasks_.empty(); ++i) {
      CompileTask* start_task = pending_tasks_.front();
      pending_tasks_.pop_front();
      active_tasks_.insert(start_task);
      start_tasks.push_back(start_task);
      ++num_exec_request_;
    }
    finished_tasks_.push_front(task);
    if (static_cast<int>(finished_tasks_.size()) > max_finished_tasks_) {
      deref_tasks.push_back(finished_tasks_.back());
      finished_tasks_.pop_back();
    }
    num_include_processor_total_files_ +=
        task->stats().include_preprocess_total_files();
    num_include_processor_skipped_files_ +=
        task->stats().include_preprocess_skipped_files();
    include_processor_total_wait_time_ +=
        task->stats().include_processor_wait_time();
    include_processor_total_run_time_ +=
        task->stats().include_processor_run_time();

    switch (task->state()) {
      case CompileTask::FINISHED:
        ++num_exec_goma_finished_;
        if (task->local_cache_hit()) {
          ++num_exec_goma_local_cache_hit_;
        } else if (task->cache_hit()) {
          ++num_exec_goma_cache_hit_;
        }
        break;
      case CompileTask::LOCAL_FINISHED:
        ++num_exec_local_finished_;
        break;
      default:
        CHECK(task->abort());
        ++num_exec_goma_aborted_;
        break;
    }
    num_exec_goma_retry_ += task->stats().exec_request_retry();

    num_file_requested_ += task->stats().num_total_input_file();
    num_file_uploaded_ +=
        SumRepeatedInt32(task->stats().num_uploading_input_file());
    num_file_missed_ +=
        SumRepeatedInt32(task->stats().num_missing_input_file());

    if (task->local_run()) {
      ++num_exec_local_run_;
      ++local_run_reason_[task->stats().local_run_reason()];
    }
    if (task->local_killed()) {
      ++num_exec_local_killed_;
    }
    if ((task->failed() || task->fail_fallback()) && !task->canceled()) {
      if (task->failed())
        ++num_exec_failure_;
      if (task->fail_fallback()) {
        ++num_exec_fail_fallback_;
        --num_active_fail_fallback_tasks_;
        DCHECK_GE(num_active_fail_fallback_tasks_, 0);
        if (num_active_fail_fallback_tasks_ <=
            max_active_fail_fallback_tasks_) {
          LOG_IF(INFO, reached_max_active_fail_fallback_time_ != 0)
              << "clearing reached_max_active_fail_fallback_time.";
          reached_max_active_fail_fallback_time_ = 0;
        }
      }
      if (task->stats().compiler_proxy_error())
        ++num_exec_compiler_proxy_failure_;
      task->Ref();
      failed_tasks_.push_front(task);
      if (static_cast<int>(failed_tasks_.size()) > max_failed_tasks_) {
        deref_tasks.push_back(failed_tasks_.back());
        failed_tasks_.pop_back();
      }
    } else {
      ++num_exec_success_;
    }

    bool is_longest = false;
    if (static_cast<int>(long_tasks_.size()) < max_long_tasks_) {
      task->Ref();
      long_tasks_.push_back(task);
      is_longest = true;
    } else if (task->stats().handler_time() >
               long_tasks_[0]->stats().handler_time()) {
      pop_heap(long_tasks_.begin(), long_tasks_.end(),
               CompareTaskHandlerTime());
      deref_tasks.push_back(long_tasks_.back());
      task->Ref();
      long_tasks_.back() = task;
      is_longest = true;
    }
    if (is_longest) {
      // Create heap on long_tasks_.
      // long_tasks_[0] should have shortest handler time in longest_tasks_.
      push_heap(long_tasks_.begin(), long_tasks_.end(),
                CompareTaskHandlerTime());
    }

    cond_.Signal();
  }
  for (auto* start_task : start_tasks) {
    wm_->RunClosure(
        FROM_HERE,
        NewCallback(start_task, &CompileTask::Start),
        WorkerThreadManager::PRIORITY_LOW);
  }
  for (auto* deref_task : deref_tasks) {
    deref_task->Deref();
  }
}

void CompileService::Quit() {
  {
    AUTOLOCK(lock, &quit_mu_);
    quit_ = true;
  }
  if (auto_updater_) {
    auto_updater_->Stop();
  }
  if (log_service_client_.get())
    log_service_client_->Flush();
#ifndef GLOG_NO_ABBREVIATED_SEVERITIES
  google::FlushLogFiles(google::INFO);
#else
  google::FlushLogFiles(google::GLOG_INFO);
#endif
}

bool CompileService::quit() const {
  AUTOLOCK(lock, &quit_mu_);
  return quit_;
}

void CompileService::Wait() {
  // Sends logs before shutting down http rpc.
  if (log_service_client_.get())
    log_service_client_->Flush();

  if (auto_updater_) {
    auto_updater_->Wait();
  }
  http_client_->Shutdown();
  wm_->Shutdown();
  {
    AUTOLOCK(lock, &mu_);
    LOG(INFO) << "Waiting all active tasks finished....";
    while (!pending_tasks_.empty() || !active_tasks_.empty()) {
      LOG(INFO) << "pending_tasks=" << pending_tasks_.size()
                << "active_tasks=" << active_tasks_.size();
      cond_.Wait(&mu_);
    }
  }
  CHECK(active_tasks_.empty());
  if (log_service_client_.get())
    log_service_client_->Wait();
  log_service_client_.reset();
  histogram_.reset();
  file_hash_cache_.reset();
  if (multi_file_store_.get())
    multi_file_store_->Wait();
  file_service_.reset();
  exec_service_client_.reset();

  // Stop all HttpClient tasks before resetting http_rpc_ b/26551623
  http_client_->WaitNoActive();
  http_rpc_.reset();
  http_client_.reset();
  watchdog_.reset();
}

bool CompileService::DumpTask(int task_id, string* out) {
  AUTOLOCK(lock, &mu_);
  const CompileTask* task = FindTaskByIdUnlocked(task_id, true);
  if (task == nullptr)
    return false;
  Json::Value json;
  task->DumpToJson(true, &json);
  *out = json.toStyledString();
  return true;
}

bool CompileService::DumpTaskRequest(int task_id) {
  const CompileTask* task = nullptr;
  {
    AUTOLOCK(lock, &mu_);
    task = FindTaskByIdUnlocked(task_id, false);
    if (task == nullptr)
      return false;
    const_cast<CompileTask*>(task)->Ref();
  }
  task->DumpRequest();
  {
    AUTOLOCK(lock, &mu_);
    const_cast<CompileTask*>(task)->Deref();
  }
  return true;
}

void CompileService::DumpToJson(Json::Value* json, long long after) {
  AUTOLOCK(lock, &mu_);

  long long last_update_ms = after;

  {
    Json::Value active(Json::arrayValue);
    for (const auto* task : active_tasks_) {
      Json::Value json_task;
      task->DumpToJson(false, &json_task);
      active.append(std::move(json_task));
    }
    (*json)["active"] = std::move(active);
  }

  {
    Json::Value finished(Json::arrayValue);
    for (const auto* task : finished_tasks_) {
      Json::Value json_task;
      task->DumpToJson(false, &json_task);
      finished.append(std::move(json_task));
    }
    (*json)["finished"] = std::move(finished);
  }

  {
    Json::Value failed(Json::arrayValue);
    for (const auto* task : failed_tasks_) {
      if (task->GetFrozenTimestampMs() <= after)
        continue;
      last_update_ms = std::max(last_update_ms, task->GetFrozenTimestampMs());
      Json::Value json_task;
      task->DumpToJson(false, &json_task);
      failed.append(std::move(json_task));
    }
    (*json)["failed"] = std::move(failed);
  }

  {
    Json::Value long_json(Json::arrayValue);
    std::vector<CompileTask*> long_tasks(long_tasks_);
    sort(long_tasks.begin(), long_tasks.end(), CompareTaskHandlerTime());
    for (const auto* task : long_tasks) {
      Json::Value json_task;
      task->DumpToJson(false, &json_task);
      long_json.append(std::move(json_task));
    }
    (*json)["long"] = std::move(long_json);
  }

  {
    Json::Value num_exec(Json::objectValue);

    num_exec["max_active_tasks"] = max_active_tasks_;
    num_exec["pending"] = Json::Int64(pending_tasks_.size());
    num_exec["request"] = num_exec_request_;
    num_exec["success"] = num_exec_success_;
    num_exec["failure"] = num_exec_failure_;
    num_exec["compiler_proxy_fail"] = num_exec_compiler_proxy_failure_;
    num_exec["compiler_info_stores"] =
        CompilerInfoCache::instance()->NumStores();
    num_exec["compiler_info_store_dups"] =
        CompilerInfoCache::instance()->NumStoreDups();
    num_exec["compiler_info_fail"] = CompilerInfoCache::instance()->NumFail();
    num_exec["compiler_info_miss"] = CompilerInfoCache::instance()->NumMiss();
    num_exec["goma_finished"] = num_exec_goma_finished_;
    num_exec["goma_cache_hit"] = num_exec_goma_cache_hit_;
    num_exec["goma_aborted"] = num_exec_goma_aborted_;
    num_exec["goma_retry"] = num_exec_goma_retry_;
    num_exec["local_run"] = num_exec_local_run_;
    num_exec["local_killed"] = num_exec_local_killed_;
    num_exec["local_finished"] = num_exec_local_finished_;
    num_exec["fail_fallback"] = num_exec_fail_fallback_;

    Json::Value version_mismatch(Json::objectValue);
    for (const auto& iter : command_version_mismatch_) {
      version_mismatch[iter.first] = iter.second;
    }
    num_exec["version_mismatch"] = std::move(version_mismatch);

    Json::Value binary_hash_mismatch(Json::objectValue);
    for (const auto& iter : command_binary_hash_mismatch_) {
      binary_hash_mismatch[iter.first] = iter.second;
    }
    num_exec["binary_hash_mismatch"] = std::move(binary_hash_mismatch);

    (*json)["num_exec"] = std::move(num_exec);
  }

  {
    Json::Value num_file;
    num_file["requested"] = num_file_requested_;
    num_file["uploaded"] = num_file_uploaded_;
    num_file["missed"] = num_file_missed_;
    (*json)["num_file"] = std::move(num_file);
  }

  {
    Json::Value http_rpc;
    http_rpc_->DumpToJson(&http_rpc);
    (*json)["http_rpc"] = std::move(http_rpc);
  }

  if (auto_updater_.get()) {
    int version = auto_updater_->my_version();
    if (version > 0) {
      Json::Value goma_version(Json::arrayValue);
      goma_version.append(version);
      goma_version.append(auto_updater_->pulled_version());

      (*json)["goma_version"] = std::move(goma_version);
    }
  }
  (*json)["last_update_ms"] = last_update_ms;
}

void CompileService::DumpStats(std::ostringstream* ss) {
  GomaStats gstats;
  std::ostringstream error_ss;
  std::ostringstream localrun_ss;
  std::ostringstream mismatches_ss;
  {
    AUTOLOCK(lock, &mu_);
    DumpCommonStatsUnlocked(&gstats);
    // Note that followings are not included in GomaStats.
    // GomaStats is used for storing statistics data for buildbot monitoring.
    // We are suggested by c-i-t monitoring folks not to store string data to
    // reduce concerns by privacy reviewers. The reviewers may think that
    // string fields can be used to send arbitrary privacy information.
    if (!error_to_user_.empty() || !error_to_log_.empty()) {
      error_ss << "error:" << std::endl;
      if (!error_to_user_.empty()) {
        error_ss << " user:" << std::endl;
      }
      for (const auto& it : error_to_user_) {
        error_ss << "  E:"
                 << it.second << " "  // count
                 << it.first << std::endl;  // message
      }
      if (!error_to_log_.empty()) {
        error_ss << " log:"
                 << " E=" << gstats.error_stats().log_error()
                 << " W=" << gstats.error_stats().log_warning()
                 << std::endl;
      }
    }
    if (!local_run_reason_.empty()) {
      localrun_ss << " local run reason:" << std::endl;
      for (const auto& it : local_run_reason_) {
        localrun_ss << "  " << it.first << "=" << it.second << std::endl;
      }
    }
    if (!command_version_mismatch_.empty()) {
      mismatches_ss << "version_mismatch:" << std::endl;
      for (const auto& it : command_version_mismatch_) {
        mismatches_ss << " " << it.first << " " << it.second << std::endl;
      }
    }
    if (!command_binary_hash_mismatch_.empty()) {
      mismatches_ss << "binary_hash_mismatch:" << std::endl;
      for (const auto& it : command_binary_hash_mismatch_) {
        mismatches_ss << " " << it.first << " " << it.second << std::endl;
      }
    }
    if (!subprogram_mismatch_.empty()) {
      mismatches_ss << "subprogram_mismatch:" << std::endl;
      for (const auto& it : subprogram_mismatch_) {
        mismatches_ss << " " << it.first << " " << it.second << std::endl;
      }
    }
  }

  (*ss) << "request:"
        << " total=" << gstats.request_stats().total()
        << " success=" << gstats.request_stats().success()
        << " failure=" << gstats.request_stats().failure()
        << std::endl;
  (*ss) << " compiler_proxy:"
        << " fail=" << gstats.request_stats().compiler_proxy().fail()
        << std::endl;
  (*ss) << " compiler_info:"
        << " stores=" << gstats.request_stats().compiler_info().stores()
        << " store_dups=" << gstats.request_stats().compiler_info().store_dups()
        << " miss=" << gstats.request_stats().compiler_info().miss()
        << " fail=" << gstats.request_stats().compiler_info().fail()
        << std::endl;
  (*ss) << " goma:"
        << " finished=" << gstats.request_stats().goma().finished()
        << " cache_hit=" << gstats.request_stats().goma().cache_hit()
        << " local_cachehit=" << gstats.request_stats().goma().local_cache_hit()
        << " aborted=" << gstats.request_stats().goma().aborted()
        << " retry=" << gstats.request_stats().goma().retry()
        << " fail=" << gstats.request_stats().goma().fail()
        << std::endl;
  const FallbackInSetupStats& fallback_in_setup =
      gstats.request_stats().fallback_in_setup();
  (*ss) << " fallback_in_setup:" << std::endl
        << "  parse_fail=" << fallback_in_setup.failed_to_parse_flags()
        << " no_remote=" << fallback_in_setup.no_remote_compile_supported()
        << " http_disabled=" << fallback_in_setup.http_disabled()
        << std::endl
        << "  compiler_info_fail="
        << fallback_in_setup.fail_to_get_compiler_info()
        << " compiler_disabled=" << fallback_in_setup.compiler_disabled()
        << " requested_by_user=" << fallback_in_setup.requested_by_user()
        << std::endl;
  (*ss) << " local:"
        << " run=" << gstats.request_stats().local().run()
        << " killed=" << gstats.request_stats().local().killed()
        << " finished=" << gstats.request_stats().local().finished()
        << std::endl;
  (*ss) << localrun_ss.str();
  (*ss) << mismatches_ss.str();
  (*ss) << error_ss.str();
  (*ss) << "files:"
        << " requested=" << gstats.file_stats().requested()
        << " uploaded=" << gstats.file_stats().uploaded()
        << " missed=" << gstats.file_stats().missed()
        << std::endl;
  (*ss) << "outputs:"
        << " files=" << gstats.output_stats().files()
        << " rename=" << gstats.output_stats().rename()
        << " buf=" << gstats.output_stats().buf()
        << " peak_req=" << gstats.output_stats().peak_req()
        << std::endl;
  (*ss) << "memory:"
        << " consuming=" << gstats.memory_stats().consuming()
        << std::endl;
  (*ss) << "time:"
        << " uptime=" << gstats.time_stats().uptime()
        << std::endl;
  (*ss) << "include_processor:"
        << " total=" << gstats.include_processor_stats().total()
        << " skipped=" << gstats.include_processor_stats().skipped()
        << " total_wait_time="
        << gstats.include_processor_stats().total_wait_time()
        << " total_run_time="
        << gstats.include_processor_stats().total_run_time()
        << std::endl;
  if (gstats.has_includecache_stats()) {
    const IncludeCacheStats& ic_stats = gstats.includecache_stats();
    int original_ave = 0;
    int filtered_ave = 0;
    if (ic_stats.total_entries() > 0) {
      original_ave = ic_stats.original_total_size() / ic_stats.total_entries();
      filtered_ave = ic_stats.filtered_total_size() / ic_stats.total_entries();
    }

    (*ss) << "includecache:" << std::endl;
    (*ss) << "  entries=" << ic_stats.total_entries()
          << " cache_size=" << ic_stats.total_cache_size()
          << " hit=" << ic_stats.hit()
          << " missed=" << ic_stats.missed()
          << " updated=" << ic_stats.updated()
          << " evicted=" << ic_stats.evicted() << std::endl;
    (*ss) << "  orig_total=" << ic_stats.original_total_size()
          << " orig_max=" << ic_stats.original_max_size()
          << " orig_ave=" << original_ave
          << " filter_total=" << ic_stats.filtered_total_size()
          << " filter_max=" << ic_stats.filtered_max_size()
          << " filter_ave=" << filtered_ave
          << std::endl;
  }
  if (gstats.has_depscache_stats()) {
    const DepsCacheStats& dc_stats = gstats.depscache_stats();
    (*ss) << "depscache:"
          << " table_size=" << dc_stats.deps_table_size()
          << " max=" << dc_stats.max_entries()
          << " total=" << dc_stats.total_entries();
    size_t average_entries = 0;
    if (dc_stats.deps_table_size() > 0) {
      average_entries = dc_stats.total_entries() / dc_stats.deps_table_size();
    }
    (*ss) << " average=" << average_entries;
    (*ss) << " idtable=" << dc_stats.idtable_size()
          << " hit=" << dc_stats.hit()
          << " updated=" << dc_stats.updated()
          << " missed=" << dc_stats.missed()
          << std::endl;
  }
  if (gstats.has_local_output_cache_stats()) {
    const LocalOutputCacheStats& loc_stats = gstats.local_output_cache_stats();
    (*ss) << "localoutputcache:"
          << std::endl
          << " save_success=" << loc_stats.save_success()
          << " save_success_time_ms=" << loc_stats.save_success_time_ms()
          << " save_failure=" << loc_stats.save_failure()
          << std::endl
          << " lookup_success=" << loc_stats.lookup_success()
          << " lookup_success_time_ms=" << loc_stats.lookup_success_time_ms()
          << " lookup_miss=" << loc_stats.lookup_miss()
          << " lookup_failure=" << loc_stats.lookup_failure()
          << std::endl
          << " commit_success=" << loc_stats.commit_success()
          << " commit_success_time_ms=" << loc_stats.commit_success_time_ms()
          << " commit_failure=" << loc_stats.commit_failure()
          << std::endl
          << " gc_count=" << loc_stats.gc_count()
          << " gc_total_time_ms=" << loc_stats.gc_total_time_ms()
          << std::endl;
    // TODO: Merge these to stats.
    if (LocalOutputCache::IsEnabled()) {
      (*ss) << " gc_removed_items="
            << LocalOutputCache::instance()->TotalGCRemovedItems()
            << " gc_removed_bytes="
            << LocalOutputCache::instance()->TotalGCRemovedBytes()
            << std::endl
            << " total_cache_count="
            << LocalOutputCache::instance()->TotalCacheCount()
            << " total_cache_bytes="
            << LocalOutputCache::instance()->TotalCacheAmountInByte()
            << std::endl;
    }
  }

  (*ss) << "http_rpc:"
        << " query=" << gstats.http_rpc_stats().query()
        << " retry=" << gstats.http_rpc_stats().retry()
        << " timeout=" << gstats.http_rpc_stats().timeout()
        << " error=" << gstats.http_rpc_stats().error()
        << std::endl;

  if (gstats.has_subprocess_stats()) {
    (*ss) << "burst_mode:"
          << " by_network="
          << gstats.subprocess_stats().count_burst_by_network_error()
          << " by_compiler_disabled="
          << gstats.subprocess_stats().count_burst_by_compiler_disabled()
          << std::endl;
  }
}

void CompileService::DumpStatsJson(
    std::string* json_string,
    CompileService::HumanReadability human_readable) {
  GomaStatzStats statz;
  {
    AUTOLOCK(lock, &mu_);
    DumpCommonStatsUnlocked(statz.mutable_stats());

    if (!error_to_user_.empty()) {
      *statz.mutable_error_to_user() = google::protobuf::Map<string, int64_t>(
          error_to_user_.begin(), error_to_user_.end());
    }
    if (!local_run_reason_.empty()) {
      *statz.mutable_local_run_reason() =
          google::protobuf::Map<string, int64_t>(
              local_run_reason_.begin(),
              local_run_reason_.end());
    }
    if (!command_version_mismatch_.empty()) {
      *statz.mutable_version_mismatch() =
          google::protobuf::Map<string, int64_t>(
              command_version_mismatch_.begin(),
              command_version_mismatch_.end());
    }
    if (!command_binary_hash_mismatch_.empty()) {
      *statz.mutable_subprogram_mismatch() =
          google::protobuf::Map<string, int64_t>(
              command_binary_hash_mismatch_.begin(),
              command_binary_hash_mismatch_.end());
    }
    if (!subprogram_mismatch_.empty()) {
      *statz.mutable_subprogram_mismatch() =
          google::protobuf::Map<string, int64_t>(subprogram_mismatch_.begin(),
                                                 subprogram_mismatch_.end());
    }
  }

  // Then, convert statz to json string.
  google::protobuf::util::JsonPrintOptions options;
  // This is necessary, otherwise field whose value is 0 won't be printed.
  options.always_print_primitive_fields = true;
  if (human_readable == HumanReadability::kHumanReadable) {
    options.add_whitespace = true;
  }
  google::protobuf::util::Status status =
      google::protobuf::util::MessageToJsonString(statz, json_string, options);
  if (!status.ok()) {
    LOG(ERROR) << "failed to convert GomaStatzStats to json"
               << " error_code=" << status.error_code()
               << " error_message=" << status.error_message();
    json_string->clear();
  }
}

void CompileService::ClearTasks() {
  AUTOLOCK(lock, &mu_);
  ClearTasksUnlocked();
}

void CompileService::ClearTasksUnlocked() {
  LOG(INFO) << "active tasks:" << active_tasks_.size();
  for (auto* task : active_tasks_) {
    task->Deref();
  }
  active_tasks_.clear();
  LOG(INFO) << "finished_tasks: " << finished_tasks_.size()
            << ", failed_tasks: " << failed_tasks_.size()
            << ", long_tasks: " << long_tasks_.size();
  for (auto* task : finished_tasks_) {
    task->Deref();
  }
  finished_tasks_.clear();
  for (auto* task : failed_tasks_) {
    task->Deref();
  }
  failed_tasks_.clear();

  for (auto* task : long_tasks_) {
    task->Deref();
  }
  long_tasks_.clear();
}

void CompileService::DumpCompilerInfo(std::ostringstream* ss) {
  if (hermetic_) {
    (*ss) << "hermetic mode\n";
    if (hermetic_fallback_) {
      (*ss) << " local fallback if same compiler doesn't exist on server\n";
    } else {
      (*ss) << " error if same compiler doesn't exist on server\n";
    }
  } else {
    (*ss) << "non-hermetic mode\n";
  }
  (*ss) << "\n";

  compiler_info_builder_->Dump(ss);

  CompilerInfoCache::instance()->Dump(ss);

  {
    AUTO_SHARED_LOCK(lock, &compiler_mu_);

    (*ss) << "local compiler path:" << local_compiler_paths_.size() << "\n";
    (*ss) << "\n[local compiler path]\n\n";
    for (const auto& entry : local_compiler_paths_) {
      (*ss) << "key: " << entry.first << "\n"
            << "local_compiler:" << entry.second.first << "\n"
            << "local_path:" << entry.second.second << "\n\n";
    }
  }
}

bool CompileService::FindLocalCompilerPath(
    const string& gomacc_path,
    const string& basename_orig,
    const string& cwd,
    const string& local_path,
    const string& pathext,
    string* local_compiler_path,
    string* no_goma_local_path) {
  // If all PATH components are absolute paths, local compiler path doesn't
  // depend on cwd.  In this case, we'll use "." in cwd field for key.
  // Otherwise, use key_cwd.
  string basename = basename_orig;
  string key(gomacc_path + kSep + basename + kCurrentDir + local_path);
  const string key_cwd(
      gomacc_path + kSep + basename + kSep + cwd + kSep + local_path);

  VLOG(1) << "find local compiler: key=" << key << " or " << key_cwd;

  {
    AUTO_SHARED_LOCK(lock, &compiler_mu_);
    if (FindLocalCompilerPathUnlocked(
            key, key_cwd, local_compiler_path, no_goma_local_path)) {
      return true;
    }
  }
  return FindLocalCompilerPathAndUpdate(
      key, key_cwd, gomacc_path, basename, cwd, local_path, pathext,
      local_compiler_path, no_goma_local_path);
}

bool CompileService::FindLocalCompilerPathUnlocked(
    const string& key,
    const string& key_cwd,
    string* local_compiler_path,
    string* no_goma_local_path) const {
  // assert compiler_mu held either exclusive or shared.
  auto found = local_compiler_paths_.find(key);
  if (found != local_compiler_paths_.end()) {
    *local_compiler_path = found->second.first;
    *no_goma_local_path = found->second.second;
    return true;
  }
  found = local_compiler_paths_.find(key_cwd);
  if (found != local_compiler_paths_.end()) {
    *local_compiler_path = found->second.first;
    *no_goma_local_path = found->second.second;
    return true;
  }
  return false;
}

bool CompileService::FindLocalCompilerPathAndUpdate(
    const string& key,
    const string& key_cwd,
    const string& gomacc_path,
    const string& basename,
    const string& cwd,
    const string& local_path,
    const string& pathext,
    string* local_compiler_path,
    string* no_goma_local_path) {
  {
    AUTO_SHARED_LOCK(lock, &compiler_mu_);
    if (FindLocalCompilerPathUnlocked(
            key, key_cwd,
            local_compiler_path, no_goma_local_path)) {
      return true;
    }
  }

  AUTO_EXCLUSIVE_LOCK(lock, &compiler_mu_);
  if (FindLocalCompilerPathUnlocked(
          key, key_cwd,
          local_compiler_path, no_goma_local_path)) {
    return true;
  }

  string local_compiler_key = key;

  if (!local_compiler_path->empty()) {
    if (!IsGomacc(*local_compiler_path, local_path, pathext, cwd)) {
      // Convert to an absolute path if the path is a relative path.
      string orig_local_compiler_path = *local_compiler_path;
#ifndef _WIN32
      local_compiler_path->assign(
          PathResolver::ResolvePath(
              file::JoinPathRespectAbsolute(cwd, orig_local_compiler_path)));
#else
      local_compiler_path->assign(
          PathResolver::ResolvePath(
              ResolveExtension(orig_local_compiler_path, pathext, cwd)));
#endif
      if (local_compiler_path->empty()) {
        LOG(ERROR) << "cannot find local_compiler:"
                   << " cwd=" << cwd
                   << " local_compiler=" << orig_local_compiler_path;
        return false;
      }
      *no_goma_local_path = local_path;
      if (*local_compiler_path != orig_local_compiler_path)
        local_compiler_key = key_cwd;
      local_compiler_paths_.insert(
          std::make_pair(local_compiler_key,
                         std::make_pair(*local_compiler_path,
                                        *no_goma_local_path)));
      return true;
    }
    LOG(ERROR) << "local_compiler is gomacc:" << *local_compiler_path;
  }

  FileId gomacc_fileid(gomacc_path);
  if (!gomacc_fileid.IsValid()) {
    PLOG(ERROR) << "stat gomacc_path:" << gomacc_path;
    return false;
  }

  bool is_relative;
  string no_goma_path_env;
  if (GetRealExecutablePath(&gomacc_fileid, basename, cwd,
                            local_path, pathext,
                            local_compiler_path, &no_goma_path_env,
                            &is_relative)) {
    if (is_relative)
      local_compiler_key = key_cwd;
    no_goma_local_path->assign(no_goma_path_env);
    local_compiler_paths_.insert(
        std::make_pair(local_compiler_key,
                       std::make_pair(*local_compiler_path,
                                      *no_goma_local_path)));
    return true;
  }
  LOG(WARNING) << basename << " not found in " << local_path;
  return false;
}

void CompileService::GetCompilerInfo(
    GetCompilerInfoParam* param,
    OneshotClosure* callback) {
  param->state.reset(CompilerInfoCache::instance()->Lookup(param->key));
  if (param->state.get() != nullptr) {
    param->cache_hit = true;
    param->state.get()->Use(param->key.local_compiler_path, *param->flags);
    callback->Run();
    return;
  }
  {
    AUTOLOCK(lock, &compiler_info_mu_);
    auto p = compiler_info_waiters_.insert(
      std::make_pair(
          param->key.ToString(
              CompilerInfoCache::Key::kCwdRelative),
          static_cast<CompilerInfoWaiterList*>(nullptr)));
    if (p.second) {
      // first call for the key.
      p.first->second = new CompilerInfoWaiterList;
      LOG(INFO) << param->trace_id << " call GetCompilerInfoInternal";
    } else {
      // another task already requested the same key.
      // callback will be called once the other task gets compiler info.
      p.first->second->emplace_back(param, callback);
      LOG(INFO) << param->trace_id << " wait GetCompilerInfoInternal"
                << " queue=" << p.first->second->size();
      return;
    }
  }
  wm_->RunClosureInPool(FROM_HERE,
                        compiler_info_pool_,
                        NewCallback(
                            this, &CompileService::GetCompilerInfoInternal,
                            param, callback),
                        WorkerThreadManager::PRIORITY_MED);
}

void CompileService::GetCompilerInfoInternal(
    GetCompilerInfoParam* param,
    OneshotClosure* callback) {
  param->state.reset(CompilerInfoCache::instance()->Lookup(param->key));
  if (param->state.get() == nullptr) {
    SimpleTimer timer;

    // Set invalid GOMA env flag to fail when local_compiler_path
    // is (masquraded) gomacc.
    // FillFromCompilerOutputs will run local_compiler_path.
    // If local_compiler_path is (masquerated) gomacc, it'll reenter
    // this routine and deadlock on mu_.  Invalid GOMA env flag
    // avoid this deadlock.
    std::vector<string> env(param->run_envs);
    env.push_back("GOMA_WILL_FAIL_WITH_UKNOWN_FLAG=true");
    std::unique_ptr<CompilerInfoData> cid(
        compiler_info_builder_->FillFromCompilerOutputs(
            *param->flags, param->key.local_compiler_path, env));

    param->state.reset(CompilerInfoCache::instance()->Store(
        param->key, std::move(cid)));
    param->updated = true;
    LOG(INFO) << param->trace_id
              << " FillFromCompilerOutputs"
              << " state=" << param->state.get()
              << " found=" << param->state.get()->info().found()
              << " in " << timer.GetInMilliSeconds() << "[ms]";
  }
  param->state.get()->Use(param->key.local_compiler_path, *param->flags);
  std::unique_ptr<CompilerInfoWaiterList> waiters;
  {
    AUTOLOCK(lock, &compiler_info_mu_);
    const string key_cwd = param->key.ToString(
        CompilerInfoCache::Key::kCwdRelative);
    auto p = compiler_info_waiters_.find(key_cwd);
    CHECK(p != compiler_info_waiters_.end())
        << param->trace_id << " state=" << param->state.get()
        << " key_cwd=" << key_cwd;
    waiters.reset(p->second);
    compiler_info_waiters_.erase(p);
  }
  // keep alive at least in this func.
  // param->state might be derefed so CompilerInfoState may be deleted.
  ScopedCompilerInfoState state(param->state.get());

  string trace_id = param->trace_id;

  wm_->RunClosureInThread(FROM_HERE,
                          param->thread_id,
                          callback,
                          WorkerThreadManager::PRIORITY_MED);
  // param may be invalidated here.
  CHECK(waiters.get() != nullptr) << trace_id << " state=" << state.get();
  LOG(INFO) << trace_id << " callback " << waiters->size() << " waiters";
  for (const auto& p : *waiters) {
    GetCompilerInfoParam* wparam = p.first;
    OneshotClosure* wcallback = p.second;
    wparam->state.reset(state.get());
    VLOG(1) << trace_id << " callback for " << wparam->trace_id;
    wparam->state.get()->Use(wparam->key.local_compiler_path, *wparam->flags);
    wm_->RunClosureInThread(FROM_HERE,
                            wparam->thread_id,
                            wcallback,
                            WorkerThreadManager::PRIORITY_MED);
  }
}

bool CompileService::DisableCompilerInfo(CompilerInfoState* state,
                                         const string& disabled_reason) {
  return CompilerInfoCache::instance()->Disable(state, disabled_reason);
}

bool CompileService::RecordCommandSpecVersionMismatch(
    const string& exec_command_version_mismatch) {
  AUTOLOCK(lock, &mu_);
  auto p = command_version_mismatch_.insert(
      std::make_pair(exec_command_version_mismatch, 0));
  p.first->second += 1;
  return p.second;
}

bool CompileService::RecordCommandSpecBinaryHashMismatch(
    const string& exec_command_binary_hash_mismatch) {
  AUTOLOCK(lock, &mu_);
  auto p = command_binary_hash_mismatch_.insert(
      std::make_pair(exec_command_binary_hash_mismatch, 0));
  p.first->second += 1;
  return p.second;
}

bool CompileService::RecordSubprogramMismatch(
    const string& subprogram_mismatch) {
  AUTOLOCK(lock, &mu_);
  auto p = subprogram_mismatch_.insert(std::make_pair(subprogram_mismatch, 0));
  p.first->second += 1;
  return p.second;
}

void CompileService::RecordErrorToLog(
    const string& error_message, bool is_error) {
  AUTOLOCK(lock, &mu_);
  auto p = error_to_log_.insert(
      std::make_pair(error_message, std::make_pair(is_error, 0)));
  p.first->second.second += 1;
  if (!p.second) {
    LOG_IF(ERROR, p.first->second.first != is_error)
        << error_message << " was is_error=" << p.first->second.first
        << " but is_error=" << is_error;
  }
}

void CompileService::RecordErrorsToUser(
    const std::vector<string>& error_messages) {
  AUTOLOCK(lock, &mu_);
  for (const auto& errmsg : error_messages) {
    auto p = error_to_user_.insert(std::make_pair(errmsg,  0));
    p.first->second += 1;
  }
}

void CompileService::RecordInputResult(
    const std::vector<string>& inputs, bool success) {
  AUTO_EXCLUSIVE_LOCK(lock, &failed_inputs_mu_);
  for (const auto& input : inputs) {
    if (success) {
      failed_inputs_.erase(input);
    } else {
      failed_inputs_.insert(input);
    }
  }
}

bool CompileService::ContainFailedInput(
    const std::vector<string>& inputs) const {
  AUTO_SHARED_LOCK(lock, &failed_inputs_mu_);
  for (const auto& input : inputs) {
    if (failed_inputs_.count(input)) {
      return true;
    }
  }
  return false;
}

bool CompileService::AcquireOutputBuffer(size_t filesize, string* buf) {
  DCHECK_EQ(0U, buf->size());

  bool success = false;

  {
    // Since buf->resize() or buf->clear() could be slow,
    // call it without holding a lock.

    AUTOLOCK(lock, &mu_);
    if (filesize > max_sum_output_size_ ||
        req_sum_output_size_ + filesize < req_sum_output_size_ ||
        cur_sum_output_size_ + filesize < cur_sum_output_size_) {
      LOG(ERROR) << "too large output buf size:" << filesize;
      success = false;
    } else {
      req_sum_output_size_ += filesize;
      if (req_sum_output_size_ > peak_req_sum_output_size_) {
        peak_req_sum_output_size_ = req_sum_output_size_;
      }

      if (cur_sum_output_size_ + filesize < max_sum_output_size_) {
        cur_sum_output_size_ += filesize;
        num_file_output_buf_++;
        success = true;
      } else {
        LOG(INFO) << "output buf size over:"
                  << " cur=" << cur_sum_output_size_
                  << " req=" << filesize
                  << " max=" << max_sum_output_size_;
        success = false;
      }
    }
  }

  if (success) {
    buf->resize(filesize);
    return true;
  }

  buf->clear();
  return false;
}

void CompileService::ReleaseOutputBuffer(size_t filesize, string* buf) {
  AUTOLOCK(lock, &mu_);
  if (req_sum_output_size_ < filesize) {
    req_sum_output_size_ = 0;
  } else {
    req_sum_output_size_ -= filesize;
  }
  size_t size = buf->size();
  buf->clear();
  if (size > cur_sum_output_size_) {
    LOG(ERROR) << "output buf size error:"
               << " cur=" << cur_sum_output_size_
               << " release=" << size;
    cur_sum_output_size_ = 0;
    return;
  }
  cur_sum_output_size_ -= size;
  return;
}

void CompileService::RecordOutputRename(bool rename) {
  AUTOLOCK(lock, &mu_);
  ++num_file_output_;
  if (rename) {
    ++num_file_rename_output_;
  }
}

int CompileService::GetEstimatedSubprocessDelayTime() {
  static int count = 0;
  static int delay = 0;
  static const int kTimeUpdateCount = 20;
  {
    AUTOLOCK(lock, &mu_);
    if ((count % kTimeUpdateCount) == 0) {
      int mean_include_fileload_time = histogram_->GetStatMean(
          CompilerProxyHistogram::IncludeFileloadTime);
      int mean_rpc_call_time = histogram_->GetStatMean(
          CompilerProxyHistogram::RPCCallTime);
      int mean_file_response_time = histogram_->GetStatMean(
          CompilerProxyHistogram::FileResponseTime);
      int mean_local_pending_time = histogram_->GetStatMean(
          CompilerProxyHistogram::LocalPendingTime);
      int mean_local_run_time = histogram_->GetStatMean(
          CompilerProxyHistogram::LocalRunTime);

      int mean_remote_time = mean_include_fileload_time
          + mean_rpc_call_time
          + mean_file_response_time;
      int mean_local_time = mean_local_pending_time + mean_local_run_time;

      if (mean_remote_time >= mean_local_time) {
        // If local run is fast enough, it uses local as much as possible.
        delay = 0;
      } else {
        // Otherwise, local run is slower than remote call.
        // In this case, it would be better to use remote call as much as
        // possible.  local run, however, will be used to mitigate a remote
        // call stall case (e.g. http shows no activity for long time).
        if (dont_kill_subprocess_) {
          // delay will be 99.7% of remote time.
          double sd_include_fileload_time =
              histogram_->GetStatStandardDeviation(
                  CompilerProxyHistogram::IncludeFileloadTime);
          double sd_rpc_call_time =
              histogram_->GetStatStandardDeviation(
                  CompilerProxyHistogram::RPCCallTime);
          double sd_file_response_time =
              histogram_->GetStatStandardDeviation(
                  CompilerProxyHistogram::FileResponseTime);
          delay = static_cast<int>(mean_remote_time
                                   + 3 * sd_include_fileload_time
                                   + 3 * sd_rpc_call_time
                                   + 3 * sd_file_response_time);
        } else {
          delay = mean_remote_time;
        }
      }
      VLOG(2) << "estimated delay subproc:"
              << " remote=" << mean_remote_time
              << " local=" << mean_local_time
              << " delay=" << delay;
      DCHECK_GE(delay, 0);
      delay += local_run_delay_msec_;
    }
    ++count;
  }
  return delay;
}

const CompileTask* CompileService::FindTaskByIdUnlocked(
    int task_id, bool include_active) {
  if (include_active) {
    for (const auto* task : active_tasks_) {
      if (task->id() == task_id)
        return task;
    }
  }
  for (const auto* task : finished_tasks_) {
    if (task->id() == task_id)
      return task;
  }
  for (const auto* task : failed_tasks_) {
    if (task->id() == task_id)
      return task;
  }
  for (const auto* task : long_tasks_) {
    if (task->id() == task_id)
      return task;
  }
  return nullptr;
}

void CompileService::DumpErrorStatus(std::ostringstream* ss) {
  const int kGomaErrorNoticeVersion = 1;

  ErrorNotices error_notices;
  ErrorNotice* notice = error_notices.add_notice();
  notice->set_version(kGomaErrorNoticeVersion);

  // TODO: decide the design and implement more error notice.
  GomaStats gstats;
  {
    AUTOLOCK(lock, &mu_);
    DumpCommonStatsUnlocked(&gstats);
  }
  InfraStatus* infra_status = notice->mutable_infra_status();
  infra_status->set_ping_status_code(
      gstats.http_rpc_stats().ping_status_code());
  infra_status->set_num_http_sent(
      gstats.http_rpc_stats().query());
  infra_status->set_num_http_active(
      gstats.http_rpc_stats().active());
  infra_status->set_num_http_retry(
      gstats.http_rpc_stats().retry());
  infra_status->set_num_http_timeout(
      gstats.http_rpc_stats().timeout());
  infra_status->set_num_http_error(
      gstats.http_rpc_stats().error());
  infra_status->set_num_network_error(
      gstats.http_rpc_stats().network_error());
  infra_status->set_num_network_recovered(
      gstats.http_rpc_stats().network_recovered());
  infra_status->set_num_compiler_info_miss(
      gstats.request_stats().compiler_info().miss());
  infra_status->set_num_compiler_info_fail(
      gstats.request_stats().compiler_info().fail());
  infra_status->set_num_exec_fail_fallback(
      gstats.request_stats().goma().fail());
  infra_status->set_num_exec_compiler_proxy_failure(
      gstats.request_stats().compiler_proxy().fail());
  infra_status->set_num_user_error(
      gstats.error_stats().user_error());

  if (infra_status->num_exec_compiler_proxy_failure() > 0) {
    notice->set_compile_error(ErrorNotice::COMPILER_PROXY_FAILURE);
  }
  // If GOMA_HERMETIC=error, compile error should also be goma's failure,
  // not compiled code is bad.
  bool compiler_mismatch = CompilerInfoCache::instance()->HasCompilerMismatch();
  if (hermetic_ && !hermetic_fallback_ && compiler_mismatch) {
    notice->set_compile_error(ErrorNotice::COMPILER_PROXY_FAILURE);
  }

  std::string s;
  google::protobuf::util::JsonPrintOptions options;
  options.preserve_proto_field_names = true;
  google::protobuf::util::MessageToJsonString(error_notices, &s, options);
  *ss << s << '\n';
}

void CompileService::DumpCommonStatsUnlocked(GomaStats* stats) {
    RequestStats* request = stats->mutable_request_stats();
    request->set_total(num_exec_request_);
    request->set_success(num_exec_success_);
    request->set_failure(num_exec_failure_);
    request->mutable_compiler_proxy()->set_fail(
        num_exec_compiler_proxy_failure_);
    request->mutable_compiler_info()->set_stores(
        CompilerInfoCache::instance()->NumStores());
    request->mutable_compiler_info()->set_store_dups(
        CompilerInfoCache::instance()->NumStoreDups());
    request->mutable_compiler_info()->set_miss(
        CompilerInfoCache::instance()->NumMiss());
    request->mutable_compiler_info()->set_fail(
        CompilerInfoCache::instance()->NumFail());
    request->mutable_compiler_info()->set_loaded_size_bytes(
        CompilerInfoCache::instance()->LoadedSize());
    request->mutable_goma()->set_finished(num_exec_goma_finished_);
    request->mutable_goma()->set_cache_hit(num_exec_goma_cache_hit_);
    request->mutable_goma()->set_local_cache_hit(
        num_exec_goma_local_cache_hit_);
    request->mutable_goma()->set_aborted(num_exec_goma_aborted_);
    request->mutable_goma()->set_retry(num_exec_goma_retry_);
    request->mutable_goma()->set_fail(num_exec_fail_fallback_);
    request->mutable_local()->set_run(num_exec_local_run_);
    request->mutable_local()->set_killed(num_exec_local_killed_);
    request->mutable_local()->set_finished(num_exec_local_finished_);
    // TODO: local run reason.  list up enum and show with it.
    //                    might need to avoid string field for privacy reason.
    // TODO: error reason. make it enum & show.
    FallbackInSetupStats* fallback = request->mutable_fallback_in_setup();
    fallback->set_failed_to_parse_flags(
        num_forced_fallback_in_setup_[kFailToParseFlags]);
    fallback->set_no_remote_compile_supported(
        num_forced_fallback_in_setup_[kNoRemoteCompileSupported]);
    fallback->set_http_disabled(
        num_forced_fallback_in_setup_[kHTTPDisabled]);
    fallback->set_fail_to_get_compiler_info(
        num_forced_fallback_in_setup_[kFailToGetCompilerInfo]);
    fallback->set_compiler_disabled(
        num_forced_fallback_in_setup_[kCompilerDisabled]);
    fallback->set_requested_by_user(
        num_forced_fallback_in_setup_[kRequestedByUser]);
    FileStats* files = stats->mutable_file_stats();
    files->set_requested(num_file_requested_);
    files->set_uploaded(num_file_uploaded_);
    files->set_missed(num_file_missed_);
    OutputStats* outputs = stats->mutable_output_stats();
    outputs->set_files(num_file_output_);
    outputs->set_rename(num_file_rename_output_);
    outputs->set_buf(num_file_output_buf_);
    outputs->set_peak_req(peak_req_sum_output_size_);
    stats->mutable_memory_stats()->set_consuming(
        GetConsumingMemoryOfCurrentProcess());
    stats->mutable_time_stats()->set_uptime(time(nullptr) - start_time());

    {
      IncludeProcessorStats* processor =
          stats->mutable_include_processor_stats();
      processor->set_total(num_include_processor_total_files_);
      processor->set_skipped(num_include_processor_skipped_files_);
      processor->set_total_wait_time(include_processor_total_wait_time_);
      processor->set_total_run_time(include_processor_total_run_time_);
    }
    if (IncludeCache::IsEnabled()) {
      IncludeCache::instance()->DumpStatsToProto(
          stats->mutable_includecache_stats());
    }
    if (DepsCache::IsEnabled()) {
      DepsCache::instance()->DumpStatsToProto(stats->mutable_depscache_stats());
    }
    if (LocalOutputCache::IsEnabled()) {
      LocalOutputCache::instance()->DumpStatsToProto(
          stats->mutable_local_output_cache_stats());
    }
    http_rpc_->DumpStatsToProto(stats->mutable_http_rpc_stats());
    subprocess_option_setter_->DumpStatsToProto(
        stats->mutable_subprocess_stats());

    int num_user_error = 0;
    int num_log_error = 0;
    int num_log_warning = 0;
    for (const auto& it : error_to_user_) {
      num_user_error += it.second;
    }
    for (const auto& it : error_to_log_) {
      if (it.second.first) {
        num_log_error += it.second.second;
      } else {
        num_log_warning += it.second.second;
      }
    }
    stats->mutable_error_stats()->set_user_error(num_user_error);
    stats->mutable_error_stats()->set_log_error(num_log_error);
    stats->mutable_error_stats()->set_log_warning(num_log_warning);

    int num_command_version_mismatch = 0;
    int num_binary_hash_mismatch = 0;
    int num_subprogram_mismatch = 0;
    for (const auto& it : command_version_mismatch_) {
      num_command_version_mismatch += it.second;
    }
    for (const auto& it : command_binary_hash_mismatch_) {
      num_binary_hash_mismatch += it.second;
    }
    for (const auto& it : subprogram_mismatch_) {
      num_subprogram_mismatch += it.second;
    }
    stats->mutable_mismatch_stats()->set_command_version_mismatch(
        num_command_version_mismatch);
    stats->mutable_mismatch_stats()->set_binary_hash_mismatch(
        num_binary_hash_mismatch);
    stats->mutable_mismatch_stats()->set_subprogram_mismatch(
        num_subprogram_mismatch);
}

void CompileService::DumpStatsToFile(const string& filename) {
  GomaStats stats;
  {
    AUTOLOCK(lock, &mu_);
    DumpCommonStatsUnlocked(&stats);
  }
  histogram_->DumpToProto(stats.mutable_histogram());
  stats.mutable_machine_info()->set_goma_revision(kBuiltRevisionString);
#if defined(__linux__)
  stats.mutable_machine_info()->set_os(MachineInfo_OSType_LINUX);
#elif defined(__MACH__)
  stats.mutable_machine_info()->set_os(MachineInfo_OSType_MAC);
#elif defined(_WIN32)
  stats.mutable_machine_info()->set_os(MachineInfo_OSType_WIN);
#else
  stats.mutable_machine_info()->set_os(MachineInfo_OSType_UNKNOWN);
#endif
  stats.mutable_machine_info()->set_ncpus(GetNumCPUs());
  stats.mutable_machine_info()->set_memory_size(GetSystemTotalMemory());

  string stats_buf;
  if (absl::EndsWith(filename, ".json")) {
    google::protobuf::util::JsonPrintOptions options;
    options.preserve_proto_field_names = true;
    google::protobuf::util::MessageToJsonString(stats, &stats_buf, options);
  } else {
    stats.SerializeToString(&stats_buf);
  }
  if (!WriteStringToFile(stats_buf, filename)) {
    LOG(ERROR) << "failed to dump stats to " << filename;
    return;
  }
  LOG(INFO) << "dumped stats to " << filename;
}

bool CompileService::IncrementActiveFailFallbackTasks() {
  AUTOLOCK(lock, &mu_);
  ++num_active_fail_fallback_tasks_;
  if (max_active_fail_fallback_tasks_ < 0 ||
      num_active_fail_fallback_tasks_ <= max_active_fail_fallback_tasks_)
    return true;

  time_t now = time(nullptr);
  if (reached_max_active_fail_fallback_time_ == 0) {
    reached_max_active_fail_fallback_time_ = now;
    LOG(INFO) << "reached max_active_fail_fallback_tasks."
              << " reached_max_active_fail_fallback_time="
              << reached_max_active_fail_fallback_time_;
  }
  if (now < reached_max_active_fail_fallback_time_ +
            allowed_max_active_fail_fallback_duration_in_sec_) {
    LOG(INFO) << "reached max_active_fail_fallback_tasks but not reached "
              << "end of allowed duration."
              << " max_active_fail_fallback_tasks="
              << max_active_fail_fallback_tasks_
              << " num_active_fail_fallback_tasks="
              << num_active_fail_fallback_tasks_
              << " reached_max_active_fail_fallback_time="
              << reached_max_active_fail_fallback_time_;
    return true;
  }

  LOG(WARNING) << "reached allowed duration of max_active_fail_fallback_tasks."
               << " max_active_fail_fallback_tasks="
               << max_active_fail_fallback_tasks_
               << " num_active_fail_fallback_tasks="
               << num_active_fail_fallback_tasks_
               << " reached_max_active_fail_fallback_time="
               << reached_max_active_fail_fallback_time_;
  return false;
}

void CompileService::RecordForcedFallbackInSetup(
    ForcedFallbackReasonInSetup r) {
  DCHECK(r >= 0 && r < arraysize(num_forced_fallback_in_setup_))
      << "Unknown fallback reason:" << r;
  {
    AUTOLOCK(lock, &mu_);
    ++num_forced_fallback_in_setup_[r];
    if (r != kCompilerDisabled || max_compiler_disabled_tasks_ < 0) {
      return;
    }

    int num_compiler_disabled = num_forced_fallback_in_setup_[r];
    if (num_compiler_disabled < max_compiler_disabled_tasks_) {
      return;
    }
    LOG(WARNING) << "setup step failed more than the threshold."
                 << "Turning on SubProcessController burst mode to make "
                 << "local fallbacks runs more."
                 << " num_compiler_disabled="
                 << num_compiler_disabled
                 << " max_compiler_disabled_tasks="
                 << max_compiler_disabled_tasks_;
    max_compiler_disabled_tasks_ = -1;
  }
  // Execution reaches here only if
  // num_compiler_disabled >= max_compiler_disabled_tasks.
  subprocess_option_setter_->TurnOnBurstMode(
      BurstModeReason::COMPILER_DISABLED);
}

}  // namespace devtools_goma
