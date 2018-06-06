// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "multi_http_rpc.h"

#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "autolock_timer.h"
#include "callback.h"
#include "compiler_specific.h"
#include "glog/logging.h"
MSVC_PUSH_DISABLE_WARNING_FOR_PROTO()
#include "prototmp/goma_data.pb.h"
MSVC_POP_WARNING()
#include "lockhelper.h"
#include "scoped_fd.h"  // for FAIL
#include "simple_timer.h"
#include "worker_thread_manager.h"

namespace devtools_goma {

MultiHttpRPC::Options::Options()
    : max_req_in_call(0),
      req_size_threshold_in_call(0),
      check_interval_ms(0) {
}

class MultiHttpRPC::MultiJob {
 public:
  // Job is a single call.
  // done callback will be called on the same thread that call is requested.
  class Job {
   public:
    Job(WorkerThreadManager* wm,
        HttpRPC::Status* http_rpc_stat,
        const google::protobuf::Message* req,
        google::protobuf::Message* resp,
        OneshotClosure* callback)
        : wm_(wm),
          thread_id_(wm_->GetCurrentThreadId()),
          http_rpc_stat_(http_rpc_stat),
          req_(req), resp_(resp), callback_(callback) {
      req_size_ = req_->ByteSize();
      timer_.Start();
    }

    HttpRPC::Status* http_rpc_stat() const { return http_rpc_stat_; }
    const google::protobuf::Message* req() const { return req_; }
    int req_size() const { return req_size_; }
    google::protobuf::Message* mutable_resp() { return resp_; }

    void StartCall(Job* master_job) {
      DCHECK(http_rpc_stat_ != nullptr);
      DCHECK(!http_rpc_stat_->finished);
      if (master_job != nullptr) {
        http_rpc_stat_->master_trace_id = master_job->http_rpc_stat()->trace_id;
      }
      http_rpc_stat_->pending_time = timer_.GetInMs();
    }

    void Done() {
      DCHECK(!http_rpc_stat_->finished);
      http_rpc_stat_->finished = true;  // will wake up HttpRPC::Wait
      http_rpc_stat_ = nullptr;
      if (callback_ != nullptr) {
        wm_->RunClosureInThread(FROM_HERE,
                                thread_id_, callback_,
                                WorkerThreadManager::PRIORITY_MED);
        callback_ = nullptr;
      }
      delete this;
    }

   private:
    ~Job() {
      CHECK(callback_ == nullptr);
    }
    WorkerThreadManager* wm_;
    WorkerThreadManager::ThreadId thread_id_;
    HttpRPC::Status* http_rpc_stat_;
    const google::protobuf::Message* req_;
    int req_size_;
    google::protobuf::Message* resp_;
    OneshotClosure* callback_;
    SimpleTimer timer_;
    DISALLOW_COPY_AND_ASSIGN(Job);
  };

  MultiJob(WorkerThreadManager* wm, MultiHttpRPC* multi_rpc)
      : wm_(wm),
        multi_rpc_(multi_rpc),
        req_size_(0) {
  }

  // Adds single call to this Multi call.
  // It must be called before calling Setup().
  void AddCall(HttpRPC::Status* http_rpc_stat,
               const google::protobuf::Message* req,
               google::protobuf::Message* resp,
               OneshotClosure* callback) {
    Job* job = new Job(wm_, http_rpc_stat, req, resp, callback);
    jobs_.push_back(job);
    req_size_ += job->req_size();
  }
  size_t num_call() const { return jobs_.size(); }
  size_t req_size() const { return req_size_; }

