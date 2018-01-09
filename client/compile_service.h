// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_COMPILE_SERVICE_H_
#define DEVTOOLS_GOMA_CLIENT_COMPILE_SERVICE_H_

#include <stdint.h>

#include <deque>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <sstream>
#include <vector>

#include "atomic_stats_counter.h"
#include "basictypes.h"
#include "compiler_info.h"
#include "compiler_info_cache.h"
#include "lockhelper.h"
#include "subprocess_option_setter.h"
#include "threadpool_http_server.h"
#include "unordered.h"
#include "worker_thread_manager.h"
#include "watchdog.h"

using std::string;

namespace devtools_goma {

class AutoUpdater;
class CompileTask;
class CompilerFlags;
class CompilerProxyHistogram;
class ExecReq;
class ExecResp;
class ExecServiceClient;
class FileServiceHttpClient;
class FileHashCache;
class GomaStats;
class HttpClient;
class HttpRPC;
class LogServiceClient;
class MultiExecReq;
class MultiExecResp;
class MultiFileStore;

// CompileService provides ExecService API in compiler proxy.
// It is proxy to goma service's ExecService API and FileService API, which is
// managed by CompileTask.
// It also provides the followings:
//   configurations for compile task.
//   remote APIs: http_rpc, file_service.
//   stats histograms.
//   global data shared by all compile tasks.
//     file hash cache, local compiler path, compiler info,
//     command version mismatches.
class CompileService {
 public:
  enum ForcedFallbackReasonInSetup {
    kFailToParseFlags,
    kNoRemoteCompileSupported,
    kHTTPDisabled,
    kFailToGetCompilerInfo,
    kCompilerDisabled,
    kRequestedByUser,

    kNumForcedFallbackReasonInSetup,
  };

  enum HumanReadability {
    kFastHumanUnreadable,
    kHumanReadable,
  };

#ifdef _WIN32
  class MultiRpcController;
#endif
  class RpcController {
   public:
    explicit RpcController(
        ThreadpoolHttpServer::HttpServerRequest* http_server_request);
    ~RpcController();

#ifdef _WIN32
    // Used as sub-RPC of MultiRpcController.
    // In this case, you can't call ParseRequest/SendReply.
    void AttachMultiRpcController(MultiRpcController* multi_rpc);
#endif
    bool ParseRequest(ExecReq* req);
    void SendReply(const ExecResp& resp);

    // Notifies callback when original request is closed.
    // Can be called from any thread.
    // callback will be called on the thread where this method is called.
    void NotifyWhenClosed(OneshotClosure* callback);

    int server_port() const { return server_port_; }

   private:
    friend class CompileService;
    ThreadpoolHttpServer::HttpServerRequest* http_server_request_;
    int server_port_;
#ifdef _WIN32
    MultiRpcController* multi_rpc_;
#endif

    size_t gcc_req_size_;
    size_t* gcc_resp_size_;

    DISALLOW_COPY_AND_ASSIGN(RpcController);
  };

#ifdef _WIN32
  // RpcController for MultiExec.
  class MultiRpcController {
   public:
    MultiRpcController(
        WorkerThreadManager* wm,
        ThreadpoolHttpServer::HttpServerRequest* http_server_request);
    ~MultiRpcController();

    // Parses request as MultiExecReq.
    // Also sets up RpcController and ExecResp for each ExecReq
    // in the MultiExecReq.
    bool ParseRequest(MultiExecReq* req);

    RpcController* rpc(int i) const;
    ExecResp* mutable_resp(int i) const;

    // Called when i-th ExecReq in the MultiExecReq has been done,
    // rpc(i) will be invalidated.
    // Returns true if all resp done.
    bool ExecDone(int i);

    void SendReply();

    // Notifies callback when original request is closed.
    // Can be called from any thread.
    // callback will be called on the thread where this method is called.
    void NotifyWhenClosed(OneshotClosure* callback);

   private:
    void RequestClosed();

    WorkerThreadManager* wm_;
    WorkerThreadManager::ThreadId caller_thread_id_;
    ThreadpoolHttpServer::HttpServerRequest* http_server_request_;
    Lock mu_;
    std::vector<RpcController*> rpcs_;
    std::unique_ptr<MultiExecResp> resp_;
    OneshotClosure* closed_callback_;
    std::vector<std::pair<WorkerThreadManager::ThreadId, OneshotClosure*>>
        closed_callbacks_;

    size_t gcc_req_size_;

