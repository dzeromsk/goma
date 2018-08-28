// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_GOMACC_COMMON_H_
#define DEVTOOLS_GOMA_CLIENT_GOMACC_COMMON_H_

#include <memory>
#include <string>
#include <vector>

#include "basictypes.h"
#include "goma_ipc.h"
#include "scoped_fd.h"

using std::string;

namespace devtools_goma {

class CompilerFlags;
class ExecReq;
class ExecResp;
class MultiExecReq;
class MultiExecResp;

// Returns the port where http server is running.
// Returns -1 when compiler proxy is not ready.
// |status| will be modified if |status| is non-NULL.
int GetCompilerProxyPort(GomaIPC::Status* status);

bool StartCompilerProxy();

class GomaClient {
 public:
  enum Result {
    IPC_OK = 0,
    IPC_FAIL = -1,
    IPC_REJECTED = -2,
  };

  GomaClient(int pid,
             std::unique_ptr<CompilerFlags> flags,
             const char** envp,
             string local_compiler_path);
  ~GomaClient();
  void OutputResp();

  int retval() const;
  int id() const { return id_; }
  const IOChannel* chan() const { return ipc_chan_.get(); }

  // Call IPC Request. Return IPC_OK if successful.
  Result CallIPCAsync();

  // Wait an already dispatched IPC request to finish.  This needs to be
  // called after CallIPCAsync().
  Result WaitIPC();

  string CreateStdinFile();

  // Blocking version of IPC call which calls CallIPCAsync and WaitIPC
  // internally.
  Result CallIPC();

  // Sets overriding gomacc_path.
  // The caller's executable path will be used by default when it is not set.
  void set_gomacc_path(const string& path) { gomacc_path_ = path; }

  void set_cwd(const string& cwd) { cwd_ = cwd; }

  void set_local_compiler_path(const string& local_compiler_path) {
    local_compiler_path_ = local_compiler_path;
  }

 private:
#ifdef _WIN32
  bool PrepareMultiExecRequest(MultiExecReq* req);
  void OutputMultiExecResp(MultiExecResp* resp);
#endif

  bool PrepareExecRequest(const CompilerFlags& flags, ExecReq* req);
  void OutputExecResp(ExecResp* resp);
#ifndef _WIN32
  void OutputProfInfo(const ExecResp& resp);
#endif

  GomaIPC goma_ipc_;
  std::unique_ptr<IOChannel> ipc_chan_;
  GomaIPC::Status status_;

  int id_;
  std::unique_ptr<CompilerFlags> flags_;
  string name_;
  std::vector<string> envs_;
#ifdef _WIN32
  std::vector<ScopedFd*> optional_files_;
  std::unique_ptr<MultiExecResp> multi_exec_resp_;
  std::vector<std::pair<string, ScopedFd*>> rsp_files_;
#endif
  std::unique_ptr<ExecResp> exec_resp_;
  ScopedFd stdin_file_;
  string stdin_filename_;
  string gomacc_path_;
  string cwd_;
  string local_compiler_path_;

  DISALLOW_COPY_AND_ASSIGN(GomaClient);
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_GOMACC_COMMON_H_
