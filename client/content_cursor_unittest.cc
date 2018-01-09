// Copyright 2017 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content_cursor.h"

#include <gtest/gtest.h>

#include <cstring>
#include <memory>

namespace {

std::unique_ptr<devtools_goma::Content> MakeNonNulTerminatedContent() {
  std::unique_ptr<char[]> buf(new char[11]);
  strcpy(buf.get(), "0123456789");

  // Taking the first 5 characters only.
  // Currently CreateFromUnique might make a Content that does not end with
  // '\0'. The other methods add '\0'.
  return devtools_goma::Content::CreateFromUnique(std::move(buf), 5);
}

}  // anonymous namespace

namespace devtools_goma {

TEST(ContentCursorTest, Advance) {
  ContentCursor c(Content::CreateFromString("0123456789"));

  EXPECT_TRUE(c.Advance(3));
  EXPECT_EQ(c.cur() - c.buf(), 3);

  // Jump to the end. This is OK.
  EXPECT_TRUE(c.Advance(7));
  EXPECT_EQ(c.cur() - c.buf(), 10);

  // Cannot overrun.
  EXPECT_FALSE(c.Advance(1));
  EXPECT_EQ(c.cur() - c.buf(), 10);

  // Advancing 0 is allowed.
  EXPECT_TRUE(c.Advance(0));
  EXPECT_EQ(c.cur() - c.buf(), 10);
}

TEST(ContentCursorTest, SkipUntil) {
  {
    ContentCursor c(MakeNonNulTerminatedContent());
    ASSERT_FALSE(c.SkipUntil('\0'));
  }

  {
    ContentCursor c(MakeNonNulTerminatedContent());
    EXPECT_TRUE(c.SkipUntil('2'));
    EXPECT_EQ(*c.cur(), '2');

    // Check |cur| index to avoid string comparison.
    EXPECT_EQ(c.cur() - c.buf(), 2);
  }

  {
    ContentCursor c(MakeNonNulTerminatedContent());
    EXPECT_TRUE(c.SkipUntil('4'));
    EXPECT_EQ(*c.cur(), '4');
    EXPECT_EQ(c.cur() - c.buf(), 4);
  }

  // '5' should be out of range.
  {
    ContentCursor c(MakeNonNulTerminatedContent());
    EXPECT_FALSE(c.SkipUntil('5'));
    EXPECT_EQ(c.cur() - c.buf(), c.buf_end() - c.buf());
  }

  // '7' should be out of range.
  {
    ContentCursor c(MakeNonNulTerminatedContent());
    EXPECT_FALSE(c.SkipUntil('7'));
    EXPECT_EQ(c.cur() - c.buf(), c.buf_end() - c.buf());
  }
}

TEST(ContentCursorTest, SkipUntilEvil) {
  std::unique_ptr<char[]> buf(new char[11]);
  strcpy(buf.get(), "0123456789");
  buf[3] = '\0';  // \0 in the Content.

  ContentCursor c(Content::CreateFromUnique(std::move(buf), 5));
  EXPECT_TRUE(c.SkipUntil('4'));
  EXPECT_EQ(c.cur() - c.buf(), 4);
}

}  // namespace devtools_goma
