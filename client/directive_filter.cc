// Copyright 2013 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "directive_filter.h"

#ifdef TEST
#include <stdio.h>
#endif
#include <string.h>

#include <memory>
#include <vector>

#include "content.h"
#include "glog/logging.h"
#include "string_piece.h"
#include "string_piece_utils.h"

using std::string;

namespace devtools_goma {

// static
std::unique_ptr<Content> DirectiveFilter::MakeFilteredContent(
    const Content& content) {
  const size_t content_length = content.size();
  std::unique_ptr<char[]> buffer(new char[content_length + 1]);

  size_t length = RemoveComments(content.buf(), content.buf_end(),
                                 buffer.get());

  length = FilterOnlyDirectives(buffer.get(), buffer.get() + length,
                                buffer.get());

  length = RemoveEscapedNewLine(buffer.get(), buffer.get() + length,
                                buffer.get());

  length = RemoveDeadDirectives(buffer.get(), buffer.get() + length,
                                buffer.get());

  return Content::CreateFromBuffer(buffer.get(), length);
}

// static
const char* DirectiveFilter::SkipSpaces(const char* pos, const char* end) {
  while (pos != end) {
    if (*pos == ' ' || *pos == '\t') {
      ++pos;
      continue;
    }

    int newline_byte = IsEscapedNewLine(pos, end);
    if (newline_byte > 0) {
      pos += newline_byte;
      continue;
    }

    return pos;
  }

  return end;
}

/* static */
const char* DirectiveFilter::NextLineHead(const char* pos, const char* end) {
  while (pos != end) {
    if (*pos == '\n')
      return pos + 1;

    int newline_byte = IsEscapedNewLine(pos, end);
    if (newline_byte)
      pos += newline_byte;
    else
      pos += 1;
  }

  return end;
}

// static
int DirectiveFilter::CopyStringLiteral(const char* pos, const char* end,
                                       char* dst) {
  const char* initial_pos = pos;

  DCHECK_EQ(*pos, '\"');
  DCHECK(pos != end);

  // Copy '\"'
  *dst++ = *pos++;

  while (pos != end) {
    // String literal ends.
    if (*pos == '\"') {
      *dst++ = *pos++;
      break;
    }

    // Corresponding " was not found. Keep this as is.
    if (*pos == '\n') {
      *dst++ = *pos++;
      break;
    }

    int newline_byte = IsEscapedNewLine(pos, end);
    if (newline_byte > 0) {
      while (newline_byte--) {
        *dst++ = *pos++;
      }
      continue;
    }

    // \" does not end string literal.
    // I don't think we need to support trigraph. So, we don't consider "??/",
    // which means "\".
    if (*pos == '\\' && pos + 1 != end && *(pos + 1) == '\"') {
      *dst++ = *pos++;
      *dst++ = *pos++;
      continue;
    }

    *dst++ = *pos++;
  }

  return pos - initial_pos;
}

// static
int DirectiveFilter::IsEscapedNewLine(const char* pos, const char* end) {
  if (*pos != '\\')
    return 0;

  if (pos + 1 < end && *(pos + 1) == '\n')
    return 2;

  if (pos + 2 < end && *(pos + 1) == '\r' && *(pos + 2) == '\n')
    return 3;

  return 0;
}

// Copied |src| to |dst| with removing comments.
// TODO: We assume '"' is not in include pathname.
// When such pathname exists, this won't work well. e.g. #include <foo"bar>
// static
size_t DirectiveFilter::RemoveComments(const char* src, const char* end,
                                       char* dst) {
  const char* original_dst = dst;

  while (src != end) {
    // String starts.
    if (*src == '\"') {
      int num_copied = CopyStringLiteral(src, end, dst);
      src += num_copied;
      dst += num_copied;
      continue;
    }

    // Check a comment does not start.
    if (*src != '/' || src + 1 == end) {
      *dst++ = *src++;
      continue;
    }

    // Block comment starts.
    if (*(src + 1) == '*') {
      const char* end_comment = nullptr;
      const char* pos = src + 2;
      while (pos + 2 <= end) {
        if (*pos == '*' && *(pos + 1) == '/') {
          end_comment = pos;
          break;
        }
        ++pos;
      }

      // When block comment end is not found, we don't skip them.
      if (end_comment == nullptr) {
        while (src < end)
          *dst++ = *src++;
        return dst - original_dst;
      }

      src = end_comment + 2;
      *dst++ = ' ';
      continue;
    }

    // One-line comment starts.
    if (*(src + 1) == '/') {
      src = DirectiveFilter::NextLineHead(src + 2, end);
      *dst++ = '\n';
      continue;
    }

    *dst++ = *src++;
  }

  return dst - original_dst;
}

// static
size_t DirectiveFilter::RemoveEscapedNewLine(
    const char* src, const char* end, char* dst) {
  const char* initial_dst = dst;

  while (src != end) {
    int newline_bytes = IsEscapedNewLine(src, end);
    if (newline_bytes == 0) {
      *dst++ = *src++;
    } else {
      src += newline_bytes;
    }
  }

  return dst - initial_dst;
}

// static
size_t DirectiveFilter::FilterOnlyDirectives(
    const char* src, const char* end, char* dst) {
  const char* const original_dst = dst;

  while (src != end) {
    src = DirectiveFilter::SkipSpaces(src, end);

    if (src != end && *src == '#') {
      *dst++ = *src++;
      // Omit spaces after '#' in directive.
      src = DirectiveFilter::SkipSpaces(src, end);
      const char* next_line_head = DirectiveFilter::NextLineHead(src, end);
      memmove(dst, src, next_line_head - src);
      dst += next_line_head - src;
      src = next_line_head;
    } else {
      src = DirectiveFilter::NextLineHead(src, end);
    }
  }

  return dst - original_dst;
}

// static
size_t DirectiveFilter::RemoveDeadDirectives(
    const char* src, const char* end, char* dst) {
  const char* const original_dst = dst;
  std::vector<StringPiece> directive_stack;

  while (src != end) {
    const char* next_line_head = DirectiveFilter::NextLineHead(src, end);
    StringPiece current_directive_line(src, next_line_head - src);

    src = next_line_head;

    // Drop "#error" support for performance.
    // We assume "#error" almost never happens,
    // so let compiler detect #error failure instead of goma preprocessor.
    if (strings::StartsWith(current_directive_line, "#error")) {
      continue;
    }

    // Drop pragma support other than once.
    // "#pragma once" is only supported pragma in goma preprocessor.
    if (strings::StartsWith(current_directive_line, "#pragma") &&
        current_directive_line.find("once") == StringPiece::npos) {
      continue;
    }

    // Drop #else and #elif until we see something else because
    // such #else of #elif does not change control flow.
    // e.g. code like following is removed because it has no effect
    // to included files.
    // #if USE_STDERR
    //   std::cerr << "some error" << std::endl;
    // #else
    //   std::cout << "some error" << std::endl;
    // #endif
    if (strings::StartsWith(current_directive_line, "#endif")) {
      while (!directive_stack.empty() &&
             (strings::StartsWith(directive_stack.back(), "#else") ||
              strings::StartsWith(directive_stack.back(), "#elif"))) {
        directive_stack.pop_back();
      }

      if (!directive_stack.empty() &&
          strings::StartsWith(directive_stack.back(), "#if")) {
        directive_stack.pop_back();
      } else {
        directive_stack.push_back(current_directive_line);
      }
    } else {
      directive_stack.push_back(current_directive_line);
    }
  }

  for (const auto& directive : directive_stack) {
    memmove(dst, directive.begin(), directive.size());
    dst += directive.size();
  }

  return dst - original_dst;
}

}  // namespace devtools_goma

#ifdef TEST

using devtools_goma::Content;
using devtools_goma::DirectiveFilter;

int main(int argc, char* argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Usage: directive_filter <header or source>\n");
    return 1;
  }

  std::unique_ptr<Content> content(Content::CreateFromFile(argv[1]));
  if (!content.get()) {
    fprintf(stderr, "Cannot read %s\n", argv[1]);
    return 1;
  }

  std::unique_ptr<Content> filtered(
      DirectiveFilter::MakeFilteredContent(*content));

  fwrite(filtered->buf(), sizeof(char), filtered->size(), stdout);
  fflush(stdout);

  return 0;
}
#endif