  // Calls requests added by AddCall.
  // This MultiJob will be deleted once responses are handled.
  void Call() {
    DCHECK_GT(jobs_.size(), 0U);
    VLOG(1) << "multi rpc " << multi_rpc_->multi_path_
            << " Call num_call=" << num_call();
    if (num_call() == 1) {
      jobs_[0]->StartCall(nullptr);
      // Uses other HttpRPC::Status for underlying http rpc call.
      http_rpc_stat_ = *jobs_[0]->http_rpc_stat();
      DCHECK(!http_rpc_stat_.finished);
      LOG(INFO) << http_rpc_stat_.trace_id << " rpc single";
      multi_rpc_->http_rpc_->CallWithCallback(
          multi_rpc_->path_, jobs_[0]->req(), jobs_[0]->mutable_resp(),
          mutable_status(),
          NewCallback(
              this, &MultiHttpRPC::MultiJob::SingleDone));
      return;
    }

    CHECK_GT(jobs_.size(), 0U);
    multi_rpc_->Setup(this);
    // Initializes with the first ExecReq's status (authorization,
    // timeout_secs, etc.)
    http_rpc_stat_ = *jobs_[0]->http_rpc_stat();
    DCHECK(!http_rpc_stat_.finished);
    LOG(INFO) << http_rpc_stat_.master_trace_id << " rpc multi:"
              << TraceIdList();
    multi_rpc_->http_rpc_->CallWithCallback(
        multi_rpc_->multi_path_, req(), mutable_resp(), mutable_status(),
        NewCallback(
            this, &MultiHttpRPC::MultiJob::Done));
  }

  void SetReq(std::unique_ptr<google::protobuf::Message> req) {
    req_ = std::move(req);
  }
  void SetResp(std::unique_ptr<google::protobuf::Message> resp) {
    resp_ = std::move(resp);
  }

  const std::vector<Job*> jobs() const { return jobs_; }

  const google::protobuf::Message* req() const { return req_.get(); }
  google::protobuf::Message* mutable_resp() { return resp_.get(); }
  HttpRPC::Status* mutable_status() { return &http_rpc_stat_; }

  // Cancels pending jobs. Must be called before calling Call.
  void Cancel() {
    VLOG(1) << "multi rpc " << multi_rpc_->multi_path_
            << " Cancel num_call=" << num_call();
    for (size_t i = 0; i < jobs_.size(); ++i) {
      Job* job = jobs_[i];
      HttpRPC::Status* stat = job->http_rpc_stat();
      stat->connect_success = false;
      stat->err = FAIL;
      stat->err_message = "multi_rpc canceled";
      job->Done();  // job will be deleted.
      jobs_[i] = nullptr;
    }
    delete this;
  }

 private:
  ~MultiJob() {}

  string TraceIdList() const {
    std::ostringstream ss;
    for (const auto* job : jobs_) {
      ss << " " << job->http_rpc_stat()->trace_id;
    }
    return ss.str();
  }

  // Multi call done callback.
  void Done() {
    VLOG(1) << "multi rpc " << multi_rpc_->multi_path_
            << " Done num_call=" << num_call();
    LOG(INFO) << http_rpc_stat_.master_trace_id << " rpc multi done:"
              << TraceIdList();
    LOG_IF(INFO, !http_rpc_stat_.response_header.empty())
        << "MultiHttpRPC done: http response="
        << http_rpc_stat_.response_header;
    if (http_rpc_stat_.err) {
      LOG(WARNING) << http_rpc_stat_.err_message;
      if (http_rpc_stat_.http_return_code == 404)
        multi_rpc_->Disable();
    }
    for (size_t i = 0; i < jobs_.size(); ++i) {
      Job* job = jobs_[i];
      HttpRPC::Status* stat = job->http_rpc_stat();
      DCHECK(!stat->finished);
      if (i == 0) {
        // size and time stat stored only in the first call.
        stat->req_size = http_rpc_stat_.req_size;
        stat->resp_size = http_rpc_stat_.resp_size;
        stat->raw_req_size = http_rpc_stat_.raw_req_size;
        stat->raw_resp_size = http_rpc_stat_.raw_resp_size;
        stat->req_build_time = http_rpc_stat_.req_build_time;
        stat->req_send_time = http_rpc_stat_.req_send_time;
        stat->wait_time = http_rpc_stat_.wait_time;
        stat->resp_recv_time = http_rpc_stat_.resp_recv_time;
        stat->resp_parse_time = http_rpc_stat_.resp_parse_time;
        stat->num_retry = http_rpc_stat_.num_retry;
      }
      multi_rpc_->Done(this, i, stat, job->mutable_resp());
      stat->connect_success = true;
      stat->err = http_rpc_stat_.err;
      stat->err_message = http_rpc_stat_.err_message;
      if (stat->err == OK && stat->http_return_code != 200) {
        stat->err = FAIL;
        std::ostringstream ss;
        ss << "MultiCall ok:" << stat->err_message
           << " but SingleCall error:" << stat->http_return_code;
        stat->err_message = ss.str();
      }
      stat->response_header = http_rpc_stat_.response_header;
      job->Done();  // job will be deleted.
      jobs_[i] = nullptr;
    }
    multi_rpc_->JobDone();
    delete this;
  }

