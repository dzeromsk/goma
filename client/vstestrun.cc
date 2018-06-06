// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// Usage:
//  vstestrun --vsver=9.0 command line

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <glog/logging.h>
#include <glog/stl_logging.h>

#ifndef _WIN32
#error This module is Windows only
#endif

#include "absl/strings/match.h"
#include "config_win.h"
#include "file.h"
#include "file_dir.h"
#include "filesystem.h"
#include "mypath.h"
#include "path.h"
#include "vsvars.h"

int RunWithVSVars(std::string vsvars_path, int argv0, int argc, char** argv) {
  std::string tmpdir = devtools_goma::GetGomaTmpDir();
  file::RecursivelyDelete(tmpdir, file::Defaults());
  std::string batchfile = file::JoinPath(tmpdir, "vsrun.bat");
  std::ofstream batch;
  batch.open(batchfile.c_str());
  batch << "call \"" << vsvars_path << "\"" << std::endl;
  for (int i = argv0; i < argc; ++i) {
    bool need_quote = strchr(argv[i], ' ') != nullptr;
    if (need_quote)
      batch << "\"";
    batch << argv[i];
    if (need_quote)
      batch << "\"";
    batch << " ";
  }
  batch << std::endl;
  batch.close();

  PROCESS_INFORMATION pi;
  STARTUPINFOA si;
  ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));
  ZeroMemory(&si, sizeof(STARTUPINFO));
  si.cb = sizeof(STARTUPINFO);

  std::string cmdline = "cmd /c \"" + batchfile + "\"";

  if (!CreateProcessA(nullptr, &(cmdline[0]), nullptr, nullptr, FALSE, 0,
                      nullptr, ".", &si, &pi)) {
    LOG_SYSRESULT(GetLastError());
    LOG(ERROR) << "Failed to run " << batchfile;
    return -1;
  }
  CloseHandle(pi.hThread);
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD exit_status = 1;
  DWORD result = GetExitCodeProcess(pi.hProcess, &exit_status);
  if (result != TRUE && exit_status == 0) {
    LOG_SYSRESULT(GetLastError());
    LOG(ERROR) << "Failed to get exit code";
    exit_status = 1;
  }
  CloseHandle(pi.hProcess);
  LOG(INFO) << "exit_status:" << exit_status;
  return exit_status;
}

int main(int argc, char** argv) {
  std::vector<std::string> vsvers;
  int argv0 = -1;
  for (int i = 1; i < argc; i++) {
    if (absl::StartsWith(argv[i], "--vsver=")) {
      vsvers.push_back(argv[i] + strlen("--vsver="));
      continue;
    }
    argv0 = i;
    break;
  }
  if (argv0 < 0) {
    std::cerr << "Usage:" << argv[0] << " [--vsver=version] command line..."
              << std::endl;
    exit(1);
  }
  LOG(INFO) << "argv0=" << argv0;
  if (vsvers.empty()) {
    vsvers.push_back("12.0");
  }

  std::set<std::string> vsvars;
  for (const auto& vsver : vsvers) {
    LOG(INFO) << "vsver:" << vsver;
    devtools_goma::GetVSVarsPath(vsver, &vsvars);
  }
  CHECK(!vsvars.empty()) << vsvers;

  for (const auto& iter : vsvars) {
    int r = RunWithVSVars(iter, argv0, argc, argv);
    if (r != 0) {
      LOG(ERROR) << "Failed to run with " << iter;
      exit(r);
    }
  }
  exit(0);
}
