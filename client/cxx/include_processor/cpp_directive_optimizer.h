// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_CXX_INCLUDE_PROCESSOR_CPP_DIRECTIVE_OPTIMIZER_H_
#define DEVTOOLS_GOMA_CLIENT_CXX_INCLUDE_PROCESSOR_CPP_DIRECTIVE_OPTIMIZER_H_

#include <ostream>

#include "atomic_stats_counter.h"
#include "cpp_directive.h"

namespace devtools_goma {

// Optimize CppDirectiveList so that CppParser can evaluate it more quickly.
class CppDirectiveOptimizer {
 public:
  static void Optimize(CppDirectiveList* directives);

  static void DumpStats(std::ostream* os);

 private:
  static StatsCounter total_directives_count_;
  static StatsCounter if_directives_count_;
  static StatsCounter converted_count_;
  static StatsCounter dropped_count_;
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_CXX_INCLUDE_PROCESSOR_CPP_DIRECTIVE_OPTIMIZER_H_
