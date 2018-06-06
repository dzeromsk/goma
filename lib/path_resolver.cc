// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.



#include "path_resolver.h"

#include <limits.h>
#include <stdlib.h>

#include <algorithm>
#include <cctype>
#include <functional>
#include <locale>


#include "absl/container/inlined_vector.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "glog/logging.h"
#include "path_util.h"
using std::string;

namespace {

void trim(string* s) {
  s->erase(s->begin(),
           std::find_if(s->begin(), s->end(),
                        std::not1(std::ptr_fun<int, int>(std::isspace))));
  s->erase(std::find_if(s->rbegin(), s->rend(),
                        std::not1(std::ptr_fun<int, int>(std::isspace))).base(),
                        s->end());
}

// Get the separator position where the UNC/drive letters end and the path
// part begins.
string::size_type GetDrivePrefixPosition(absl::string_view path) {
  if (path.size() < 2) return 0;
  absl::string_view preserve = path.substr(0, 2);
  bool is_unc = (preserve == "\\\\");
  if (!is_unc && preserve[1] != ':')
    return 0;
  if (!is_unc)
    return 2;
  string::size_type pos = absl::ClippedSubstr(path, 2).find('\\');
  return (pos == string::npos) ? path.size() : pos + 2;
}

// Separate UNC/drive letter from path so that path operations can be done
// correctly.  The UNC/drive letter will be kept in |preserve|, and the
// path relative to topmost level (i.e. UNC host/drive letter) is in
// |resolved_path|.
void SeparatePath(string* preserve, string* resolved_path) {
  *preserve = resolved_path->substr(0, 2);
  bool is_unc = (strcmp(preserve->c_str(), "\\\\") == 0);
  if (!is_unc && (*preserve)[1] != ':') {
    preserve->clear();
  } else {
    *resolved_path = resolved_path->substr(2);
    if (is_unc) {  // we need to preserve \\host
      string::size_type pos = resolved_path->find('\\');
      if (pos == string::npos) {
        *preserve += *resolved_path;
        resolved_path->clear();
      } else {
        *preserve += resolved_path->substr(0, pos);
        *resolved_path = resolved_path->substr(pos);
      }
    }
  }
}

}  // namespace

