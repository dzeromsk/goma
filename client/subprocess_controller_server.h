// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// SubProcessController server side.
// Runs in single threaded process.

#ifndef DEVTOOLS_GOMA_CLIENT_SUBPROCESS_CONTROLLER_SERVER_H_
#define DEVTOOLS_GOMA_CLIENT_SUBPROCESS_CONTROLLER_SERVER_H_

#include <map>
#include <memory>

#ifndef _WIN32  // The order of including these files matters in _WIN32
#include "basictypes.h"
#include "scoped_fd.h"
#endif
#include "compiler_specific.h"
MSVC_PUSH_DISABLE_WARNING_FOR_PROTO()
#include "prototmp/subprocess.pb.h"
MSVC_POP_WARNING()
#include "subprocess_controller.h"

namespace devtools_goma {

class SubProcessImpl;

// SubProcessControllerServer runs in a single thread process, and
// communicates with SubProcessControllerClient via pipe_fd.
//
class SubProcessControllerServer: public SubProcessController {
 public:
  // Take ownsership of sock_fd.
  SubProcessControllerServer(int sock_fd,
                             SubProcessController::Options options);
  ~SubProcessControllerServer() override;

  void Loop();

 private:
  SubProcessImpl* LookupSubProcess(int id);

  void Register(std::unique_ptr<SubProcessReq> req) override;
  void RequestRun(std::unique_ptr<SubProcessRun> run) override;
  void Kill(std::unique_ptr<SubProcessKill> kill) override;
  void SetOption(std::unique_ptr<SubProcessSetOption> option) override;

  void Started(std::unique_ptr<SubProcessStarted> started) override;
  void Terminated(std::unique_ptr<SubProcessTerminated> terminated) override;

  void TrySpawnSubProcess();

  void ErrorTerminate(int id, SubProcessTerminated_ErrorTerminate reason);

  void SendNotify(int op, const google::protobuf::Message& message);
  void DoWrite();
  void DoRead();

#ifndef _WIN32
  void SetupSigchldHandler();

  void DoSignal();
#endif
  void DoTimeout();

  std::map<int, std::unique_ptr<SubProcessImpl>> subprocs_;
  ScopedSocket sock_fd_;
#ifndef _WIN32
  ScopedFd signal_fd_;
#endif
  int timeout_millisec_;
  SubProcessController::Options options_;

  DISALLOW_COPY_AND_ASSIGN(SubProcessControllerServer);
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_SUBPROCESS_CONTROLLER_SERVER_H_
