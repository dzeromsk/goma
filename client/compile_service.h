// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_COMPILE_SERVICE_H_
#define DEVTOOLS_GOMA_CLIENT_COMPILE_SERVICE_H_

#include <stdint.h>

#include <deque>
#include <map>
#include <memory>
#include <string>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "absl/time/time.h"
#include "absl/types/optional.h"
#include "atomic_stats_counter.h"
#include "basictypes.h"
#include "compiler_info.h"
#include "compiler_info_builder.h"
#include "compiler_info_cache.h"
#include "compiler_info_state.h"
#include "compiler_type_specific.h"
#include "compiler_type_specific_collection.h"
#include "lockhelper.h"
#include "subprocess_option_setter.h"
#include "threadpool_http_server.h"
#include "watchdog.h"
#include "worker_thread.h"
#include "worker_thread_manager.h"

using std::string;

namespace devtools_goma {

class AutoUpdater;
class BlobClient;
class CompileTask;
class CompilerFlags;
class CompilerProxyHistogram;
class ExecReq;
class ExecResp;
class ExecServiceClient;
class FileServiceBlobClient;
class FileServiceHttpClient;
class FileHashCache;
class GomaStats;
class HttpClient;
class HttpRPC;
class LogServiceClient;
class MultiExecReq;
class MultiExecResp;
class MultiFileStore;
class RpcController;

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
    KFailToUpdateRequiredFiles,