    DISALLOW_COPY_AND_ASSIGN(MultiRpcController);
  };
#endif
  struct GetCompilerInfoParam {
    GetCompilerInfoParam()
        : flags(nullptr), cache_hit(false), updated(false) {}
    // request
    WorkerThreadManager::ThreadId thread_id;
    string trace_id;
    CompilerInfoCache::Key key;
    const CompilerFlags* flags;
    std::vector<string> run_envs;

    // response
    ScopedCompilerInfoState state;
    // cache_hit=true > fast cache hit, didn't run in worker thread
    // cache_hit=false,updated=true > cache miss, updated with compiler output
    // cache_hit=false,update=false > cache miss->cache hit in worker thread
    bool cache_hit;
    bool updated;

   private:
    DISALLOW_COPY_AND_ASSIGN(GetCompilerInfoParam);
  };

  explicit CompileService(WorkerThreadManager* wm);
  ~CompileService();

  WorkerThreadManager* wm() { return wm_; }

  // Configurations.
  void SetActiveTaskThrottle(int max_active_tasks);
  void SetCompileTaskHistorySize(int max_finished_tasks,
                                 int max_failed_tasks,
                                 int max_long_tasks);

  const string& username() const { return username_; }

  const string& nodename() const { return nodename_; }
  time_t start_time() const { return start_time_; }
  const string& compiler_proxy_id_prefix() const {
    return compiler_proxy_id_prefix_;
  }
  void SetCompilerProxyIdPrefix(const string& prefix);

  // Takes ownership of option_setter.
  void SetSubProcessOptionSetter(
      std::unique_ptr<SubProcessOptionSetter> option_setter);

  // Takes ownership of http_client.
  void SetHttpClient(std::unique_ptr<HttpClient> http_client);
  HttpClient* http_client() const { return http_client_.get(); }

  // Takes ownership of http_rpc.
  void SetHttpRPC(std::unique_ptr<HttpRPC> http_rpc);
  HttpRPC* http_rpc() const { return http_rpc_.get(); }

  void SetExecServiceClient(
      std::unique_ptr<ExecServiceClient> exec_service_client);
  ExecServiceClient* exec_service_client() const {
    return exec_service_client_.get();
  }

  // Takes ownership of multi_file_store.
  void SetMultiFileStore(std::unique_ptr<MultiFileStore> multi_file_store);
  MultiFileStore* multi_file_store() const {
    return multi_file_store_.get();
  }

  // Takes ownership of file_service.
  void SetFileServiceHttpClient(
      std::unique_ptr<FileServiceHttpClient> file_service);
  FileServiceHttpClient* file_service() const { return file_service_.get(); }

  FileHashCache* file_hash_cache() const { return file_hash_cache_.get(); }
  CompilerProxyHistogram* histogram() const { return histogram_.get(); }

  void StartIncludeProcessorWorkers(int num_threads);
  int include_processor_pool() const { return include_processor_pool_; }

  // Takes ownership of log_service_client.
  void SetLogServiceClient(
      std::unique_ptr<LogServiceClient> log_service_client);
  LogServiceClient* log_service() const { return log_service_client_.get(); }

  // Takes ownership of auto_updater.
  void SetAutoUpdater(std::unique_ptr<AutoUpdater> auto_updater);

  // Takes ownership of watchdog.
  void SetWatchdog(std::unique_ptr<Watchdog> watchdog,
                   const std::vector<string>& goma_ipc_env);

  void WatchdogStart(ThreadpoolHttpServer* server, int count) {
    watchdog_->Start(server, count);
  }

  void SetNeedToSendContent(bool need_to_send_content) {
    need_to_send_content_ = need_to_send_content;
  }
  bool need_to_send_content() const { return need_to_send_content_; }

  void SetNewFileThreshold(int threshold) {
    new_file_threshold_ = threshold;
  }
  int new_file_threshold() const { return new_file_threshold_; }

  void SetEnableGchHack(bool enable) { enable_gch_hack_ = enable;  }
  bool enable_gch_hack() const { return enable_gch_hack_; }

  void SetUseRelativePathsInArgv(bool use_relative_paths_in_argv) {
    use_relative_paths_in_argv_ = use_relative_paths_in_argv;
  }
  bool use_relative_paths_in_argv() const {
    return use_relative_paths_in_argv_;
  }

  void SetCommandCheckLevel(const string& level) {
    command_check_level_ = level;
  }
  const string& command_check_level() const { return command_check_level_; }

  void SetHermetic(bool hermetic) {
    hermetic_ = hermetic;
  }
  bool hermetic() const { return hermetic_; }

