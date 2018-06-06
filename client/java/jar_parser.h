// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_JAVA_JAR_PARSER_H_
#define DEVTOOLS_GOMA_CLIENT_JAVA_JAR_PARSER_H_

#include <set>
#include <string>
#include <vector>

using std::string;

namespace devtools_goma {

class JarParser {
 public:
  JarParser();

  // Reads |input_jar_files| and push required jar files into |jar_files|.
  // TODO: We may want to return additional class pathes as well.
  void GetJarFiles(const std::vector<string>& input_jar_files,
                   const string& cwd,
                   std::set<string>* jar_files);
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_JAVA_JAR_PARSER_H_
