// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_CXX_INCLUDE_PROCESSOR_CPP_MACRO_ENV_H_
#define DEVTOOLS_GOMA_CLIENT_CXX_INCLUDE_PROCESSOR_CPP_MACRO_ENV_H_

#include <string>

#include "absl/container/flat_hash_map.h"
#include "cpp_macro.h"

using std::string;

namespace devtools_goma {

class CppMacroEnv {
 public:
  using UnderlyingMapType =
      absl::flat_hash_map<absl::string_view, const Macro*>;

  // Add |macro| to map.
  // If the same name macro exists, |macro| overrides the existing one,
  // and the old macro is returned. nullptr if not.
  const Macro* Add(const Macro* macro) {
    absl::string_view name = macro->name;
    auto p = env_.emplace(name, macro);
    if (p.second) {
      // no existing macro.
      return nullptr;
    }

    const Macro* existing_macro = p.first->second;

    // Be careful. key must be always the view of macro name.
    // Otherwise, string_view won't be alive.
    // So, we need to erase & insert to update key.
    env_.erase(p.first);
    env_.emplace(name, macro);

    return existing_macro;
  }

  // Get a macro by |name|.
  const Macro* Get(const string& name) const {
    auto it = env_.find(name);
    if (it == env_.end()) {
      return nullptr;
    }

    return it->second;
  }

  // Delete a macro by name.
  // The deleted macro is returned.
  const Macro* Delete(const string& name) {
    auto it = env_.find(name);
    if (it == env_.end()) {
      return nullptr;
    }

    const Macro* existing = it->second;
    env_.erase(it);
    return existing;
  }

  // Returns the underlying map. for dump, debug, etc.
  const UnderlyingMapType& UnderlyingMap() const { return env_; }

 private:
  UnderlyingMapType env_;
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_CXX_INCLUDE_PROCESSOR_CPP_MACRO_ENV_H_