  void SetHermeticFallback(bool fallback) {
    hermetic_fallback_ = fallback;
  }
  bool hermetic_fallback() const { return hermetic_fallback_; }

  void SetDontKillSubprocess(bool dont_kill_subprocess) {
    dont_kill_subprocess_ = dont_kill_subprocess;
  }
  bool dont_kill_subprocess() const { return dont_kill_subprocess_; }

  void SetMaxSubProcsPending(int max_subprocs_pending) {
    max_subprocs_pending_ = max_subprocs_pending;
  }
  int max_subprocs_pending() const { return max_subprocs_pending_; }
  void SetLocalRunPreference(int local_run_preference) {
    local_run_preference_ = local_run_preference;
  }
  int local_run_preference() const { return local_run_preference_; }
  void SetLocalRunForFailedInput(bool local_run_for_failed_input) {
    local_run_for_failed_input_ = local_run_for_failed_input;
  }
  bool local_run_for_failed_input() const {
    return local_run_for_failed_input_;
  }
  void SetLocalRunDelayMsec(int local_run_delay_msec) {
    local_run_delay_msec_ = local_run_delay_msec;
  }
  int local_run_delay_msec() const { return local_run_delay_msec_; }
  void SetStoreLocalRunOutput(bool store_local_run_output) {
    store_local_run_output_ = store_local_run_output;
  }
  bool store_local_run_output() const { return store_local_run_output_; }
  void SetEnableRemoteLink(bool enable_remote_link) {
    enable_remote_link_ = enable_remote_link;
  }
  bool enable_remote_link() const { return enable_remote_link_; }

  void SetTmpDir(const string& tmp_dir) { tmp_dir_ = tmp_dir; }
  const string& tmp_dir() const { return tmp_dir_; }

  void SetTimeoutSecs(const std::vector<int>& timeout_secs);
  const std::vector<int>& timeout_secs() const { return timeout_secs_; }

  // Allow to send info. when this function is called.
  // All method that use username() and nodename() should check the flag first.
  void AllowToSendUserInfo() { can_send_user_info_ = true; }
  bool CanSendUserInfo() const { return can_send_user_info_; }

  void SetAllowedNetworkErrorDuration(int seconds) {
    allowed_network_error_duration_in_sec_ = seconds;
  }
  int AllowedNetworkErrorDuration() const {
    return allowed_network_error_duration_in_sec_;
  }

  void SetHashRewriteRule(const std::map<std::string, std::string>& mapping) {
    compiler_info_builder_->SetHashRewriteRule(mapping);
  }

  void SetMaxActiveFailFallbackTasks(int num) {
    max_active_fail_fallback_tasks_ = num;
  }
  void SetAllowedMaxActiveFailFallbackDuration(int duration) {
    allowed_max_active_fail_fallback_duration_in_sec_ = duration;
  }

  void SetMaxCompilerDisabledTasks(int num) {
    max_compiler_disabled_tasks_ = num;
  }

  // ExecService API.
  // Starts new CompileTask.  done will be called on the same thread.
  void Exec(RpcController* rpc,
            const ExecReq* exec_req,
            ExecResp* exec_resp,
            OneshotClosure* done);

  // Called when CompileTask is finished.
  void CompileTaskDone(CompileTask* task);

  // Requests to quit service.
  void Quit();
  bool quit() const;
  // Waits for all tasks finish.
  void Wait();

  bool DumpTask(int task_id, string* out);
  bool DumpTaskRequest(int task_id);
  // Dump the tasks whose state is active or frozen time stamp is after |after|.
  void DumpToJson(Json::Value* json, long long after);
  void DumpStats(std::ostringstream* ss);
  void DumpStatsToFile(const string& filename);
  // Dump stats in json form (converted from GomaStatzStats).
  void DumpStatsJson(std::string* json_string, HumanReadability human_readable);

  void ClearTasks();

  // Finds local compiler for |basename| invoked by |gomacc_path| at |cwd|
  // from |local_path|, and sets the local compiler's path in
  // |local_compiler_path| and PATH that goma dir is removed in
  // |no_goma_local_path|.
  // If |local_compiler_path| is given (and |basename| may be full path),
  // it just checks |local_compiler_path| is not gomacc.
  // Returns true if it finds local compiler.
  // *local_compiler_path returned from this is not be gomacc.
  // |pathext| should only be given on Windows, which represents PATHEXT
  // environment variable.
  bool FindLocalCompilerPath(const string& gomacc_path,
                             const string& basename,
                             const string& cwd,
                             const string& local_path,
                             const string& pathext,
                             string* local_compiler_path,
                             string* no_goma_local_path);

