// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "log_service_client.h"

#include "autolock_timer.h"
#include "callback.h"
#include "compiler_specific.h"
#include "cpu.h"
#include "glog/logging.h"
MSVC_PUSH_DISABLE_WARNING_FOR_PROTO()
#include "prototmp/goma_log.pb.h"
MSVC_POP_WARNING()
#include "string_piece_utils.h"
#include "http_rpc.h"
#include "worker_thread_manager.h"

#ifdef __MACH__
#include "mac_version.h"
#elif defined(__linux__)
#include <gnu/libc-version.h>
#endif

namespace {

static devtools_goma::CpuFeatures GetCpuFeatures() {
  devtools_goma::CPU cpu;
  devtools_goma::CpuFeatures features;

  features.set_mmx(cpu.has_mmx());
  features.set_sse(cpu.has_sse());
  features.set_sse2(cpu.has_sse2());
  features.set_sse3(cpu.has_sse3());
  features.set_sse41(cpu.has_sse41());
  features.set_sse42(cpu.has_sse42());
  features.set_popcnt(cpu.has_popcnt());
  features.set_avx(cpu.has_avx());
  features.set_avx2(cpu.has_avx2());
  features.set_aesni(cpu.has_aesni());
  features.set_non_stop_time_stamp_counter(
      cpu.has_non_stop_time_stamp_counter());

  return features;
}

static devtools_goma::OSInfo GetOsInfo() {
  devtools_goma::OSInfo os_info;

#if defined(_WIN32)
  // TODO: set windows version
  os_info.mutable_win_info();
#elif defined(__MACH__)
  os_info.mutable_mac_info()->set_mac_osx_minor_version(
      devtools_goma::MacOSXMinorVersion());
#elif defined(__linux__)
  // TODO: set linux (ubuntu) version (libc version is better?)
  os_info.mutable_linux_info()->set_gnu_libc_version(gnu_get_libc_version());
#endif

  return os_info;
}

}  // namespace

namespace devtools_goma {

class LogServiceClient::SaveLogJob {
 public:
  explicit SaveLogJob(LogServiceClient* log_service)
      : log_service_(log_service), cpu_features_(GetCpuFeatures()),
        os_info_(GetOsInfo()) {
  }

  void AddExecLog(const ExecLog& exec_log) {
    ExecLog* log = req_.add_exec_log();
    *log = exec_log;

    const HttpClient::Options& options =
        log_service_->http_rpc_->client()->options();
    log->set_use_ssl(options.use_ssl);

    log->set_auth_type(ExecLog_AuthenticationType_NONE);
    if (options.oauth2_config.enabled()) {
      log->set_auth_type(ExecLog_AuthenticationType_OAUTH2_APPLICATION);
      if (!options.gce_service_account.empty()) {
        log->set_auth_type(
            ExecLog_AuthenticationType_OAUTH2_GCE_SERVICE_ACCOUNT);
      } else if (!options.service_account_json_filename.empty()) {
        log->set_auth_type(ExecLog_AuthenticationType_OAUTH2_SERVICE_ACCOUNT);
      }
    } else if (options.luci_context_auth.enabled()) {
      log->set_auth_type(ExecLog_AuthenticationType_OAUTH2_LUCI_LOCAL_AUTH);
    } else if (!options.authorization.empty()) {
      if (strings::StartsWith(options.authorization, "Bearer ")) {
        log->set_auth_type(ExecLog_AuthenticationType_OAUTH2_UNSPEC);
      } else {
        log->set_auth_type(ExecLog_AuthenticationType_UNKNOWN);
      }
    }
    *log->mutable_cpu_features() = cpu_features_;
    *log->mutable_os_info() = os_info_;
  }

  void AddMemoryLog(const MemoryUsageLog& memory_usage_log) {
    MemoryUsageLog* log = req_.add_memory_usage_log();
    *log = memory_usage_log;
  }

  bool HasReachedMaxLogSize() const {
    return num_log() >= log_service_->max_log_in_req_;
  }

  size_t num_exec_log() const {
    return req_.exec_log_size();
  }

  size_t num_memory_usage_log() const {
    return req_.memory_usage_log_size();
  }

  size_t num_log() const {
    return num_exec_log() + num_memory_usage_log();
  }

  void Call() {
    LOG(INFO) << "SaveLog"
              << " exec_log=" << num_exec_log()
              << " memory_usage_log=" << num_memory_usage_log()
              << " size=" << req_.ByteSize();
    log_service_->http_rpc_->CallWithCallback(
        log_service_->save_log_path_, &req_, &resp_, &http_rpc_stat_,
        NewCallback(this, &LogServiceClient::SaveLogJob::Done));
  }

  void Delete() {
    VLOG(1) << "Delete";
    delete this;
  }

 private:
  ~SaveLogJob() {
  }
  void Done() {
    VLOG(1) << "SaveLog Done";
    LOG_IF(INFO, !http_rpc_stat_.response_header.empty())
        << "SaveLog done: http response=" << http_rpc_stat_.response_header;
    if (http_rpc_stat_.err) {
      LOG(WARNING) << http_rpc_stat_.err_message;
    }
    log_service_->FinishSaveLogJob();
    delete this;
  }

  LogServiceClient* log_service_;

  SaveLogReq req_;
  SaveLogResp resp_;
  HttpRPC::Status http_rpc_stat_;

  devtools_goma::CpuFeatures cpu_features_;
  const devtools_goma::OSInfo os_info_;

