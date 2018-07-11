// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "java_execreq_normalizer.h"

#include "glog/logging.h"
using std::string;

namespace devtools_goma {

ConfigurableExecReqNormalizer::Config JavacExecReqNormalizer::Configure(
    int id,
    const std::vector<string>& args,
    bool normalize_include_path,
    bool is_linking,
    const std::vector<string>& normalize_weak_relative_for_arg,
    const std::map<string, string>& debug_prefix_map,
    const ExecReq* req) const {
  if (is_linking) {
    return Config::AsIs();
  }

  Config config;
  config.keep_cwd = kOmit;
  // It would be OK to normalize args (e.g. in classname) for Javac.
  // However, currently normalizer considers only gcc (clang) args.
  // So, don't normalize.
  config.keep_args = kAsIs;
  config.keep_pathnames_in_input = kNormalizeWithCwd;
  config.keep_system_include_dirs = kOmit;

  // Dropping pathnames may generate same hash from different input.
  CHECK(!(config.keep_pathnames_in_input & kOmit));
  return config;
}

ConfigurableExecReqNormalizer::Config JavaExecReqNormalizer::Configure(
    int id,
    const std::vector<string>& args,
    bool normalize_include_path,
    bool is_linking,
    const std::vector<string>& normalize_weak_relative_for_arg,
    const std::map<string, string>& debug_prefix_map,
    const ExecReq* req) const {
  return Config::AsIs();
}

}  // namespace devtools_goma