  void GetCompilerInfo(GetCompilerInfoParam* param,
                       OneshotClosure* callback);
  bool DisableCompilerInfo(CompilerInfoState* state,
                           const string& disabled_reason);
  void DumpCompilerInfo(std::ostringstream* ss);

  bool RecordCommandSpecVersionMismatch(
      const string& exec_command_version_mismatch);
  bool RecordCommandSpecBinaryHashMismatch(
      const string& exec_command_binary_hash_mismatch);
  bool RecordSubprogramMismatch(const string& subprogram_mismatch);
  // Record |error_message| is logged with LOG(ERROR) or LOG(WARNING).
  // If |is_error| is true, logged to LOG(ERROR), otherwise LOG(WARNING).
  // Statistics would be kept in CompileService.
  void RecordErrorToLog(const string& error_message, bool is_error);
  // Record |error_message| is sent to gomacc as GOMA Error.
  // Statistics would be kept in CompileService.
  void RecordErrorsToUser(const std::vector<string>& error_messages);

  // Records result for inputs.
  void RecordInputResult(const std::vector<string>& inputs, bool success);
  // Returns true if RecordInputResult recorded any of inputs as not succuess
  // before.
  bool ContainFailedInput(const std::vector<string>& inputs) const;

  void SetMaxSumOutputSize(size_t size) { max_sum_output_size_ = size; }

  // Acquire output buffer in buf for filesize. buf must be empty.
  // Returns true when succeeded and buf would have filesize buffer.
  // Returns false otherwise, and buf remains empty.
  bool AcquireOutputBuffer(size_t filesize, string* buf);
  // Release output buffer acquired by AcquireOutputBuffer.
  // filesize and buf should be the same with AcquireOutputBuffer.
  void ReleaseOutputBuffer(size_t filesize, string* buf);

  // Records output file is renamed or not.
  void RecordOutputRename(bool rename);

  // Returns in msec to delay subprocess setup.
  int GetEstimatedSubprocessDelayTime();

  void DumpErrorStatus(std::ostringstream* ss);

  // Returns false if it reached max_active_fail_fallback_tasks_.
  bool IncrementActiveFailFallbackTasks();

  void RecordForcedFallbackInSetup(ForcedFallbackReasonInSetup r);

 private:
  typedef std::pair<GetCompilerInfoParam*, OneshotClosure*> CompilerInfoWaiter;
  typedef std::vector<CompilerInfoWaiter> CompilerInfoWaiterList;

  // Called when reply from Exec.
  void ExecDone(WorkerThreadManager::ThreadId thread_id, OneshotClosure* done);

  // Called when compiler_mu_ is held either exlusive or shared.
  bool FindLocalCompilerPathUnlocked(
      const string& key,
      const string& key_cwd,
      string* local_compiler_path,
      string* no_goma_local_path) const;
  bool FindLocalCompilerPathAndUpdate(
      const string& key,
      const string& key_cwd,
      const string& gomacc_path,
      const string& basename,
      const string& cwd,
      const string& local_path,
      const string& pathext,
      string* local_compiler_path,
      string* no_goma_local_path);

  void ClearTasksUnlocked();

  const CompileTask* FindTaskByIdUnlocked(int task_id, bool include_active);

  void DumpCommonStatsUnlocked(GomaStats* stats);

  void GetCompilerInfoInternal(GetCompilerInfoParam* param,
                               OneshotClosure* callback);

  WorkerThreadManager* wm_;

  Lock quit_mu_;  // protects quit_
  bool quit_;

  Lock task_id_mu_;
  int task_id_ GUARDED_BY(task_id_mu_);

  Lock mu_;  // protects other fields.
  ConditionVariable cond_;

  int max_active_tasks_;
  int max_finished_tasks_;
  int max_failed_tasks_;
  int max_long_tasks_;
  std::deque<CompileTask*> pending_tasks_;
  std::set<CompileTask*> active_tasks_;
  std::deque<CompileTask*> finished_tasks_;
  std::deque<CompileTask*> failed_tasks_;
  // long_tasks_ is a heap compared by task's handler time.
  // A task with the shortest handler time would come to front of long_tasks_.
  std::vector<CompileTask*> long_tasks_;

  // CompileTask's input that failed.
  ReadWriteLock failed_inputs_mu_;
  unordered_set<string> failed_inputs_;

