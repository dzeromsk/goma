// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_AUTO_UPDATER_H_
#define DEVTOOLS_GOMA_CLIENT_AUTO_UPDATER_H_

#include <memory>
#include <string>
#include <vector>

#include "basictypes.h"
#include "lockhelper.h"
#include "threadpool_http_server.h"

namespace devtools_goma {

class Closure;
class SubProcessTask;

class AutoUpdater {
 public:
  explicit AutoUpdater(const string& goma_ctl);
  ~AutoUpdater();

  // Sets environments to run goma_ctl.
  void SetEnv(const char* envp[]);

  // Starts auto updater.  If server's idle counter reaches "count",
  // it will check latest version of goma by calling "goma_ctl pull", and
  // if it finds newer version, it runs "goma_ctl update" to update binaries.
  // "goma_ctl update" will restart the running process.
  void Start(ThreadpoolHttpServer* server, int count);

  // Stops auto updater.
  void Stop();

  // Wait subproc_ finished.
  void Wait();

  int my_version() const { return my_version_; }
  int pulled_version() const { return pulled_version_; }

 private:
  // Reads manifest file specified by path, and sets version.
  // Returns true if success, false otherwise.
  bool ReadManifest(const string& path, int* version);

  void CheckUpdate();

  void StartGomaCtlPull();
  void GomaCtlPullDone();

  void StartGomaCtlUpdate();

  string dir_;
  int my_version_;
  int pulled_version_;
  int idle_counter_;
  // server_ != nullptr while AutoUpdater is running.
  ThreadpoolHttpServer* server_;
  ThreadpoolHttpServer::RegisteredClosureID pull_closure_id_;

  // protect |subproc_|
  mutable Lock mu_;
  // signaled if subproc_ become nullptr.
  ConditionVariable cond_;
  // If subproc_ != nullptr, "goma_ctl pull" is running.
  SubProcessTask* subproc_;
  std::vector<string> env_;
  string goma_ctl_;

  DISALLOW_COPY_AND_ASSIGN(AutoUpdater);
};
}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_AUTO_UPDATER_H_
