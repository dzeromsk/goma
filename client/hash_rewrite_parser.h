// Copyright 2015 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_HASH_REWRITE_PARSER_H_
#define DEVTOOLS_GOMA_CLIENT_HASH_REWRITE_PARSER_H_

#include <map>
#include <string>

namespace devtools_goma {

// Parsers subprogram's hash rewrite rule.
// The rule format is like:
// <src SHA256 01>:<to SHA256 01>\n
// <src SHA256 02>:<to SHA256 02>\n
// <src SHA256 03>:<to SHA256 02>\n
//
// It returns true if successed to parse. Otherwise false.
// The function update |mapping| based on |contents|.  If it returns false,
// a caller should not use |mapping|.
// Note that duplicate src hash is considered as error.
bool ParseRewriteRule(const std::string& contents,
                      std::map<std::string, std::string>* mapping);

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_HASH_REWRITE_PARSER_H_