  string username_;
  string nodename_;
  time_t start_time_;
  string compiler_proxy_id_prefix_;

  std::unique_ptr<SubProcessOptionSetter> subprocess_option_setter_;
  std::unique_ptr<HttpClient> http_client_;
  std::unique_ptr<HttpRPC> http_rpc_;

  std::unique_ptr<ExecServiceClient> exec_service_client_;
  std::unique_ptr<MultiFileStore> multi_file_store_;
  std::unique_ptr<FileServiceHttpClient> file_service_;

  std::unique_ptr<CompilerInfoBuilder> compiler_info_builder_;

  int compiler_info_pool_;

  // protects compiler_info_waiters_, compiler_info_callbacks.
  Lock compiler_info_mu_;
  // key: key_cwd: value: a list of waiting param+closure.
  unordered_map<std::string, CompilerInfoWaiterList*>
    compiler_info_waiters_;

  std::unique_ptr<FileHashCache> file_hash_cache_;

  int include_processor_pool_;

  std::unique_ptr<LogServiceClient> log_service_client_;

  std::unique_ptr<CompilerProxyHistogram> histogram_;

  std::unique_ptr<AutoUpdater> auto_updater_;
  std::unique_ptr<Watchdog> watchdog_;

  bool need_to_send_content_;
  int new_file_threshold_;
  std::vector<int> timeout_secs_;
  bool enable_gch_hack_;
  bool use_relative_paths_in_argv_;
  string command_check_level_;

  // Set hermetic_mode in ExecReq, that is, don't choose different compiler
  // than local one.
  bool hermetic_;
  // If true, local fallback when no compiler in server side.
  // If false, error when no compiler in server side.
  bool hermetic_fallback_;

  bool dont_kill_subprocess_;
  int max_subprocs_pending_;
  int local_run_preference_;
  bool local_run_for_failed_input_;
  int local_run_delay_msec_;
  bool store_local_run_output_;
  bool enable_remote_link_;
  string tmp_dir_;

  // key: "req_ver - resp_ver", value: count
  unordered_map<string, int> command_version_mismatch_;
  unordered_map<string, int> command_binary_hash_mismatch_;

  // key: "path hash", value: count
  unordered_map<string, int> subprogram_mismatch_;

  // key: error reason, value: pair<is_error, count>
  unordered_map<string, std::pair<bool, int>> error_to_log_;
  // key: error reason, value: count
  unordered_map<string, int> error_to_user_;

  // protects local_compiler_paths_
  ReadWriteLock compiler_mu_;

  // key: <gomacc_path>:<basename>:<cwd>:<local_path>
  //     if all path in <local_path> are absolute, "." is used for <cwd>.
  // value: (local_compiler_path, no_goma_local_path)
  unordered_map<string, std::pair<string, string>> local_compiler_paths_;

  int num_exec_request_;
  int num_exec_success_;
  int num_exec_failure_;

  int num_exec_compiler_proxy_failure_;

  int num_exec_goma_finished_;
  int num_exec_goma_cache_hit_;
  int num_exec_goma_local_cache_hit_;
  int num_exec_goma_aborted_;
  int num_exec_goma_retry_;
  int num_exec_local_run_;
  int num_exec_local_killed_;
  int num_exec_local_finished_;
  int num_exec_fail_fallback_;

  std::map<string, int> local_run_reason_;

  int num_file_requested_;
  int num_file_uploaded_;
  int num_file_missed_;
  int num_file_output_;
  int num_file_rename_output_;
  int num_file_output_buf_;

  int num_include_processor_total_files_;
  int num_include_processor_skipped_files_;
  int64_t include_processor_total_wait_time_;  // might not fit in int32.
  int64_t include_processor_total_run_time_;  // might not fit in int32.

  size_t cur_sum_output_size_;
  size_t max_sum_output_size_;
  size_t req_sum_output_size_;
  size_t peak_req_sum_output_size_;

  bool can_send_user_info_;
  int allowed_network_error_duration_in_sec_;

  int num_active_fail_fallback_tasks_;
  int max_active_fail_fallback_tasks_;
  int allowed_max_active_fail_fallback_duration_in_sec_;
  time_t reached_max_active_fail_fallback_time_;

  int num_forced_fallback_in_setup_[kNumForcedFallbackReasonInSetup];
  int max_compiler_disabled_tasks_;

  DISALLOW_COPY_AND_ASSIGN(CompileService);
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_COMPILE_SERVICE_H_
