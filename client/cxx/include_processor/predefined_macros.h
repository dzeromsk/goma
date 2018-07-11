// Copyright 2015 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_CXX_INCLUDE_PROCESSOR_PREDEFINED_MACROS_H_
#define DEVTOOLS_GOMA_CLIENT_CXX_INCLUDE_PROCESSOR_PREDEFINED_MACROS_H_

#include "basictypes.h"

namespace devtools_goma {

const char* const kPredefinedObjectMacros[] = {
  "__FILE__",
  "__LINE__",
  "__DATE__",
  "__TIME__",
  "__COUNTER__",
  "__BASE_FILE__",
};
const int kPredefinedObjectMacroSize =
    arraysize(kPredefinedObjectMacros);

const char* const kPredefinedFunctionMacros[] = {
  "__has_include",
  "__has_include__",
  "__has_include_next",
  "__has_include_next__",
  "__has_feature",
  "__has_extension",
  "__has_attribute",
  "__has_cpp_attribute",
  "__has_declspec_attribute",
  "__has_builtin",
};
const int kPredefinedFunctionMacroSize =
    arraysize(kPredefinedFunctionMacros);

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_CXX_INCLUDE_PROCESSOR_PREDEFINED_MACROS_H_
