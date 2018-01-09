// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
#ifndef _WIN32
#error This module is Windows only
#endif

#include <memory>

#include "vsvars.h"

#include <limits.h>
#include <windows.h>
#include <winbase.h>
#include <winreg.h>

#include <glog/logging.h>
#include <glog/stl_logging.h>

#include "config_win.h"
#include "path.h"
#include "posix_helper_win.h"

namespace {

const char* kVCRegPath[] = {
  "SOFTWARE\\Microsoft\\VisualStudio\\",
  "SOFTWARE\\Wow6432Node\\Microsoft\\VisualStudio\\",
  "SOFTWARE\\Wow6432Node\\Microsoft\\VCExpress\\",
};

}  // anonymous namespace

namespace devtools_goma {

string GetVCInstallDir(const string& reg_path) {
  string install_dir;
  HKEY regKey;
  if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, reg_path.c_str(), 0, KEY_READ, &regKey)
      != ERROR_SUCCESS) {
    LOG_SYSRESULT(GetLastError());
    LOG(ERROR) << "Failed to find regkey for " << reg_path;
    return "";
  }
  DWORD reg_type;
  DWORD data_size = PATH_MAX;
  std::unique_ptr<char[]> data(new char[data_size]);
  DWORD ret = RegQueryValueExA(regKey, "InstallDir", nullptr, &reg_type,
                       reinterpret_cast<LPBYTE>(data.get()), &data_size);
  if (ret == ERROR_SUCCESS) {
    install_dir = string(data.get());
  } else if (ret == ERROR_MORE_DATA) {
    CHECK_GT(data_size, 0U);
    data.reset(new char[data_size]);
    if (RegQueryValueExA(regKey, "InstallDir", nullptr, &reg_type,
                         reinterpret_cast<LPBYTE>(data.get()), &data_size) ==
        ERROR_SUCCESS) {
      install_dir = string(data.get());
    } else {
      LOG_SYSRESULT(GetLastError());
      LOG(ERROR) << "Failed to get InstallDir for " << reg_path;
    }
  } else {
    LOG_SYSRESULT(GetLastError());
    LOG(ERROR) << "Failed to get size of InstallDir for " << reg_path;
  }
  RegCloseKey(regKey);
  return install_dir;
}

void GetVSVarsPath(string vs_version, std::set<string>* vsvars) {
  for (const auto* path : kVCRegPath) {
    string install_dir = GetVCInstallDir(path + vs_version);
    VLOG(1) << "VC " << path << vs_version << " " << install_dir;
    if (!install_dir.empty()) {
      const string tooldir = file::JoinPath(file::JoinPath(install_dir, ".."),
                                            "Tools");
      // TODO: check vsvars64.bat for x64 support?
      string vsvar_path = file::JoinPath(tooldir, "vsvars32.bat");
      if (access(vsvar_path.c_str(), R_OK) == 0) {
        vsvars->insert(vsvar_path);
      } else {
        LOG(ERROR) << "vsvars32.bat not found:" << vsvar_path;
      }
    }
  }
}

}  // namespace devtools_goma
