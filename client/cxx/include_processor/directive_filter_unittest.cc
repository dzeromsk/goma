// Copyright 2013 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "directive_filter.h"

#include <memory>
#include <string>

#include <glog/logging.h>
#include <gtest/gtest.h>

#include "content.h"

using std::string;

namespace devtools_goma {

class DirectiveFilterTest : public testing::Test {
};

TEST_F(DirectiveFilterTest, SkipSpaces) {
  string src = "    12   3 \\\n 4 \\\n\\\n   5  \\\r\n  6  \\\n";
  const char* pos = src.c_str();
  const char* end = src.c_str() + src.size();

  pos = DirectiveFilter::SkipSpaces(pos, end);
  EXPECT_EQ('1', *pos);

  ++pos;
  pos = DirectiveFilter::SkipSpaces(pos, end);
  EXPECT_EQ('2', *pos);

  ++pos;
  pos = DirectiveFilter::SkipSpaces(pos, end);
  EXPECT_EQ('3', *pos);

  ++pos;
  pos = DirectiveFilter::SkipSpaces(pos, end);
  EXPECT_EQ('4', *pos);

  ++pos;
  pos = DirectiveFilter::SkipSpaces(pos, end);
  EXPECT_EQ('5', *pos);

  ++pos;
  pos = DirectiveFilter::SkipSpaces(pos, end);
  EXPECT_EQ('6', *pos);

  ++pos;
  pos = DirectiveFilter::SkipSpaces(pos, end);
  EXPECT_EQ(end, pos);
}

TEST_F(DirectiveFilterTest, NextLineHead) {
  string src = "\n1    \\\n  \n2  \\\n\\\n\\\r\n\n3   \\\r\n";
  const char* pos = src.c_str();
  const char* end = src.c_str() + src.size();

  pos = DirectiveFilter::NextLineHead(pos, end);
  EXPECT_EQ('1', *pos);

  ++pos;
  pos = DirectiveFilter::NextLineHead(pos, end);
  EXPECT_EQ('2', *pos);

  ++pos;
  pos = DirectiveFilter::NextLineHead(pos, end);
  EXPECT_EQ('3', *pos);

  ++pos;
  pos = DirectiveFilter::NextLineHead(pos, end);
  EXPECT_EQ(end, pos);
}

TEST_F(DirectiveFilterTest, RemovesBlockComment) {
  // All comments will be removed.
  string src = "/* foo bar */";
  std::unique_ptr<Content> content(Content::CreateFromString(src));
  std::unique_ptr<Content> filtered(
      DirectiveFilter::MakeFilteredContent(*content));

  EXPECT_EQ("",
            string(filtered->buf(), filtered->buf_end() - filtered->buf()));
}

TEST_F(DirectiveFilterTest, RemoveNonComment) {
  // All comments will be removed.
  string src = "foo bar";
  std::unique_ptr<Content> content(Content::CreateFromString(src));
  std::unique_ptr<Content> filtered(
      DirectiveFilter::MakeFilteredContent(*content));

  EXPECT_EQ("",
            string(filtered->buf(), filtered->buf_end() - filtered->buf()));
}

TEST_F(DirectiveFilterTest, RemovesBlockCommentContainingOnelineComment) {
  string src = "/* // */";
  std::unique_ptr<Content> content(Content::CreateFromString(src));
  std::unique_ptr<Content> filtered(
      DirectiveFilter::MakeFilteredContent(*content));

  EXPECT_EQ("",
            string(filtered->buf(), filtered->buf_end() - filtered->buf()));
}

TEST_F(DirectiveFilterTest, RemovesOnelineComment) {
  string src = "// foo bar";
  std::unique_ptr<Content> content(Content::CreateFromString(src));
  std::unique_ptr<Content> filtered(
      DirectiveFilter::MakeFilteredContent(*content));

  EXPECT_EQ("",
            string(filtered->buf(), filtered->buf_end() - filtered->buf()));
}

TEST_F(DirectiveFilterTest, RemovesOnelineCommentContainingBlockCommentStart1) {
  string src = "// /*";
  std::unique_ptr<Content> content(Content::CreateFromString(src));
  std::unique_ptr<Content> filtered(
      DirectiveFilter::MakeFilteredContent(*content));

  EXPECT_EQ("",
            string(filtered->buf(), filtered->buf_end() - filtered->buf()));
}

TEST_F(DirectiveFilterTest, RemovesOnelineCommentContainingBlockCommentStart2) {
  string src = "// /*\n*/";
  std::unique_ptr<Content> content(Content::CreateFromString(src));
  std::unique_ptr<Content> filtered(
      DirectiveFilter::MakeFilteredContent(*content));

  EXPECT_EQ("",
            string(filtered->buf(), filtered->buf_end() - filtered->buf()));
}

TEST_F(DirectiveFilterTest, RemovesComplexBlockComment) {
  string src = "/*/ #include <iostream> /*/";
  std::unique_ptr<Content> content(Content::CreateFromString(src));
  std::unique_ptr<Content> filtered(
      DirectiveFilter::MakeFilteredContent(*content));

  EXPECT_EQ("",
            string(filtered->buf(), filtered->buf_end() - filtered->buf()));
}

TEST_F(DirectiveFilterTest, BlockCommentIsNotFinished) {
  string src = "/* #include <iostream>";
  std::unique_ptr<Content> content(Content::CreateFromString(src));
  std::unique_ptr<Content> filtered(
      DirectiveFilter::MakeFilteredContent(*content));

  EXPECT_EQ("",
            string(filtered->buf(), filtered->buf_end() - filtered->buf()));
}

TEST_F(DirectiveFilterTest, FilterDirectives) {
  string src =
    "#include <iostream>\n"
    " f(); g(); h(); \n"
    "#include <iomanip>\n";
  std::unique_ptr<Content> content(Content::CreateFromString(src));
  std::unique_ptr<Content> filtered(
      DirectiveFilter::MakeFilteredContent(*content));

  string expected =
    "#include <iostream>\n"
    "#include <iomanip>\n";

  EXPECT_EQ(expected,
            string(filtered->buf(), filtered->buf_end() - filtered->buf()));
}

TEST_F(DirectiveFilterTest, DirectiveIsDividedWithBackslashAndLF) {
  string src =
    "#include \\\n"
    "<iostream>";
  std::unique_ptr<Content> content(Content::CreateFromString(src));
  std::unique_ptr<Content> filtered(
      DirectiveFilter::MakeFilteredContent(*content));

  string expected = "#include <iostream>";

  EXPECT_EQ(expected,
            string(filtered->buf(), filtered->buf_end() - filtered->buf()));
}

TEST_F(DirectiveFilterTest, DirectiveIsDividedWithBackslashAndLFLF) {
  string src =
    "#include \\\n\\\n"
    "<iostream>";
  std::unique_ptr<Content> content(Content::CreateFromString(src));
  std::unique_ptr<Content> filtered(
      DirectiveFilter::MakeFilteredContent(*content));

  string expected = "#include <iostream>";

  EXPECT_EQ(expected,
            string(filtered->buf(), filtered->buf_end() - filtered->buf()));
}

TEST_F(DirectiveFilterTest, DirectiveIsDividedWithBackslashAndCRLF) {
  string src =
    "#include \\\r\n"
    "<iostream>";
  std::unique_ptr<Content> content(Content::CreateFromString(src));
  std::unique_ptr<Content> filtered(
      DirectiveFilter::MakeFilteredContent(*content));

  string expected = "#include <iostream>";

  EXPECT_EQ(expected,
            string(filtered->buf(), filtered->buf_end() - filtered->buf()));
}

TEST_F(DirectiveFilterTest, EmptyLineAndBackslashLFBeforeDirective) {
  string src =
    "                \\\n"
    "#include <iostream>";
  std::unique_ptr<Content> content(Content::CreateFromString(src));
  std::unique_ptr<Content> filtered(
      DirectiveFilter::MakeFilteredContent(*content));

  EXPECT_EQ("#include <iostream>",
            string(filtered->buf(), filtered->buf_end() - filtered->buf()));
}

TEST_F(DirectiveFilterTest, EmptyLineAndBackslashLFLFBeforeDirective) {
  string src =
    "                \\\n\\\n"
    "#include <iostream>";
  std::unique_ptr<Content> content(Content::CreateFromString(src));
  std::unique_ptr<Content> filtered(
      DirectiveFilter::MakeFilteredContent(*content));

  EXPECT_EQ("#include <iostream>",
            string(filtered->buf(), filtered->buf_end() - filtered->buf()));
}

TEST_F(DirectiveFilterTest, EmptyLineAndBackslashCRLFBeforeDirective) {
  string src =
    "                \\\r\n"
    "#include <iostream>";
  std::unique_ptr<Content> content(Content::CreateFromString(src));
  std::unique_ptr<Content> filtered(
      DirectiveFilter::MakeFilteredContent(*content));

  EXPECT_EQ("#include <iostream>",
            string(filtered->buf(), filtered->buf_end() - filtered->buf()));
}

TEST_F(DirectiveFilterTest, DirectiveIsDividedWithComments) {
  string src =
    "#include /*\n"
    " something */\\\n"
    "<iostream>\n";
  std::unique_ptr<Content> content(Content::CreateFromString(src));
  std::unique_ptr<Content> filtered(
      DirectiveFilter::MakeFilteredContent(*content));

  EXPECT_EQ("#include  <iostream>\n",
            string(filtered->buf(), filtered->buf_end() - filtered->buf()));
}

TEST_F(DirectiveFilterTest, FilterDirectivesWithContinuingLines4) {
  string src =
    "      #include <iostream>\n"
    "  #endif\n"
    " #include /* hoge */\n";
  std::unique_ptr<Content> content(Content::CreateFromString(src));
  std::unique_ptr<Content> filtered(
      DirectiveFilter::MakeFilteredContent(*content));

  string expected =
    "#include <iostream>\n"
    "#endif\n"
    "#include  \n";

  EXPECT_EQ(expected,
            string(filtered->buf(), filtered->buf_end() - filtered->buf()));
}

TEST_F(DirectiveFilterTest, DirectiveContainsComments) {
  string src =
    "      #include <iostream>  //\n"
    "  #endif /* \n"
    " #include /* hoge */\n";
  std::unique_ptr<Content> content(Content::CreateFromString(src));
  std::unique_ptr<Content> filtered(
      DirectiveFilter::MakeFilteredContent(*content));

  string expected =
    "#include <iostream>  \n"
    "#endif  \n";

  EXPECT_EQ(expected,
            string(filtered->buf(), filtered->buf_end() - filtered->buf()));
}

TEST_F(DirectiveFilterTest, OneLineCommentContainsBlockComment) {
  string src = "// /* \n#include <iostream>\n";
  std::unique_ptr<Content> content(Content::CreateFromString(src));
  std::unique_ptr<Content> filtered(
      DirectiveFilter::MakeFilteredContent(*content));

  string expected = "#include <iostream>\n";

  EXPECT_EQ(expected,
            string(filtered->buf(), filtered->buf_end() - filtered->buf()));
}

TEST_F(DirectiveFilterTest, IncludePathContainsSlashSlash) {
  string src = "#include \"foo//bar\"\n";
  std::unique_ptr<Content> content(Content::CreateFromString(src));
  std::unique_ptr<Content> filtered(
      DirectiveFilter::MakeFilteredContent(*content));

  string expected = "#include \"foo//bar\"\n";

  EXPECT_EQ(expected,
            string(filtered->buf(), filtered->buf_end() - filtered->buf()));
}

// When just keeping #include containing //, we might miss other comment start.
TEST_F(DirectiveFilterTest, IncludePathContainsSlashSlash2) {
  string src = "#include \"foo//bar\" /*\n hoge */\n";
  std::unique_ptr<Content> content(Content::CreateFromString(src));
  std::unique_ptr<Content> filtered(
      DirectiveFilter::MakeFilteredContent(*content));

  string expected = "#include \"foo//bar\"  \n";

  EXPECT_EQ(expected,
            string(filtered->buf(), filtered->buf_end() - filtered->buf()));
}

// When just keeping #include containing //, we might miss other comment start.
TEST_F(DirectiveFilterTest, IncludePathContainsSlashSlash3) {
  string src = "#include \"foo//bar\" // hoge */\n";
  std::unique_ptr<Content> content(Content::CreateFromString(src));
  std::unique_ptr<Content> filtered(
      DirectiveFilter::MakeFilteredContent(*content));

  string expected = "#include \"foo//bar\" \n";

  EXPECT_EQ(expected,
            string(filtered->buf(), filtered->buf_end() - filtered->buf()));
}

TEST_F(DirectiveFilterTest, StrayDoubleQuotation) {
  string src = "\"\n#include <iostream>\n";
  std::unique_ptr<Content> content(Content::CreateFromString(src));
  std::unique_ptr<Content> filtered(
      DirectiveFilter::MakeFilteredContent(*content));

  string expected = "#include <iostream>\n";

  EXPECT_EQ(expected,
            string(filtered->buf(), filtered->buf_end() - filtered->buf()));
}

TEST_F(DirectiveFilterTest, StrayDoubleQuotation2) {
  string src = "#include <iostream> \"\n";
  std::unique_ptr<Content> content(Content::CreateFromString(src));
  std::unique_ptr<Content> filtered(
      DirectiveFilter::MakeFilteredContent(*content));

  string expected = "#include <iostream> \"\n";

  EXPECT_EQ(expected,
            string(filtered->buf(), filtered->buf_end() - filtered->buf()));
}

TEST_F(DirectiveFilterTest, BlockCommentStartInString) {
  string src = "\"ho/*ge\"\n#include <iostream>\n\"fu*/ga\"";
  std::unique_ptr<Content> content(Content::CreateFromString(src));
  std::unique_ptr<Content> filtered(
      DirectiveFilter::MakeFilteredContent(*content));

  string expected = "#include <iostream>\n";

  EXPECT_EQ(expected,
            string(filtered->buf(), filtered->buf_end() - filtered->buf()));
}

TEST_F(DirectiveFilterTest, LineCommentStartInString) {
  string src = "#define HOGE \"HOGE\\FUGA\"\n";
  std::unique_ptr<Content> content(Content::CreateFromString(src));
  std::unique_ptr<Content> filtered(
      DirectiveFilter::MakeFilteredContent(*content));

  string expected = "#define HOGE \"HOGE\\FUGA\"\n";

  EXPECT_EQ(expected,
            string(filtered->buf(), filtered->buf_end() - filtered->buf()));
}

TEST_F(DirectiveFilterTest, MultipleLineString) {
  string src =
    "#define HOGE \"HOGE\\\n"
    "//\\\"hoge\\\"\\\n"
    "FUGA\"\n";
  std::unique_ptr<Content> content(Content::CreateFromString(src));
  std::unique_ptr<Content> filtered(
      DirectiveFilter::MakeFilteredContent(*content));

  string expected =
      "#define HOGE \"HOGE//\\\"hoge\\\"FUGA\"\n";
  EXPECT_EQ(expected,
            string(filtered->buf(), filtered->buf_end() - filtered->buf()));
}

TEST_F(DirectiveFilterTest, StringContainingDoubleQuotation) {
  string src =
    "#define HOGE \"HOGE\\\"\\\n"
    "//\\\"hoge\\\"\\\n"
    "FUGA\"\n";
  std::unique_ptr<Content> content(Content::CreateFromString(src));
  std::unique_ptr<Content> filtered(
      DirectiveFilter::MakeFilteredContent(*content));

  string expected =
    "#define HOGE \"HOGE\\\"//\\\"hoge\\\"FUGA\"\n";

  EXPECT_EQ(expected,
            string(filtered->buf(), filtered->buf_end() - filtered->buf()));
}

TEST_F(DirectiveFilterTest, MultipleLineDirectiveAndIdentifier) {
  string src =
      "#de\\\n"
      "fi\\\n"
      "ne\\\n"
      " \\\n"
      "H\\\n"
      "OG\\\n"
      "E";
  std::unique_ptr<Content> content(Content::CreateFromString(src));
  std::unique_ptr<Content> filtered(
      DirectiveFilter::MakeFilteredContent(*content));

  string expected =
    "#define HOGE";

  EXPECT_EQ(expected,
            string(filtered->buf(), filtered->buf_end() - filtered->buf()));
}

}  // namespace devtools_goma