  // Single call done callback.
  void SingleDone() {
    LOG(INFO) << http_rpc_stat_.trace_id << " rpc single done";
    VLOG(1) << "multi rpc " << multi_rpc_->multi_path_
            << " SingleDone num_call=" << num_call();
    CHECK_EQ(jobs_.size(), 1U);
    CHECK(http_rpc_stat_.finished);
    // Copy http_rpc_stat_ except finished.
    // If finished becomes true, waiting thread would destruct HttpRPC::Status.
    // job's http_rpc_stat finished would become true in Job::Done().
    HttpRPC::Status status = http_rpc_stat_;
    status.finished = false;
    *jobs_[0]->http_rpc_stat() = status;
    jobs_[0]->Done();  // job will be deleted.
    jobs_[0] = nullptr;
    multi_rpc_->JobDone();
    delete this;
  }

  WorkerThreadManager* wm_;
  MultiHttpRPC* multi_rpc_;

  std::unique_ptr<google::protobuf::Message> req_;
  std::unique_ptr<google::protobuf::Message> resp_;
  HttpRPC::Status http_rpc_stat_;
  std::vector<Job*> jobs_;
  size_t req_size_;

  DISALLOW_COPY_AND_ASSIGN(MultiJob);
};

MultiHttpRPC::MultiHttpRPC(HttpRPC* http_rpc,
                           string path,
                           string multi_path,
                           const Options& options,
                           WorkerThreadManager* wm)
    : wm_(wm),
      http_rpc_(http_rpc),
      path_(std::move(path)),
      multi_path_(std::move(multi_path)),
      options_(options),
      periodic_callback_id_(kInvalidPeriodicClosureId),
      num_multi_job_(0),
      available_(true),
      num_call_by_req_num_(0),
      num_call_by_req_size_(0),
      num_call_by_latency_(0) {
  CHECK_GT(options_.max_req_in_call, 0U);
  num_call_by_multi_.resize(options_.max_req_in_call + 1);
}

MultiHttpRPC::~MultiHttpRPC() {
  CHECK_EQ(periodic_callback_id_, kInvalidPeriodicClosureId);
}

void MultiHttpRPC::Call(
    HttpRPC::Status* http_rpc_stat,
    const google::protobuf::Message* req,
    google::protobuf::Message* resp,
    OneshotClosure* callback) {
  if (!available_ || options_.max_req_in_call == 1) {
    {
      AUTOLOCK(lock, &mu_);
      ++num_call_by_multi_[1];
    }
    http_rpc_->CallWithCallback(
        path_, req, resp, http_rpc_stat, callback);
    return;
  }

  MultiJob* multi_job = nullptr;
  {
    AUTOLOCK(lock, &mu_);

    // If it is the first call, register periodic checker.
    if (!http_rpc_->client()->shutting_down() &&
        periodic_callback_id_ == kInvalidPeriodicClosureId) {
      periodic_callback_id_ = wm_->RegisterPeriodicClosure(
          FROM_HERE, options_.check_interval_ms,
          NewPermanentCallback(this, &MultiHttpRPC::CheckPending));
    }

    const string& key = MultiJobKey(req);
    MultiJob* pending_multi_job = pending_multi_jobs_[key];
    if (pending_multi_job == nullptr) {
      pending_multi_job = pending_multi_jobs_[key] = new MultiJob(wm_, this);
    }
    pending_multi_job->AddCall(http_rpc_stat, req, resp, callback);
    bool call_now = http_rpc_->client()->shutting_down();
    if (pending_multi_job->num_call() == options_.max_req_in_call) {
      ++num_call_by_req_num_;
      call_now = true;
    } else if (pending_multi_job->req_size() >=
               options_.req_size_threshold_in_call) {
      ++num_call_by_req_size_;
      call_now = true;
    }
    if (call_now) {
      multi_job = pending_multi_job;
      ++num_multi_job_;
      pending_multi_jobs_[key] = nullptr;
      DCHECK_LE(multi_job->num_call(), options_.max_req_in_call);
      ++num_call_by_multi_[multi_job->num_call()];
    }
  }
  if (multi_job != nullptr)
    multi_job->Call();
}

void MultiHttpRPC::Wait() {
  LOG(INFO) << "Wait";
  AUTOLOCK(lock, &mu_);
  DCHECK(http_rpc_->client()->shutting_down());
  if (periodic_callback_id_ != kInvalidPeriodicClosureId) {
    wm_->UnregisterPeriodicClosure(periodic_callback_id_);
    periodic_callback_id_ = kInvalidPeriodicClosureId;
  }
  for (auto& entry : pending_multi_jobs_) {
    if (entry.second != nullptr) {
      entry.second->Cancel();
      entry.second = nullptr;
    }
  }
  for (;;) {
    bool busy = num_multi_job_ > 0;
    if (!busy) {
      for (const auto& entry : pending_multi_jobs_) {
        if (entry.second != nullptr) {
          busy = true;
          break;
        }
      }
    }
    if (!busy) {
      break;
    }
    LOG(INFO) << "num_multi_job=" << num_multi_job_;
    cond_.Wait(&mu_);
  }
}

bool MultiHttpRPC::available() {
  AUTOLOCK(lock, &mu_);
  return available_;
}

string MultiHttpRPC::MultiJobKey(const google::protobuf::Message* req) {
  return "";
}

void MultiHttpRPC::CheckPending() {
  std::vector<MultiJob*> multi_jobs;
  PeriodicClosureId periodic_callback_to_delete = kInvalidPeriodicClosureId;
  {
    AUTOLOCK(lock, &mu_);
    for (auto& entry : pending_multi_jobs_) {
      MultiJob* pending_multi_job = entry.second;
      if (pending_multi_job != nullptr &&
          pending_multi_job->num_call() > 0) {
        multi_jobs.push_back(pending_multi_job);
        entry.second = nullptr;
        DCHECK_LE(pending_multi_job->num_call(), options_.max_req_in_call);
        ++num_call_by_latency_;
        ++num_call_by_multi_[pending_multi_job->num_call()];
      }
    }
    if (periodic_callback_id_ != kInvalidPeriodicClosureId && !available_) {
      periodic_callback_to_delete = periodic_callback_id_;
      periodic_callback_id_ = kInvalidPeriodicClosureId;
    }
  }
  for (const auto& multi_job : multi_jobs) {
    wm_->RunClosure(
        FROM_HERE,
        NewCallback(
            multi_job, &MultiHttpRPC::MultiJob::Call),
        WorkerThreadManager::PRIORITY_MED);
  }

  if (periodic_callback_to_delete != kInvalidPeriodicClosureId) {
    LOG(INFO) << "Unregister periodic callback for MultiHttpRPC "
              << multi_path_;
    // This runs on alamer worker. unregister the closure on another worker.
    wm_->RunClosure(
        FROM_HERE,
        NewCallback(
            this, &MultiHttpRPC::UnregisterCheckPending,
            periodic_callback_to_delete),
        WorkerThreadManager::PRIORITY_IMMEDIATE);
  }
}

void MultiHttpRPC::UnregisterCheckPending(PeriodicClosureId id) {
  wm_->UnregisterPeriodicClosure(id);
}

void MultiHttpRPC::Disable() {
  AUTOLOCK(lock, &mu_);
  LOG_IF(WARNING, available_) << "Disable MultiHttpRPC call " << multi_path_;
  available_ = false;
}

void MultiHttpRPC::JobDone() {
  AUTOLOCK(lock, &mu_);
  --num_multi_job_;
}

string MultiHttpRPC::DebugString() const {
  AUTOLOCK(lock, &mu_);

  std::ostringstream ss;
  ss << "path=" << path_ << std::endl;
  if (available_) {
    ss << "multi_path=" << multi_path_ << std::endl;
    ss << " max req in call=" << options_.max_req_in_call
       << " : call=" << num_call_by_req_num_ << std::endl
       << " req size threshold in call=" << options_.req_size_threshold_in_call
       << " : call=" << num_call_by_req_size_ << std::endl
       << " check interval ms=" << options_.check_interval_ms
       << " : call=" << num_call_by_latency_ << std::endl;
  } else {
    ss << "multi_call disabled" << std::endl;
  }
  ss << "num call by multi:" << std::endl;
  for (size_t i = 1; i < num_call_by_multi_.size(); ++i) {
    ss << i << " reqs in call=" << num_call_by_multi_[i] << std::endl;
  }
  return ss.str();
}

MultiFileStore::MultiFileStore(
    HttpRPC* http_rpc,
    const string& path,
    const MultiHttpRPC::Options& options,
    WorkerThreadManager* wm)
    : MultiHttpRPC(http_rpc, path, path, options, wm) {
}

MultiFileStore::~MultiFileStore() {
}

void MultiFileStore::StoreFile(
    HttpRPC::Status* http_rpc_stat,
    const StoreFileReq* req, StoreFileResp* resp,
    OneshotClosure* callback) {
  Call(http_rpc_stat, req, resp, callback);
}

void MultiFileStore::Setup(MultiHttpRPC::MultiJob* job) {
  std::unique_ptr<StoreFileReq> req(new StoreFileReq);
  const StoreFileReq* one_req = nullptr;
  for (auto* j : job->jobs()) {
    one_req = static_cast<const StoreFileReq*>(j->req());
    DCHECK_EQ(1, one_req->blob_size());
    StoreFileReq* mutable_one_req = const_cast<StoreFileReq*>(one_req);
    req->add_blob()->Swap(mutable_one_req->mutable_blob(0));
  }
  one_req = static_cast<const StoreFileReq*>(job->jobs()[0]->req());
  *req->mutable_requester_info() = one_req->requester_info();
  job->SetReq(std::move(req));
  job->SetResp(std::unique_ptr<google::protobuf::Message>(new StoreFileResp));
}

void MultiFileStore::Done(MultiHttpRPC::MultiJob* multi_job,
                          int i, HttpRPC::Status* stat,
                          google::protobuf::Message* resp) {
  if (i < static_cast<int>(multi_job->jobs().size())) {
    const StoreFileReq* one_req =
        static_cast<const StoreFileReq*>(multi_job->jobs()[i]->req());
    StoreFileReq* mutable_one_req = const_cast<StoreFileReq*>(one_req);
    const StoreFileReq* multi_req =
        static_cast<const StoreFileReq*>(multi_job->req());
    StoreFileReq* mutable_multi_req = const_cast<StoreFileReq*>(multi_req);
    mutable_one_req->mutable_blob(0)->Swap(mutable_multi_req->mutable_blob(i));
  }

  StoreFileResp* multi_resp =
      static_cast<StoreFileResp*>(multi_job->mutable_resp());
  StoreFileResp* one_resp = static_cast<StoreFileResp*>(resp);
  if (i < multi_resp->hash_key_size()) {
    stat->http_return_code = 200;
    one_resp->add_hash_key(multi_resp->hash_key(i));
  } else {
    stat->http_return_code = 500;
  }
}

}  // namespace devtools_goma
