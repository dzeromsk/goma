// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_WATCHDOG_H_
#define DEVTOOLS_GOMA_CLIENT_WATCHDOG_H_

#include <string>
#include <vector>

#include "basictypes.h"
#include "threadpool_http_server.h"

using std::string;

namespace devtools_goma {

class Closure;
class CompileService;

// compiler proxy watchdog.
// It periodically runs "gomacc port" and see the port is the same as
// this process's port.  If it doesn't match, commit suicide.
class Watchdog {
 public:
  Watchdog();
  ~Watchdog();

  // Starts watchdog with server's idle timer.
  // Doesn't take ownership of server.
  void Start(ThreadpoolHttpServer* server, int count);

  // Sets watchdog target.
  // Doesn't take ownership of service.
  void SetTarget(CompileService* service,
                 const std::vector<string>& goma_ipc_env);
 private:
  void Check();

  const string dir_;
  const string gomacc_path_;
  ThreadpoolHttpServer* server_;
  int idle_counter_;
  CompileService* service_;
  std::vector<string> goma_ipc_env_;
  ThreadpoolHttpServer::RegisteredClosureID closure_id_;

  DISALLOW_COPY_AND_ASSIGN(Watchdog);
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_WATCHDOG_H_
