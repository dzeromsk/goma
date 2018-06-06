// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_LIBRARY_PATH_RESOLVER_H_
#define DEVTOOLS_GOMA_CLIENT_LIBRARY_PATH_RESOLVER_H_

#include <string>
#include <vector>

#include "basictypes.h"

using std::string;

namespace devtools_goma {

class LibraryPathResolverTest;
class LinkerInputProcessorTest;

// Expands library name to full path name (e.g. -lfoo => /usr/lib/libfoo.so).
class LibraryPathResolver {
 public:
  explicit LibraryPathResolver(string cwd);
  ~LibraryPathResolver();

  // for -lfoo flag, value is "foo".
  string ExpandLibraryPath(const string& value) const;
  // e.g. soname = "libc.so.6"
  string FindBySoname(const string& soname) const;
  string FindByFullname(const string& fullname) const;
  void PreventSharedLibrary() { static_link_ = true; }
  void SetSyslibroot(const string& path) { syslibroot_ = path; }
  void SetSysroot(const string& path) { sysroot_ = path; }
  void AppendSearchdirs(const std::vector<string>& paths);
  void AddSearchdir(const string& path);

  const std::vector<string>& searchdirs() const { return searchdirs_; }
  const string& cwd() const { return cwd_; }
  const string& sysroot() const { return sysroot_; }
  const string& syslibroot() const { return syslibroot_; }

 private:
  friend class LibraryPathResolverTest;
  friend class LinkerInputProcessorTest;

  string FindByName(const string& so_name, const string& ar_name) const;
  string ResolveLibraryFilePath(
      const string& syslibroot, const string& dirname,
      const string& so_name, const string& ar_name) const;
  string ResolveFilePath(const string& syslibroot, const string& dirname,
                         const string& filename) const;

  std::vector<string> searchdirs_;
  std::vector<string> fallback_searchdirs_;
  const string cwd_;
  bool static_link_;
  // For mac -syslibroot option.
  string syslibroot_;
  string sysroot_;

  static const char* fakeroot_;

  DISALLOW_COPY_AND_ASSIGN(LibraryPathResolver);
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_LIBRARY_PATH_RESOLVER_H_
