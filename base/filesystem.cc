// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "filesystem.h"

#ifndef _WIN32
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#else
#include "config_win.h"
#endif

#include <fstream>

#include "file_dir.h"
#include "glog/logging.h"
#include "path.h"

using std::string;

namespace file {

#ifdef _WIN32
// unlink is deprecated on Win. Use _unlink.
#define unlink _unlink
#endif  // _WIN32

::util::Status RecursivelyDelete(absl::string_view path,
                                 const file::Options& options) {
  string name(path);

  // TODO: rewrite non recursive like devtools/goma/server/dirutil.cc?
  std::vector<devtools_goma::DirEntry> entries;
  if (!devtools_goma::ListDirectory(name, &entries)) {
    return ::util::Status(false);
  }
  if (entries.empty()) {
    if (unlink(name.c_str()) != 0) {
      return ::util::Status(false);
    }
  }
  for (const auto& ent : entries) {
    if (ent.name == "." || ent.name == "..") {
      continue;
    }
    const string& filename = file::JoinPath(name, ent.name);
    if (ent.is_dir) {
      ::util::Status status = RecursivelyDelete(filename, options);
      if (!status.ok()) {
        return status;
      }
    } else {
      if (unlink(filename.c_str()) != 0) {
        return ::util::Status(false);
      }
    }
  }
  if (!devtools_goma::DeleteDirectory(name)) {
    return ::util::Status(false);
  }
  return ::util::Status(true);
}

::util::Status IsDirectory(absl::string_view path, const file::Options&) {
  string str = string(path);
#ifndef _WIN32
  struct stat st;
  if (stat(str.c_str(), &st) == 0) {
    return ::util::Status(S_ISDIR(st.st_mode));
  }
  return ::util::Status(false);
#else
  DWORD attr = GetFileAttributesA(str.c_str());
  if (attr == INVALID_FILE_ATTRIBUTES)
    return ::util::Status(false);
  return ::util::Status((attr & FILE_ATTRIBUTE_DIRECTORY) ==
                        FILE_ATTRIBUTE_DIRECTORY);
#endif
}

::util::Status CreateDir(absl::string_view path, const file::Options& options) {
  string cpath(path);
#ifndef _WIN32
  int r = mkdir(cpath.c_str(), options.creation_mode());
  if (r < 0) {
    PLOG(ERROR) << "CreateDir failed: " << path;
    return ::util::Status(false);
  }
#else
  UNREFERENCED_PARAMETER(options);
  if (!CreateDirectoryA(cpath.c_str(), nullptr)) {
    DWORD err = GetLastError();
    LOG(ERROR) << "CreateDir failed: " << cpath << ": " << err;
    LOG_SYSRESULT(err);
    return ::util::Status(false);
  }
#endif
  return ::util::Status(true);
}

::util::Status Copy(absl::string_view from,
                    absl::string_view to,
                    const Options& options) {
  string cfrom(from);
  string cto(to);

#ifdef _WIN32
  if (!CopyFileA(cfrom.c_str(), cto.c_str(), !options.overwrite())) {
    DWORD err = GetLastError();
    LOG_SYSRESULT(err);
    LOG(WARNING) << "failed to copy file:"
                 << " from=" << from << " to=" << to;
    return ::util::Status(false);
  }

  return ::util::Status(true);
#else
  std::ifstream ifs(cfrom, std::ifstream::binary);
  if (!ifs) {
    LOG(WARNING) << "Input file not found: " << from;
    return ::util::Status(false);
  }

  struct stat stat_buf;
  if (!options.overwrite() && (0 == stat(cto.c_str(), &stat_buf))) {
    LOG(ERROR) << "File " << to << " exists and overwrite is disabled";
    return ::util::Status(false);
  }

  std::ofstream ofs(cto, std::ofstream::binary);
  if (!ofs) {
    LOG(WARNING) << "Cannot open output file: " << to;
    return ::util::Status(false);
  }

  ::util::Status status(true);
  while (!ifs.eof()) {
    char buf[4096];
    ifs.read(buf, sizeof(buf));
    if (ifs.fail() && !ifs.eof()) {
      LOG(WARNING) << "Failed to read file from: " << from;
      return ::util::Status(false);
    }
    ofs.write(buf, ifs.gcount());
  }

  return ::util::Status(true);
#endif
}

}  // namespace file
