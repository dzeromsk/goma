// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "library_path_resolver.h"

#ifndef _WIN32
#include <unistd.h>
#endif

#include <iterator>
#include <string>
#include <vector>

#include <glog/logging.h>
#include <glog/stl_logging.h>

#include "path.h"
#include "string_piece_utils.h"

#ifdef _WIN32
#include "posix_helper_win.h"
#endif

using std::string;

namespace devtools_goma {

const char* LibraryPathResolver::fakeroot_ = "";

LibraryPathResolver::LibraryPathResolver(const string& cwd)
    : cwd_(cwd),
      static_link_(false) {
#ifdef __MACH__
  fallback_searchdirs_.push_back("/usr/lib");
  fallback_searchdirs_.push_back("/usr/local/lib");
#endif
}

LibraryPathResolver::~LibraryPathResolver() {
}

string LibraryPathResolver::ExpandLibraryPath(const string& value) const {
  string lib_name = "lib";
#ifdef __MACH__
  string so_name = lib_name + value + ".dylib";
  string ar_name = lib_name + value + ".a";
  // See: linker manual of Mac (-lx).
  if (strings::EndsWith(value, ".o")) {
    so_name = value;
    ar_name = value;
  }
#elif defined(_WIN32)
  StringPiece ext = file::Extension(value);
  string so_name = value;
  if (ext != "tlb") {
    so_name = value + ".tlb";
  }

  string ar_name = value;
  if (ext != "lib") {
    ar_name = value + ".lib";
  }
#else
  string so_name = lib_name + value + ".so";
  string ar_name = lib_name + value + ".a";
  // See: GNU linker manual (-l namespace).
  if (strings::StartsWith(value, ":")) {
    so_name = ar_name = value.substr(1);
  }
#endif
  string pathname = FindByName(so_name, ar_name);
  if (pathname.empty()) {
    LOG(INFO) << "-l" << value << " not found in " << searchdirs_;
  }
  return pathname;
}

string LibraryPathResolver::FindBySoname(const string& soname) const {
  return FindByName(soname, "");
}

string LibraryPathResolver::ResolveLibraryFilePath(
    const string& syslibroot, const string& dirname,
    const string& so_name, const string& ar_name) const {
  if (!static_link_) {
    const string filename = fakeroot_ +
        file::JoinPath(syslibroot,
            file::JoinPathRespectAbsolute(
              file::JoinPathRespectAbsolute(cwd_, dirname),
              so_name));
    VLOG(2) << "check:" << filename;
    if (access(filename.c_str(), R_OK) == 0)
      return filename.substr(strlen(fakeroot_));
  }
  if (ar_name.empty())
    return "";
  const string filename = fakeroot_ +
      file::JoinPath(syslibroot,
          file::JoinPathRespectAbsolute(
            file::JoinPathRespectAbsolute(cwd_, dirname),
            ar_name));
  VLOG(2) << "check:" << filename;
  if (access(filename.c_str(), R_OK) == 0)
    return filename.substr(strlen(fakeroot_));

  return "";
}

string LibraryPathResolver::FindByName(const string& so_name,
                                       const string& ar_name) const {
  for (const auto& dir : searchdirs_) {
    // Inspite of ld(1) manual, ld won't prepend syslibroot to -L options.
    // I have checked it with dtruss(1).
    const string filename = ResolveLibraryFilePath(
        "", dir, so_name, ar_name);
    if (!filename.empty())
      return filename;
  }

  for (const auto& dir : fallback_searchdirs_) {
    const string filename = ResolveLibraryFilePath(
        syslibroot_, dir, so_name, ar_name);
    if (!filename.empty())
      return filename;
  }

  return "";
}

string LibraryPathResolver::ResolveFilePath(
    const string& syslibroot, const string& dirname,
    const string& basename) const {
  const string filename = fakeroot_ +
      file::JoinPath(
          syslibroot,
          file::JoinPath(file::JoinPathRespectAbsolute(cwd_, dirname),
                         basename));
  VLOG(2) << "check:" << filename;
  if (access(filename.c_str(), R_OK) == 0)
    return filename.substr(strlen(fakeroot_));

  return "";
}

string LibraryPathResolver::FindByFullname(const string& name) const {
  {
    string filename = fakeroot_ + file::JoinPathRespectAbsolute(cwd_, name);
    VLOG(2) << "check:" << filename;
    if (access(filename.c_str(), R_OK) == 0)
      return filename.substr(strlen(fakeroot_));
  }

  const string search_name = string(file::Basename(name));
  for (const auto& dir : searchdirs_) {
    // Inspite of ld(1) manual, ld won't prepend syslibroot to -L options.
    const string filename = ResolveFilePath("", dir, search_name);
    if (!filename.empty())
      return filename;
  }

  for (const auto& dir : fallback_searchdirs_) {
    const string filename = ResolveFilePath(
        syslibroot_, dir, search_name);
    if (!filename.empty())
      return filename;
  }

  return "";
}

void LibraryPathResolver::AppendSearchdirs(
    const std::vector<string>& searchdirs) {
  copy(searchdirs.begin(), searchdirs.end(), back_inserter(searchdirs_));
}

void LibraryPathResolver::AddSearchdir(const string& searchdir) {
  searchdirs_.push_back(searchdir);
}


}  // namespace devtools_goma
