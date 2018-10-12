// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_COMPILER_PROXY_HTTP_HANDLER_H_
#define DEVTOOLS_GOMA_CLIENT_COMPILER_PROXY_HTTP_HANDLER_H_

#include <map>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "basictypes.h"
#include "compile_service.h"
#include "http.h"
#include "log_cleaner.h"
#include "threadpool_http_server.h"
#include "worker_thread.h"

namespace devtools_goma {

class WorkerThreadManager;

// This class is used to handle for every HTTP request to compiler_proxy.
class CompilerProxyHttpHandler : public ThreadpoolHttpServer::HttpHandler,
                                 public ThreadpoolHttpServer::Monitor {
 public:
  CompilerProxyHttpHandler(string myname,
                           string setting,
                           string tmpdir,
                           WorkerThreadManager* wm);

  ~CompilerProxyHttpHandler() override;

  bool InitialPing();

  void HandleHttpRequest(
      ThreadpoolHttpServer::HttpServerRequest* http_server_request) override;

  bool shutting_down() override { return service_.quit(); }

  void Wait();

  void FinishHandle(const ThreadpoolHttpServer::Stat& stat) override;

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

  void TrackMemoryOneshot() { TrackMemory(); }

 private:
  typedef ThreadpoolHttpServer::HttpServerRequest HttpServerRequest;

  typedef int (CompilerProxyHttpHandler::*HttpHandlerMethod)(
      const HttpServerRequest& request,
      string* response);

  void OutputOkHeader(const char* content_type, std::ostringstream* ss);

  int Redirect(const string& url, string* response);

  int BadRequest(string* response);

  void OutputOkHeaderAndBody(const char* content_type,
                             absl::string_view content,
                             std::ostringstream* ss);

  int HandleStatusRequest(const HttpServerRequest& request, string* response);

  int HandleCompilerzRequest(const HttpServerRequest& request,
                             string* response);

  int HandleCompilerzScript(const HttpServerRequest& request, string* response);

  int HandleCompilerzStyle(const HttpServerRequest& request, string* response);

  int HandleJQuery(const HttpServerRequest& request, string* response);

  int HandleLegendHelp(const HttpServerRequest& request, string* response);


  int HandleStatusJavaScript(const HttpServerRequest& request,
                             string* response);

  int HandleContentionzJavaScript(const HttpServerRequest& request,
                                  string* response);

  int HandleStatusCSS(const HttpServerRequest& request, string* response);

  // Helper function for HandleStatusRequest() and HandleStatusRequestOld().
  int HandleStatusRequestHtml(const HttpServerRequest& request,
                              const string& original_status,
                              string* response);

  void GetEndpoints(std::ostringstream* ss);

  void GetGlobalInfo(const HttpServerRequest& request, std::ostringstream* ss);

  int HandleTaskRequest(const HttpServerRequest& request, string* response);

  int HandleAccountRequest(const HttpServerRequest& /* req */,
                           string* response);

  int HandleStatsRequest(const HttpServerRequest& request, string* response);

  int HandleHistogramRequest(const HttpServerRequest& request,
                             string* response);

  int HandleHttpRpcRequest(const HttpServerRequest& /* request */,
                           string* response);

  int HandleThreadRequest(const HttpServerRequest& /* request */,
                          string* response);

  int HandleContentionRequest(const HttpServerRequest& request,
                              string* response);

  int HandleFileCacheRequest(const HttpServerRequest& /* request */,
                             string* response);

  int HandleCompilerInfoRequest(const HttpServerRequest& /* request */,
                                string* response);

  int HandleCompilerJSONRequest(const HttpServerRequest& /* request */,
                                string* response);

  int HandleIncludeCacheRequest(const HttpServerRequest& /* request */,
                                string* response);

  int HandleFlagRequest(const HttpServerRequest& /* request */,
                        string* response);

  int HandleVersionRequest(const HttpServerRequest& /* request */,
                           string* response);

  int HandleHealthRequest(const HttpServerRequest& request, string* response);

  int HandlePortRequest(const HttpServerRequest& request, string* response);

  int HandleLogRequest(const HttpServerRequest& request, string* response);

  int HandleErrorStatusRequest(const HttpServerRequest&, string* response);

#ifdef HAVE_COUNTERZ
  int HandleCounterRequest(const HttpServerRequest&, string* response);
#endif

#ifdef _WIN32
  void ExecDoneInMulti(CompileService::MultiRpcController* rpc, int i);
#endif

  void ExecDone(CompileService::RpcController* rpc, ExecResp* resp);

  void SendErrorMessage(
      ThreadpoolHttpServer::HttpServerRequest* http_server_request,
      int response_code,
      const string& status_message);

  void RunCleanOldLogs();

  void CleanOldLogs();

  void RunTrackMemory();

  void TrackMemory() LOCKS_EXCLUDED(memory_mu_);

  void DumpStatsToInfoLog();

  void DumpHistogramToInfoLog();

  void DumpIncludeCacheLogToInfoLog();

  void DumpContentionLogToInfoLog();

  void DumpStatsProto();

  void DumpCounterz();

  void DumpDirectiveOptimizer();

#if HAVE_HEAP_PROFILER
  int HandleHeapRequest(const HttpServerRequest& request, string* response);
#endif

#if HAVE_CPU_PROFILER
  int HandleProfileRequest(const HttpServerRequest& request, string* response);
#endif

  bool ShouldTrace();

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

  DISALLOW_COPY_AND_ASSIGN(CompilerProxyHttpHandler);
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_COMPILER_PROXY_HTTP_HANDLER_H_