namespace devtools_goma {

#ifndef _WIN32
const char PathResolver::kPathSep = '/';
#else
const char PathResolver::kPathSep = '\\';
#endif

PathResolver::PathResolver() {
}

PathResolver::~PathResolver() {
}

string PathResolver::PlatformConvert(const string& path) {
  string OUTPUT;
  PlatformConvertToString(path, &OUTPUT);
  return OUTPUT;
}

void PathResolver::PlatformConvertToString(const string& path,
                                           string* OUTPUT) {
#ifdef _WIN32
  PlatformConvertToString(path,
                          PathResolver::kWin32PathSep,
                          PathResolver::kPreserveCase,
                          OUTPUT);
#else
  PlatformConvertToString(path,
                          PathResolver::kPosixPathSep,
                          PathResolver::kPreserveCase,
                          OUTPUT);
#endif
}

string PathResolver::PlatformConvert(
    const string& path, PathResolver::PathSeparatorType sep_type,
    PathResolver::PathCaseType case_type) {
  string OUTPUT;
  PlatformConvertToString(path, sep_type, case_type, &OUTPUT);
  return OUTPUT;
}

void PathResolver::PlatformConvertToString(
    const string& path, PathResolver::PathSeparatorType sep_type,
    PathResolver::PathCaseType case_type, string* OUTPUT) {
  // TODO: use Chrome base FilePath object, which has everything
  //                  we need and is much better than the hack below.
  *OUTPUT = path;
  trim(OUTPUT);

  if (sep_type == PathResolver::kWin32PathSep) {
    std::replace(OUTPUT->begin(), OUTPUT->end(), '/', '\\');
    if (OUTPUT->size() > 2) {
      string::size_type pos = 2;
      while (pos < OUTPUT->size() && pos != string::npos) {
        pos = OUTPUT->find("\\\\", pos);
        if (pos != string::npos) {
          OUTPUT->replace(pos, strlen("\\\\"), string("\\"));
          pos += strlen("\\");
        }
      }
    }
  } else {
#ifdef _WIN32
    LOG(FATAL) << "Unsupported";
#endif
    std::replace(OUTPUT->begin(), OUTPUT->end(), '\\', '/');
  }

  if (case_type == PathResolver::kLowerCase)
    absl::AsciiStrToLower(OUTPUT);
}

string PathResolver::ResolvePath(absl::string_view path) {
#ifndef _WIN32
  return PathResolver::ResolvePath(path, kPosixPathSep);
#else
  return PathResolver::ResolvePath(path, kWin32PathSep);
#endif
}

// TODO: This does similar path conversion to PlatformConvert inline.
// Probably we should also (or rather) improve the method too.
/* static */
string PathResolver::ResolvePath(absl::string_view path,
                                 PathSeparatorType sep_type) {
  // Note: Windows PathCanonicalize() API has different behavior than
  //       what's expected, so we'll do a lot of due dilligence here.
  absl::string_view buf(path);
  absl::InlinedVector<char, 1024> ibuf;

  if (sep_type == kWin32PathSep && path.find('/') != string::npos) {
    // Normalize path separator.
    ibuf.assign(path.begin(), path.end());
    std::replace(ibuf.begin(), ibuf.end(), '/', '\\');
    buf = absl::string_view(ibuf.begin(), ibuf.size());
  }

  string resolved_path;
  resolved_path.reserve(path.size());

  char sep_char = '/';
  if (sep_type == kPosixPathSep) {
  } else if (sep_type == kWin32PathSep) {
    sep_char = '\\';
    // Split UNC paths and drive letter.
    string::size_type drive_position = GetDrivePrefixPosition(buf);
    resolved_path.append(buf.begin(), drive_position);
    if (drive_position == buf.size()) {
      return resolved_path;
    }
    buf = absl::ClippedSubstr(buf, drive_position);
  } else {
    LOG(ERROR) << "Unknown sep_type=" << sep_type;
    return string(path);
  }

  size_t found = 0;
  bool is_absolute = buf[0] == sep_char;
  absl::InlinedVector<absl::string_view, 32> components;

  do {
    found = buf.find(sep_char);
    absl::string_view component = buf.substr(0, found);
    buf.remove_prefix(found + 1);
    if (component.empty() || component == ".") {
      continue;
    }
    if (component == ".." && (!components.empty() || is_absolute)) {
      if (!components.empty() && components.back() == "..") {
        components.push_back("..");
      } else if (!components.empty()) {
        components.pop_back();
      }
      continue;
    }
    components.push_back(component);
  } while (found != string::npos);

  if (is_absolute) {
    resolved_path.push_back(sep_type);
  }
  if (components.empty()) {
    return resolved_path;
  }
  resolved_path.append(components[0].begin(), components[0].size());
  for (size_t i = 1; i < components.size(); ++i) {
    resolved_path.push_back(sep_type);
    resolved_path.append(components[i].begin(), components[i].size());
  }
  return resolved_path;
}

/* static */
string PathResolver::WeakRelativePath(
    const string& raw_path, const string& raw_cwd) {
  // Note: Windows PathRelativePathTo() API has a way different behavior than
  //       what's expected, so we'll do a lot of due dilligence here.
  PathSeparatorType sep_type;
  if (IsPosixAbsolutePath(raw_cwd)) {
    sep_type = kPosixPathSep;
  } else if (IsWindowsAbsolutePath(raw_cwd)) {
    sep_type = kWin32PathSep;
  } else {
    LOG(ERROR) << "Unknown path type given to raw_cwd=" << raw_cwd;
    return raw_path;
  }

  string path = raw_path;
  string cwd = raw_cwd;
  if (sep_type == kWin32PathSep) {
    PlatformConvertToString(raw_path,
                            kWin32PathSep,
                            kPreserveCase,
                            &path);
    PlatformConvertToString(raw_cwd,
                            kWin32PathSep,
                            kPreserveCase,
                            &cwd);
  }

  if (sep_type == kPosixPathSep && !IsPosixAbsolutePath(path)) {
    return path;
  }

  string preserve_path;
  if (sep_type == kWin32PathSep) {
    if (!IsWindowsAbsolutePath(path)) {
      return path;
    }

    SeparatePath(&preserve_path, &path);
    string preserve_cwd;
    SeparatePath(&preserve_cwd, &cwd);
    if (preserve_path != preserve_cwd) {
      return preserve_path + path;
    }
  }

  string resolved_cwd = ResolvePath(cwd, sep_type);
  absl::string_view real_cwd = resolved_cwd;
  CHECK_EQ(real_cwd[0], sep_type)
      << "expect real_cwd[0] == sep_type"
      << " real_cwd=" << real_cwd
      << " sep_type=" << sep_type;
  // Don't resolve path for some case:
  //  cwd = "/tmp"
  //  path = "/tmp/foo/../bar"
  //  /tmp/foo -> /var/tmp/foo
  // if path is resolved, we'll get "bar" in /tmp.
  // but it should be /var/tmp/bar.
  // it might failed some cases, but we'll take safer option here.
  absl::string_view target = path;
  CHECK_EQ(target[0], sep_type);
  if (target == real_cwd)
    return ".";

  if (HasPrefixDirWithSep(target, real_cwd, sep_type)) {
    target.remove_prefix(real_cwd.size() + 1);
    return string(target);
  }
  size_t found;
  size_t last_slash = 0;
  while ((found = real_cwd.find(sep_type, last_slash + 1)) != string::npos) {
    if (real_cwd.substr(0, found) == target.substr(0, found)) {
      last_slash = found;
      continue;
    }
    // mismatch path component.
    break;
  }
  if (last_slash == 0) {
    // If it shares only /, use absolute path instead of relative.
    // e.g. $HOME/src vs /tmp
    if (sep_type == kWin32PathSep && target == path) {
      path = preserve_path + path;
      return path;
    }
    return string(target);
  }
  target = absl::ClippedSubstr(target, last_slash + 1);
  int depth = 1;
  found = last_slash;
  while ((found = real_cwd.find(sep_type, found + 1)) != string::npos) {
    ++depth;
  }
  string relative_path;
  relative_path.reserve(depth * 3 + target.size());
  for (int i = 0; i < depth; ++i) {
    relative_path += "..";
    relative_path += sep_type;
  }
  relative_path += string(target);

  if (sep_type == kWin32PathSep && relative_path == path) {
    relative_path = preserve_path + relative_path;
  }

  return relative_path;
}

bool PathResolver::IsSystemPath(const string& raw_path) const {
#ifndef _WIN32
  const string& path = raw_path;
#else
  string path = PlatformConvert(raw_path);
#endif

  for (const auto& iter : system_paths_) {
    if (absl::StartsWith(path, iter))
      return true;
  }
  return false;
}

void PathResolver::RegisterSystemPath(const string& raw_path) {
#ifndef _WIN32
  const string& path = raw_path;
#else
  string path = PlatformConvert(raw_path);
#endif

  system_paths_.push_back(path);
}

}  // namespace devtools_goma
