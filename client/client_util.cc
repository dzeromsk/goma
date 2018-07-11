// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "client_util.h"

#include <deque>

#include "absl/strings/str_split.h"
#include "glog/logging.h"
#include "glog/stl_logging.h"
#include "ioutil.h"
#include "path.h"
#include "path_resolver.h"
#include "util.h"
#include "vc_flags.h"

namespace devtools_goma {

namespace {

// Path separators are platform dependent
#ifndef _WIN32
const char* kPathListSep = ":";
#else
const char* kPathListSep = ";";
#endif

#ifdef _WIN32
std::deque<string> ParsePathExts(const string& pathext_spec) {
  std::vector<string> pathexts;
  if (!pathext_spec.empty()) {
    pathexts =
        ToVector(absl::StrSplit(pathext_spec, kPathListSep, absl::SkipEmpty()));
  } else {
    // If |pathext_spec| is empty, we should use the default PATHEXT.
    // See:
    // http://technet.microsoft.com/en-us/library/cc723564.aspx#XSLTsection127121120120
    static const char* kDefaultPathext = ".COM;.EXE;.BAT;.CMD";
    pathexts = ToVector(
        absl::StrSplit(kDefaultPathext, kPathListSep, absl::SkipEmpty()));
  }

  for (auto& pathext : pathexts) {
    absl::AsciiStrToLower(&pathext);
  }
  return std::deque<string>(pathexts.begin(), pathexts.end());
}

bool HasExecutableExtension(const std::deque<string>& pathexts,
                            const string& filename) {
  const size_t pos = filename.rfind(".");
  if (pos == string::npos)
    return false;

  string ext = filename.substr(pos);
  absl::AsciiStrToLower(&ext);
  for (const auto& pathext : pathexts) {
    if (ext == pathext)
      return true;
  }
  return false;
}

string GetExecutableWithExtension(const std::deque<string>& pathexts,
                                  const string& cwd,
                                  const string& prefix) {
  for (const auto& pathext : pathexts) {
    const string& fullname = prefix + pathext;
    // Do not return cwd prefixed path here.
    const string& candidate = file::JoinPathRespectAbsolute(cwd, fullname);
    DWORD attr = GetFileAttributesA(candidate.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES &&
        (attr & FILE_ATTRIBUTE_DIRECTORY) == 0) {
      return fullname;
    }
  }
  return "";
}
#endif

}  // anonymous namespace

// True if |candidate_path| is gomacc, by running it under an invalid GOMA env
// flag.  It is usually used to confirm |candidate_path| is not gomacc.
// If candadate_path is (a copy of or a symlink to) gomacc, it will die with
// "unknown GOMA_ parameter".
// It assumes real compiler doesn't emit "GOMA" in its output.
// On Windows, path must include a directory where mspdb*.dll,
// otherwise, real cl.exe will pops up a dialog:
//  This application has failed to start because mspdb100.dll was not found.
// Error mode SEM_FAILCRITICALERRORS and SEM_NOGPFAULTERRORBOX
// prevent from popping up message box on error, which we did in
// compiler_proxy.cc:main()
bool IsGomacc(const string& candidate_path,
              const string& path,
              const string& pathext,
              const string& cwd) {
  // TODO: fix workaround.
  // Workaround not to pause with dialog when cl.exe is executed.
  if (VCFlags::IsVCCommand(candidate_path))
    return false;

  std::vector<string> argv;
  argv.push_back(candidate_path);
  std::vector<string> env;
  env.push_back("GOMA_WILL_FAIL_WITH_UKNOWN_FLAG=true");
  env.push_back("PATH=" + path);
  if (!pathext.empty())
    env.push_back("PATHEXT=" + pathext);
  int32_t status = 0;
  string out = ReadCommandOutput(candidate_path, argv, env, cwd,
                                 MERGE_STDOUT_STDERR, &status);
  return (status == 1) && (out.find("GOMA") != string::npos);
}

bool GetRealExecutablePath(const FileStat* gomacc_filestat,
                           const string& cmd,
                           const string& cwd,
                           const string& path_env,
                           const string& pathext_env,
                           string* local_executable_path,
                           string* no_goma_path_env,
                           bool* is_in_relative_path) {
  CHECK(local_executable_path);
#ifndef _WIN32
  DCHECK(pathext_env.empty());
#else
  std::deque<string> pathexts = ParsePathExts(pathext_env);
  if (HasExecutableExtension(pathexts, cmd)) {
    pathexts.push_front("");
  }
#endif

  if (no_goma_path_env)
    *no_goma_path_env = path_env;

  // Fast path.
  // If cmd contains '/', it is just cwd/cmd.
  if (cmd.find_first_of(PathResolver::kPathSep) != string::npos) {
#ifndef _WIN32
    string candidate_fullpath = file::JoinPathRespectAbsolute(cwd, cmd);
    if (access(candidate_fullpath.c_str(), X_OK) != 0)
      return false;
    const string& candidate_path = cmd;
#else
    string candidate_path = GetExecutableWithExtension(pathexts, cwd, cmd);
    if (candidate_path.empty()) {
      LOG(ERROR) << "empty candidate_path from GetExecutableWithExtension"
                 << " pathexts=" << pathexts << " cwd=" << cwd
                 << " cmd=" << cmd;
      return false;
    }
    string candidate_fullpath =
        file::JoinPathRespectAbsolute(cwd, candidate_path);
#endif
    const FileStat candidate_filestat(candidate_fullpath);
    if (is_in_relative_path)
      *is_in_relative_path = !file::IsAbsolutePath(cmd);

    if (!candidate_filestat.IsValid()) {
      LOG(ERROR) << "invalid filestats candidate_path=" << candidate_path
                 << " candidate_fullpath=" << candidate_fullpath;
      return false;
    }

    if (gomacc_filestat && candidate_filestat == *gomacc_filestat)
      return false;

    if (gomacc_filestat &&
        IsGomacc(candidate_fullpath, path_env, pathext_env, cwd))
      return false;

    *local_executable_path = candidate_path;
    return true;
  }

  for (size_t pos = 0, next_pos; pos != string::npos; pos = next_pos) {
    next_pos = path_env.find(kPathListSep, pos);
    absl::string_view dir;
    if (next_pos == absl::string_view::npos) {
      dir = absl::string_view(path_env.c_str() + pos, path_env.size() - pos);
    } else {
      dir = absl::string_view(path_env.c_str() + pos, next_pos - pos);
      ++next_pos;
    }

    if (is_in_relative_path)
      *is_in_relative_path = !file::IsAbsolutePath(dir);

    // Empty paths should be treated as the current directory.
    if (dir.empty()) {
      dir = cwd;
    }
    VLOG(2) << "dir:" << dir;

    string candidate_path(PathResolver::ResolvePath(
        file::JoinPath(file::JoinPathRespectAbsolute(cwd, dir), cmd)));
    VLOG(2) << "candidate:" << candidate_path;

#ifndef _WIN32
    if (access(candidate_path.c_str(), X_OK) != 0)
      continue;
#else
    candidate_path = GetExecutableWithExtension(pathexts, cwd, candidate_path);
    if (candidate_path.empty())
      continue;
#endif
    DCHECK(file::IsAbsolutePath(candidate_path));

    FileStat candidate_filestat(candidate_path);
    if (candidate_filestat.IsValid()) {
      if (gomacc_filestat && candidate_filestat == *gomacc_filestat &&
          next_pos != string::npos) {
        // file is the same as gomacc.
        // Update local path.
        // TODO: drop a path of gomacc only. preserve other paths
        // For example,
        // PATH=c:\P\MVS10.0\Common7\Tools;c:\goma;c:\P\MVS10.0\VC\bin
        // we should not drop c:\P\MVS10.0\Common7\Tools.
        if (no_goma_path_env)
          *no_goma_path_env = path_env.substr(next_pos);
      } else {
        // file is executable, and from file id, it is different
        // from gomacc.
        if (gomacc_filestat &&
            IsGomacc(candidate_path, path_env.substr(pos), pathext_env, cwd)) {
          LOG(ERROR) << "You have 2 goma directories in your path? "
                     << candidate_path << " seems gomacc";
          if (next_pos != string::npos && no_goma_path_env)
            *no_goma_path_env = path_env.substr(next_pos);
          continue;
        }
        *local_executable_path = candidate_path;
        return true;
      }
    }
  }
  return false;
}

#ifdef _WIN32

string ResolveExtension(const string& cmd,
                        const string& pathext_env,
                        const string& cwd) {
  std::deque<string> pathexts = ParsePathExts(pathext_env);
  if (HasExecutableExtension(pathexts, cmd)) {
    pathexts.push_front("");
  }
  return GetExecutableWithExtension(pathexts, cwd, cmd);
}

#endif

}  // namespace devtools_goma
