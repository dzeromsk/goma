// Copyright 2013 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_FRAMEWORK_PATH_RESOLVER_H_
#define DEVTOOLS_GOMA_CLIENT_FRAMEWORK_PATH_RESOLVER_H_

#include <string>
#include <vector>

#include "basictypes.h"

using std::string;

namespace devtools_goma {

class FrameworkPathResolver {
 public:
  explicit FrameworkPathResolver(const string& cwd);
  ~FrameworkPathResolver() {}

  // Returns list of files in the framework.
  string ExpandFrameworkPath(const string& framework) const;
  void SetSyslibroot(const string& syslibroot) {
    syslibroot_ = syslibroot;
  }
  void AppendSearchpaths(const std::vector<string>& searchpaths);

 private:
  string FrameworkFile(const string& syslibroot, const string& dirname,
                       const string& name,
                       const std::vector<string>& candidates) const;

  const string cwd_;
  string syslibroot_;
  std::vector<string> searchpaths_;
  std::vector<string> default_searchpaths_;

  DISALLOW_COPY_AND_ASSIGN(FrameworkPathResolver);
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_FRAMEWORK_PATH_RESOLVER_H_
