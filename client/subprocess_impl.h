// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_SUBPROCESS_IMPL_H_
#define DEVTOOLS_GOMA_CLIENT_SUBPROCESS_IMPL_H_

#include <memory>

#include "basictypes.h"
#include "compiler_specific.h"
MSVC_PUSH_DISABLE_WARNING_FOR_PROTO()
#include "prototmp/subprocess.pb.h"
MSVC_POP_WARNING()
#include "spawner.h"
#include "simple_timer.h"
#include "scoped_fd.h"

namespace devtools_goma {

// A SubProcessImpl is associated with a single subprocess.
// It is created and owned by SubProcessControllerServer.
class SubProcessImpl {
 public:
  SubProcessImpl(const SubProcessReq& req, bool dont_kill_subprocess);
  ~SubProcessImpl();

  SubProcessState::State state() const { return state_; }
  const SubProcessReq& req() const { return req_; }
  const SubProcessStarted& started() const { return started_; }

  SubProcessStarted* Spawn();

  void RaisePriority();

  // Kills the subprocess.
  // Returns true if the subprocess is still running.
  // Returns false if the subprocess has been terminated.
  bool Kill();

  void Signaled(int status);

  // Waits for the subprocess termination.
  // If |need_kill| is true, it will kill the subprocess.
  // Returns SubProcessTerminated object if the subprocess has been terminated.
  // Returns NULL if the subprocess is still running.
  SubProcessTerminated* Wait(bool need_kill);

 private:
  SubProcessState::State state_;
  SubProcessReq req_;
  SubProcessStarted started_;
  SubProcessTerminated terminated_;
  std::unique_ptr<Spawner> spawner_;
  SimpleTimer timer_;
  bool kill_subprocess_;

  DISALLOW_COPY_AND_ASSIGN(SubProcessImpl);
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_SUBPROCESS_IMPL_H_