    kNumForcedFallbackReasonInSetup,
  };

  enum HumanReadability {
    kFastHumanUnreadable,
    kHumanReadable,
  };

  struct GetCompilerInfoParam {
    GetCompilerInfoParam()
        : flags(nullptr), cache_hit(false), updated(false) {}
    // request
    WorkerThread::ThreadId thread_id;
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

  CompileService(WorkerThreadManager* wm, int compiler_info_pool);
  ~CompileService();

  WorkerThreadManager* wm() { return wm_; }

  CompilerTypeSpecificCollection* compiler_type_specific_collection() {
    return compiler_type_specific_collection_.get();
  }

  // Configurations.
  void SetActiveTaskThrottle(int max_active_tasks);
  void SetCompileTaskHistorySize(int max_finished_tasks,
                                 int max_failed_tasks,
                                 int max_long_tasks);

  const string& username() const { return username_; }

  const string& nodename() const { return nodename_; }
  absl::Time start_time() const { return start_time_; }
  const string& compiler_proxy_id_prefix() const {
    return compiler_proxy_id_prefix_;
  }
  void SetCompilerProxyIdPrefix(const string& prefix);

  void SetSubProcessOptionSetter(
      std::unique_ptr<SubProcessOptionSetter> option_setter);

  void SetHttpClient(std::unique_ptr<HttpClient> http_client);
  HttpClient* http_client() const { return http_client_.get(); }

  void SetHttpRPC(std::unique_ptr<HttpRPC> http_rpc);
  HttpRPC* http_rpc() const { return http_rpc_.get(); }

  void SetExecServiceClient(
      std::unique_ptr<ExecServiceClient> exec_service_client);
  ExecServiceClient* exec_service_client() const {
    return exec_service_client_.get();
  }

  void SetMultiFileStore(std::unique_ptr<MultiFileStore> multi_file_store);
  MultiFileStore* multi_file_store() const {
    return multi_file_store_.get();
  }

  void SetFileServiceHttpClient(
      std::unique_ptr<FileServiceHttpClient> file_service);
  BlobClient* blob_client() const;

  FileHashCache* file_hash_cache() const { return file_hash_cache_.get(); }
  CompilerProxyHistogram* histogram() const { return histogram_.get(); }

  void StartIncludeProcessorWorkers(int num_threads);
  int include_processor_pool() const { return include_processor_pool_; }

  void SetLogServiceClient(
      std::unique_ptr<LogServiceClient> log_service_client);
  LogServiceClient* log_service() const { return log_service_client_.get(); }

  void SetAutoUpdater(std::unique_ptr<AutoUpdater> auto_updater);

  void SetWatchdog(std::unique_ptr<Watchdog> watchdog,
                   const std::vector<string>& goma_ipc_env);

  void WatchdogStart(ThreadpoolHttpServer* server, int count) {
    watchdog_->Start(server, count);
  }

  void SetNeedToSendContent(bool need_to_send_content) {
    need_to_send_content_ = need_to_send_content;
  }
  bool need_to_send_content() const { return need_to_send_content_; }

  void SetNewFileThresholdDuration(absl::Duration threshold) {
    new_file_threshold_duration_ = threshold;
  }
  absl::Duration new_file_threshold_duration() const {
    return new_file_threshold_duration_;
  }

  void SetEnableGchHack(bool enable) { enable_gch_hack_ = enable;  }
  bool enable_gch_hack() const { return enable_gch_hack_; }

  void SetUseRelativePathsInArgv(bool use_relative_paths_in_argv) {
    use_relative_paths_in_argv_ = use_relative_paths_in_argv;
  }
  bool use_relative_paths_in_argv() const {
    return use_relative_paths_in_argv_;
  }
  void SetSendExpectedOutputs(bool send_expected_outputs) {
    send_expected_outputs_ = send_expected_outputs;
  }
  bool send_expected_outputs() const {
    return send_expected_outputs_;
  }

  void SetSendCompilerBinaryAsInput(bool flag) {
    send_compiler_binary_as_input_ = flag;
  }
  bool send_compiler_binary_as_input() const {
    return send_compiler_binary_as_input_;
  }
  void SetUseUserSpecifiedPathForSubprograms(bool flag) {
    use_user_specified_path_for_subprograms_ = flag;
  }
  bool use_user_specified_path_for_subprograms() const {
    return use_user_specified_path_for_subprograms_;
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
  void SetLocalRunDelay(absl::Duration local_run_delay) {
    local_run_delay_ = local_run_delay;
  }
  absl::Duration local_run_delay() const { return local_run_delay_; }
  void SetStoreLocalRunOutput(bool store_local_run_output) {
    store_local_run_output_ = store_local_run_output;
  }
  bool store_local_run_output() const { return store_local_run_output_; }

  void SetShouldFailForUnsupportedCompilerFlag(bool f) {
    should_fail_for_unsupported_compiler_flag_ = f;
  }
  bool should_fail_for_unsupported_compiler_flag() const {
    return should_fail_for_unsupported_compiler_flag_;
  }

  void SetTmpDir(const string& tmp_dir) { tmp_dir_ = tmp_dir; }
  const string& tmp_dir() const { return tmp_dir_; }

  void SetTimeouts(const std::vector<absl::Duration>& timeouts) {
    timeouts_ = timeouts;
  }
  const std::vector<absl::Duration>& timeouts() const { return timeouts_; }

  // Allow to send info. when this function is called.
  // All method that use username() and nodename() should check the flag first.
  void AllowToSendUserInfo() { can_send_user_info_ = true; }
  bool CanSendUserInfo() const { return can_send_user_info_; }

  void SetAllowedNetworkErrorDuration(absl::Duration duration) {
    allowed_network_error_duration_ = duration;
  }
  absl::optional<absl::Duration> AllowedNetworkErrorDuration() const {
    return allowed_network_error_duration_;
  }

  void SetMaxActiveFailFallbackTasks(int num) {
    max_active_fail_fallback_tasks_ = num;
  }
  void SetAllowedMaxActiveFailFallbackDuration(absl::Duration duration) {
    allowed_max_active_fail_fallback_duration_ = duration;
  }

  void SetMaxCompilerDisabledTasks(int num) {
    max_compiler_disabled_tasks_ = num;
  }

  // ExecService API.
  // Starts new CompileTask.  done will be called on the same thread.
  void Exec(RpcController* rpc,
            const ExecReq& exec_req,
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
  bool DumpTaskRequest(int task_id, string* message);
  // Dump the tasks whose state is active or frozen time stamp is after |after|.
  void DumpToJson(Json::Value* json, absl::Time after);
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

  void SetMaxSumOutputSize(size_t size) LOCKS_EXCLUDED(buf_mu_) {
    AUTO_EXCLUSIVE_LOCK(lock, &buf_mu_);
    max_sum_output_size_ = size;
  }

  // Acquire output buffer in buf for filesize. buf must be empty.
  // Returns true when succeeded and buf would have filesize buffer.
  // Returns false otherwise, and buf remains empty.
  bool AcquireOutputBuffer(size_t filesize, string* buf)
      LOCKS_EXCLUDED(buf_mu_);
  // Release output buffer acquired by AcquireOutputBuffer.
  // filesize and buf should be the same with AcquireOutputBuffer.
  void ReleaseOutputBuffer(size_t filesize, string* buf)
      LOCKS_EXCLUDED(buf_mu_);

  // Records output file is renamed or not.
  void RecordOutputRename(bool rename);

  // Returns duration to delay subprocess setup.
  absl::Duration GetEstimatedSubprocessDelayTime();

  void DumpErrorStatus(std::ostringstream* ss);

  // Returns false if it reached max_active_fail_fallback_tasks_.
  bool IncrementActiveFailFallbackTasks();

  void RecordForcedFallbackInSetup(ForcedFallbackReasonInSetup r);

 private:
  typedef std::pair<GetCompilerInfoParam*, OneshotClosure*> CompilerInfoWaiter;
  typedef std::vector<CompilerInfoWaiter> CompilerInfoWaiterList;

  // Called when reply from Exec.
  void ExecDone(WorkerThread::ThreadId thread_id, OneshotClosure* done);

  bool FindLocalCompilerPathUnlocked(const string& key,
                                     const string& key_cwd,
                                     string* local_compiler_path,
                                     string* no_goma_local_path) const
      SHARED_LOCKS_REQUIRED(compiler_mu_);
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

  void DumpCommonStatsUnlocked(GomaStats* stats) SHARED_LOCKS_REQUIRED(buf_mu_)
      EXCLUSIVE_LOCKS_REQUIRED(mu_);

  void GetCompilerInfoInternal(GetCompilerInfoParam* param,
                               OneshotClosure* callback);

  WorkerThreadManager* wm_;

  mutable Lock quit_mu_;
  bool quit_ GUARDED_BY(quit_mu_) = false;

  mutable Lock task_id_mu_;
  int task_id_ GUARDED_BY(task_id_mu_) = 0;

  // TODO: add thread annotation
  mutable Lock mu_;  // protects other fields.
  ConditionVariable cond_;

  int max_active_tasks_;
  int max_finished_tasks_;
  int max_failed_tasks_;
  int max_long_tasks_;
  std::deque<CompileTask*> pending_tasks_;
  std::unordered_set<CompileTask*> active_tasks_;
  std::deque<CompileTask*> finished_tasks_;
  std::deque<CompileTask*> failed_tasks_;
  // long_tasks_ is a heap compared by task's handler time.
  // A task with the shortest handler time would come to front of long_tasks_.
  std::vector<CompileTask*> long_tasks_;

  // CompileTask's input that failed.
  mutable ReadWriteLock failed_inputs_mu_;
  std::unordered_set<string> failed_inputs_ GUARDED_BY(failed_inputs_mu_);

  string username_;
  string nodename_;
  absl::Time start_time_;
  string compiler_proxy_id_prefix_;

  std::unique_ptr<SubProcessOptionSetter> subprocess_option_setter_;
  std::unique_ptr<HttpClient> http_client_;
  std::unique_ptr<HttpRPC> http_rpc_;

  std::unique_ptr<ExecServiceClient> exec_service_client_;
  std::unique_ptr<MultiFileStore> multi_file_store_;
  std::unique_ptr<BlobClient> blob_client_;

  std::unique_ptr<CompilerTypeSpecificCollection>
      compiler_type_specific_collection_;

  int compiler_info_pool_;

  mutable Lock compiler_info_mu_;
  // key: key_cwd: value: a list of waiting param+closure.
  std::unordered_map<std::string, CompilerInfoWaiterList*>
      compiler_info_waiters_ GUARDED_BY(compiler_info_mu_);

  std::unique_ptr<FileHashCache> file_hash_cache_;

  int include_processor_pool_;

  std::unique_ptr<LogServiceClient> log_service_client_;

  std::unique_ptr<CompilerProxyHistogram> histogram_;

  std::unique_ptr<AutoUpdater> auto_updater_;
  std::unique_ptr<Watchdog> watchdog_;

  bool need_to_send_content_ = false;
  absl::Duration new_file_threshold_duration_;
  std::vector<absl::Duration> timeouts_;
  // TODO: moving enable_gch_hack to compiler specific place.
  //                    The code using the flag scatters in compile_task.
  //                    It is difficult to omit all the use case in one CL.
  bool enable_gch_hack_;
  bool use_relative_paths_in_argv_ = false;
  bool send_expected_outputs_ = false;
  string command_check_level_;
  bool send_compiler_binary_as_input_ = false;
  bool use_user_specified_path_for_subprograms_ = false;

  // Set hermetic_mode in ExecReq, that is, don't choose different compiler
  // than local one.
  bool hermetic_ = false;
  // If true, local fallback when no compiler in server side.
  // If false, error when no compiler in server side.
  bool hermetic_fallback_ = false;

  bool dont_kill_subprocess_ = false;
  int max_subprocs_pending_ = 0;
  int local_run_preference_ = 0;
  bool local_run_for_failed_input_ = false;
  absl::Duration local_run_delay_;
  bool store_local_run_output_ = false;
  bool should_fail_for_unsupported_compiler_flag_ = false;
  string tmp_dir_;

  // key: "req_ver - resp_ver", value: count
  std::unordered_map<string, int> command_version_mismatch_;
  std::unordered_map<string, int> command_binary_hash_mismatch_;

  // key: "path hash", value: count
  std::unordered_map<string, int> subprogram_mismatch_;

  // key: error reason, value: pair<is_error, count>
  std::unordered_map<string, std::pair<bool, int>> error_to_log_;
  // key: error reason, value: count
  std::unordered_map<string, int> error_to_user_;

  mutable ReadWriteLock compiler_mu_;

  // key: <gomacc_path>:<basename>:<cwd>:<local_path>
  //     if all path in <local_path> are absolute, "." is used for <cwd>.
  // value: (local_compiler_path, no_goma_local_path)
  std::unordered_map<string, std::pair<string, string>> local_compiler_paths_
      GUARDED_BY(compiler_mu_);

  int num_exec_request_ = 0;
  int num_exec_success_ = 0;
  int num_exec_failure_ = 0;

  int num_exec_compiler_proxy_failure_ = 0;

  int num_exec_goma_finished_ = 0;
  int num_exec_goma_cache_hit_ = 0;
  int num_exec_goma_local_cache_hit_ = 0;
  int num_exec_goma_aborted_ = 0;
  int num_exec_goma_retry_ = 0;
  int num_exec_local_run_ = 0;
  int num_exec_local_killed_ = 0;
  int num_exec_local_finished_ = 0;
  int num_exec_fail_fallback_ = 0;

  std::map<string, int> local_run_reason_;

  mutable ReadWriteLock buf_mu_;

  int num_file_requested_ = 0;
  int num_file_uploaded_ = 0;
  int num_file_missed_ = 0;
  int num_file_dropped_ = 0;
  int num_file_output_ = 0;
  int num_file_rename_output_ = 0;
  int num_file_output_buf_ GUARDED_BY(buf_mu_) = 0;

  int num_include_processor_total_files_ = 0;
  int num_include_processor_skipped_files_ = 0;
  absl::Duration include_processor_total_wait_time_;
  absl::Duration include_processor_total_run_time_;

  size_t cur_sum_output_size_ GUARDED_BY(buf_mu_) = 0;
  size_t max_sum_output_size_ GUARDED_BY(buf_mu_) = 0;
  size_t req_sum_output_size_ GUARDED_BY(buf_mu_) = 0;
  size_t peak_req_sum_output_size_ GUARDED_BY(buf_mu_) = 0;

  bool can_send_user_info_ = false;
  absl::optional<absl::Duration> allowed_network_error_duration_;

  int num_active_fail_fallback_tasks_ = 0;
  int max_active_fail_fallback_tasks_ = -1;
  absl::Duration allowed_max_active_fail_fallback_duration_;
  absl::optional<absl::Time> reached_max_active_fail_fallback_time_;

  int num_forced_fallback_in_setup_[kNumForcedFallbackReasonInSetup];
  int max_compiler_disabled_tasks_ = -1;

  DISALLOW_COPY_AND_ASSIGN(CompileService);
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_COMPILE_SERVICE_H_
