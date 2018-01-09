// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_MULTI_HTTP_RPC_H_
#define DEVTOOLS_GOMA_CLIENT_MULTI_HTTP_RPC_H_

#include <string>
#include <vector>

#include "basictypes.h"
#include "http_rpc.h"
#include "lockhelper.h"
#include "unordered.h"

using std::string;

namespace google {
namespace protobuf {
class Message;
}  // namespace protobuf
}  // namespace google

namespace devtools_goma {

class ExecReq;
class ExecResp;
class OneshotClosure;
class StoreFileReq;
class StoreFileResp;
class WorkerThreadManager;

// MultiExecClient is an ExecService.Exec API service implementation that
// is realized by ExecService.MultiExec stub on top of HttpRPC.
// Client can use Exec() as single Exec API call, but MultiExecClient packs
// at most max_req_in_call into single MultiExec call to path over http_rpc.
// It also checks pending requests in each check_interval_ms, and if any
// pending Exec requests, it issues MultiExec call.
class MultiHttpRPC {
 public:
  struct Options {
    Options();
    size_t max_req_in_call;
    size_t req_size_threshold_in_call;
    int check_interval_ms;
  };

  virtual ~MultiHttpRPC();

  virtual void Call(HttpRPC::Status* http_rpc_stat,
                    const google::protobuf::Message* req,
                    google::protobuf::Message* resp,
                    OneshotClosure* callback);

  void Wait();

  const Options& options() { return options_; }
  bool available();

  string DebugString() const;

 protected:
  class MultiJob;
  friend class MultiJob;

  MultiHttpRPC(HttpRPC* http_rpc,
               const string& path, const string& multi_path,
               const Options& options,
               WorkerThreadManager* wm);

  // Returns a key for pending multi job for the given req.
  // req will be batched in same multi job if the key is the same.
  // Returns "" by default (so no affinity).
  virtual string MultiJobKey(const google::protobuf::Message* req);

  virtual void Setup(MultiJob* job) = 0;
  virtual void Done(MultiJob* job, int i, HttpRPC::Status* stat,
                    google::protobuf::Message* resp) = 0;

  void CheckPending();
  void UnregisterCheckPending(PeriodicClosureId id);
  void Disable();

  void JobDone();

  WorkerThreadManager* wm_;
  HttpRPC* http_rpc_;
  const string path_;
  const string multi_path_;
  const Options options_;

  PeriodicClosureId periodic_callback_id_;

  Lock mu_;
  // Condition to check num_multi_job_ becomes 0.
  ConditionVariable cond_;
  int num_multi_job_;  // number of jobs on-the-fly.

  unordered_map<string, MultiJob*> pending_multi_jobs_;
  bool available_;
  std::vector<int> num_call_by_multi_;
  int num_call_by_req_num_;
  int num_call_by_req_size_;
  int num_call_by_latency_;

 private:
  DISALLOW_COPY_AND_ASSIGN(MultiHttpRPC);
};

class MultiFileStore : public MultiHttpRPC {
 public:
  MultiFileStore(HttpRPC* http_rpc,
                 const string& path,
                 const MultiHttpRPC::Options& options,
                 WorkerThreadManager* wm);
  ~MultiFileStore() override;

  void StoreFile(HttpRPC::Status* http_rpc_stat,
                 const StoreFileReq* req, StoreFileResp* resp,
                 OneshotClosure* callback);

  void Setup(MultiHttpRPC::MultiJob* job) override;
  void Done(MultiHttpRPC::MultiJob* job,
            int i, HttpRPC::Status* stat,
            google::protobuf::Message* resp) override;

 private:
  DISALLOW_COPY_AND_ASSIGN(MultiFileStore);
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_MULTI_HTTP_RPC_H_
