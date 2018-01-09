// Copyright 2017 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cpp_macro.h"

#include "autolock_timer.h"

namespace devtools_goma {

namespace {

Lock mu_;
std::vector<std::unique_ptr<MacroEnv>>* macro_env_cache_ GUARDED_BY(mu_);

}  // anonymous namespace

string Macro::DebugString(CppParser* parser, const string& name) const {
  string str;
  str.reserve(64);
  str.append("Macro[");
  str.append(name);
  switch (type) {
    case OBJ:
      str.append("(OBJ)]");
      break;
    case FUNC:
      str.append("(FUNC, args:");
      str.append(std::to_string(num_args));
      if (is_vararg) {
        str.append(", vararg");
      }
      str.append(")]");
      break;
    case CBK:
      str.append("(CALLBACK)]");
      break;
    case CBK_FUNC:
      str.append("(CALLBACK_FUNC)]");
      break;
    case UNDEFINED:
      str.append("(UNDEFINED)]");
      break;
    case UNUSED:
      str.append("(UNUSED)]");
      break;
  }
  str.append(" => ");
  if (callback) {
    str.append((parser->*callback)().DebugString());
  } else {
    for (const auto& iter : replacement) {
      str.append(iter.DebugString());
    }
  }
  return str;
}

void InitMacroEnvCache() {
  AUTOLOCK(lock, &mu_);
  CHECK(macro_env_cache_ == nullptr);
  macro_env_cache_ = new std::vector<std::unique_ptr<MacroEnv>>();
}

void QuitMacroEnvCache() {
  AUTOLOCK(lock, &mu_);
  delete macro_env_cache_;
  macro_env_cache_ = nullptr;
}

std::unique_ptr<MacroEnv> GetMacroEnvFromCache() {
  AUTOLOCK(lock, &mu_);
  if (macro_env_cache_ == nullptr || macro_env_cache_->empty()) {
    return std::unique_ptr<MacroEnv>(new MacroEnv);
  }
  auto macro = std::move(macro_env_cache_->back());
  macro_env_cache_->pop_back();
  return macro;
}

void ReleaseMacroEnvToCache(std::unique_ptr<MacroEnv> macro) {
  AUTOLOCK(lock, &mu_);
  if (macro_env_cache_ == nullptr) {
    return;
  }
  macro_env_cache_->push_back(std::move(macro));
}

}  // namespace devtools_goma