  DISALLOW_COPY_AND_ASSIGN(SaveLogJob);
};

LogServiceClient::LogServiceClient(
    HttpRPC* http_rpc,
    const string& save_log_path,
    size_t max_log_in_req,
    int max_pending_ms,
    WorkerThreadManager* wm)
    : wm_(wm),
      http_rpc_(http_rpc),
      save_log_path_(save_log_path),
      max_log_in_req_(max_log_in_req),
      max_pending_ms_(max_pending_ms),
      periodic_callback_id_(kInvalidPeriodicClosureId),
      cond_(&mu_),
      save_log_job_(nullptr),
      num_save_log_job_(0),
      last_timestamp_ms_(0) {
  CHECK_GT(max_log_in_req_, 0U);
  timer_.Start();
  last_timestamp_ms_ = timer_.GetInMilliSeconds();
}

LogServiceClient::~LogServiceClient() {
  CHECK_EQ(periodic_callback_id_, kInvalidPeriodicClosureId);
  CHECK(save_log_job_ == nullptr);
  CHECK_EQ(0, num_save_log_job_);
}

struct ExecLogSaveFunc {
  explicit ExecLogSaveFunc(const ExecLog& exec_log) : log(exec_log) {}
  void operator()(LogServiceClient::SaveLogJob* job) const {
    job->AddExecLog(log);
  }
  const ExecLog& log;
};

struct MemoryUsageLogSaveFunc {
  explicit MemoryUsageLogSaveFunc(const MemoryUsageLog& memory_usage_log) :
      log(memory_usage_log) {}
  void operator()(LogServiceClient::SaveLogJob* job) const {
    job->AddMemoryLog(log);
  }
  const MemoryUsageLog& log;
};

void LogServiceClient::SaveExecLog(const ExecLog& exec_log) {
  VLOG(2) << "SaveExecLog";
  SaveLogImpl(ExecLogSaveFunc(exec_log));
}

void LogServiceClient::SaveMemoryUsageLog(const MemoryUsageLog& mem_usage_log) {
  VLOG(2) << "SaveMemoryUsageLog";
  SaveLogImpl(MemoryUsageLogSaveFunc(mem_usage_log));
}

template<typename SaveLogFunc>
void LogServiceClient::SaveLogImpl(const SaveLogFunc& func) {
  SaveLogJob* job = nullptr;
  {
    AUTOLOCK(lock, &mu_);
    last_timestamp_ms_ = timer_.GetInMilliSeconds();
    if (!http_rpc_->client()->shutting_down() &&
        periodic_callback_id_ == kInvalidPeriodicClosureId) {
      periodic_callback_id_ = wm_->RegisterPeriodicClosure(
          FROM_HERE,
          std::min(max_pending_ms_ / 10, 1000),
          NewPermanentCallback(this, &LogServiceClient::CheckPending));
    }
    if (save_log_job_ == nullptr)
      save_log_job_ = new SaveLogJob(this);

    func(save_log_job_);

    if (http_rpc_->client()->shutting_down()
        || save_log_job_->HasReachedMaxLogSize()) {
      job = save_log_job_;
      save_log_job_ = nullptr;
    }
    if (job != nullptr) {
      ++num_save_log_job_;
    }
  }
  if (job != nullptr)
    job->Call();
}

void LogServiceClient::Flush() {
  VLOG(1) << "Flush";
  SaveLogJob* job = nullptr;
  {
    AUTOLOCK(lock, &mu_);
    last_timestamp_ms_ = timer_.GetInMilliSeconds();
    if (save_log_job_ == nullptr)
      return;
    if (save_log_job_->num_log() == 0) {
      save_log_job_->Delete();
      save_log_job_ = nullptr;
      return;
    }
    job = save_log_job_;
    save_log_job_ = nullptr;
    if (job != nullptr) {
      ++num_save_log_job_;
    }
  }
  if (job != nullptr) {
    wm_->RunClosure(
        FROM_HERE,
        NewCallback(job, &LogServiceClient::SaveLogJob::Call),
        WorkerThreadManager::PRIORITY_MED);
  }
}

void LogServiceClient::Wait() {
  LOG(INFO) << "Wait";
  AUTOLOCK(lock, &mu_);
  DCHECK(http_rpc_->client()->shutting_down());
  if (periodic_callback_id_ != kInvalidPeriodicClosureId) {
    wm_->UnregisterPeriodicClosure(periodic_callback_id_);
    periodic_callback_id_ = kInvalidPeriodicClosureId;
  }
  if (save_log_job_ != nullptr) {
    save_log_job_->Delete();
    save_log_job_ = nullptr;
  }
  while (save_log_job_ != nullptr || num_save_log_job_ > 0) {
    LOG(INFO) << "num_save_log_job=" << num_save_log_job_;
    cond_.Wait();
  }
}

void LogServiceClient::CheckPending() {
  VLOG(1) << "CheckPending";
  SaveLogJob* job = nullptr;
  {
    AUTOLOCK(lock, &mu_);
    if (save_log_job_ == nullptr)
      return;
    if (save_log_job_->num_log() == 0)
      return;
    if (timer_.GetInMilliSeconds() < last_timestamp_ms_ + max_pending_ms_)
      return;
    job = save_log_job_;
    save_log_job_ = nullptr;
    if (job != nullptr) {
      ++num_save_log_job_;
    }
  }
  if (job != nullptr) {
    wm_->RunClosure(
        FROM_HERE,
        NewCallback(job, &LogServiceClient::SaveLogJob::Call),
        WorkerThreadManager::PRIORITY_MED);
  }
}

void LogServiceClient::FinishSaveLogJob() {
  AUTOLOCK(lock, &mu_);
  --num_save_log_job_;
  CHECK_GE(num_save_log_job_, 0);
  if (num_save_log_job_ == 0)
    cond_.Signal();
}

}  // namespace devtools_goma
