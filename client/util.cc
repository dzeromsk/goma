// Copyright 2010 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "util.h"

#include <algorithm>
#include <deque>

#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "env_flags.h"
#include "file_stat.h"
#include "glog/logging.h"
#include "glog/stl_logging.h"
#include "path.h"
#include "path_resolver.h"

using std::string;

namespace devtools_goma {

static ReadCommandOutputFunc gReadCommandOutput = nullptr;

void InstallReadCommandOutputFunc(ReadCommandOutputFunc func) {
  gReadCommandOutput = func;
}

string ReadCommandOutput(
    const string& prog, const std::vector<string>& argv,
    const std::vector<string>& env,
    const string& cwd, CommandOutputOption option, int32_t* status) {
  if (gReadCommandOutput == nullptr) {
    LOG(FATAL) << "gReadCommandOutput should be set before calling."
               << " prog=" << prog
               << " cwd=" << cwd
               << " argv=" << argv
               << " env=" << env;
  }
  return gReadCommandOutput(prog, argv, env, cwd, option, status);
}

// Platform independent getenv.
string GetEnv(const string& name) {
#ifndef _WIN32
  char* ret = getenv(name.c_str());
  if (ret == nullptr)
    return "";
  return ret;
#else
  DWORD size = GetEnvironmentVariableA(name.c_str(), nullptr, 0);
  if (size == 0) {
    CHECK(GetLastError() == ERROR_ENVVAR_NOT_FOUND);
    return "";
  }
  string envvar(size, '\0');
  DWORD ret = GetEnvironmentVariableA(name.c_str(), &envvar[0], size);
  CHECK_EQ(ret, size - 1)
      << "GetEnvironmentVariableA failed but should not:" << name
      << " ret=" << ret << " size=" << size;
  CHECK_EQ(envvar[ret], '\0');
  // cut off the null-terminating character.
  return envvar.substr(0, ret);
#endif
}

void SetEnv(const string& name, const string& value) {
#ifndef _WIN32
  if (setenv(name.c_str(), value.c_str(), 1) != 0) {
    PLOG(ERROR) << "setenv name=" << name << " value=" << value;
  }
#else
  BOOL ret = SetEnvironmentVariableA(name.c_str(), value.c_str());
  if (!ret) {
    LOG_SYSRESULT(GetLastError());
    LOG(ERROR) << "setenv name=" << name << " value=" << value;
  }
#endif
}

pid_t Getpid() {
#ifdef _WIN32
  return static_cast<pid_t>(::GetCurrentProcessId());
#else
  return getpid();
#endif
}

string ToShortNodename(const string& nodename) {
  std::vector<string> entries = ToVector(absl::StrSplit(nodename, '.'));
  return absl::AsciiStrToLower(entries[0]);
}

int64_t SumRepeatedInt32(
    const google::protobuf::RepeatedField<google::protobuf::int32>& input) {
  int64_t sum = 0;
  for (int32_t input_value : input) { sum += input_value; }
  return sum;
}

}  // namespace devtools_goma
