// Copyright 2013 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_CXX_INCLUDE_PROCESSOR_DIRECTIVE_FILTER_H_
#define DEVTOOLS_GOMA_CLIENT_CXX_INCLUDE_PROCESSOR_DIRECTIVE_FILTER_H_

#include <memory>

#include "content.h"
#include "gtest/gtest_prod.h"

namespace devtools_goma {

// DirectiveFilter removes lines
// that do not affect included files from Content.
//
// TODO: Currently we cannot handle #include <foo//bar> correctly.
class DirectiveFilter {
 public:
  // Removes lines that do not affect included files from |content|.
  // The result Content is newly generated.
  static std::unique_ptr<Content> MakeFilteredContent(
      const Content& content);

 private:
  // Returns the pointer to the next non-space character. If nothing, |end| will
  // be returned.
  static const char* SkipSpaces(const char* pos, const char* end);

  // Returns the pointer to the head of the logical next line.
  // A escaped newline (\\\n) is considered.
  static const char* NextLineHead(const char* pos, const char* end);

  // Copies string literal beginning with |pos| to |dst|.
  // Returns how many bytes are copied.
  static int CopyStringLiteral(const char* pos, const char* end, char* dst);

  // If |*pos| points to "\\\n" or "\\\r\n", the number of bytes for
  // escaped newline is returned. Otherwise, 0 is returned.
  // For example, "a" is 0, "\\\n" is 2, and "\\\r\n" is 3.
  static int IsEscapedNewLine(const char* pos, const char* end);

  // Removes comments from |src|. It's OK if |src| and |dst| are the same.
  // |dst| should points at least (end - src) bytes of memory.
  // The size of copied byte is returned.
  static size_t RemoveComments(const char* src, const char* end,
                               char* dst);

  // Remove escaped newlines \\\n and \\\r\n from |src|.
  // It's OK if |src| and |dst| are the same.
  // |dst| should points at least (end - src) bytes of memory.
  // The size of copied byte is returned.
  static size_t RemoveEscapedNewLine(const char* src, const char* end,
                                     char* dst);

  // Removes comments and non-directive lines from |src|. It's OK
  // if |src| and |dst| are the same. |dst| should points at least
  // (end - src) bytes of memory. The size of copied byte is returned.
  static size_t FilterOnlyDirectives(const char* src, const char* end,
                                     char* dst);

  // Removes if/ifdef/ifndef/elif/else/endif/error/pragma directive lines
  // that do not affect included files from |src|.
  // It's OK if |src| and |dst| are the same.
  // |dst| should points at least (end - src) bytes of memory.
  // The size of copied byte is returned.
  static size_t RemoveDeadDirectives(const char* src, const char* end,
                                     char* dst);

  FRIEND_TEST(DirectiveFilterTest, SkipSpaces);
  FRIEND_TEST(DirectiveFilterTest, NextLineHead);
  DISALLOW_COPY_AND_ASSIGN(DirectiveFilter);
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_CXX_INCLUDE_PROCESSOR_DIRECTIVE_FILTER_H_
