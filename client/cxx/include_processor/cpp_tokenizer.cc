// Copyright 2017 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cpp_tokenizer.h"

#ifndef NO_SSE2
#include <emmintrin.h>
#endif  // NO_SSE2

#ifdef _WIN32
#include <intrin.h>
#endif

#include "absl/strings/ascii.h"
#include "compiler_specific.h"
#include "glog/logging.h"

namespace {

#ifdef _WIN32
static inline int CountZero(int v) {
  unsigned long r;
  _BitScanForward(&r, v);
  return r;
}
#else
static inline int CountZero(int v) {
  return __builtin_ctz(v);
}
#endif

// __popcnt (on MSVC) emits POPCNT. Some engineers are still using older
// machine that does not have POPCNT. So, we'd like to avoid __popcnt.
// clang-cl.exe must have __builtin_popcunt, so use it.
// For cl.exe, use this somewhat fast algorithm.
// See b/65465347
#if defined(_WIN32) && !defined(__clang__)
static inline int PopCount(int v) {
  v = (v & 0x55555555) + (v >> 1 & 0x55555555);
  v = (v & 0x33333333) + (v >> 2 & 0x33333333);
  v = (v & 0x0f0f0f0f) + (v >> 4 & 0x0f0f0f0f);
  v = (v & 0x00ff00ff) + (v >> 8 & 0x00ff00ff);
  return (v & 0x0000ffff) + (v >>16 & 0x0000ffff);
}
#else
static inline int PopCount(int v) {
  return __builtin_popcount(v);
}
#endif

#ifndef NO_SSE2
typedef ALIGNAS(16) char aligned_char16[16];
const aligned_char16 kNewlinePattern = {
  0xA, 0xA, 0xA, 0xA, 0xA, 0xA, 0xA, 0xA,
  0xA, 0xA, 0xA, 0xA, 0xA, 0xA, 0xA, 0xA,
};
const aligned_char16 kSlashPattern = {
  '/', '/', '/', '/', '/', '/', '/', '/',
  '/', '/', '/', '/', '/', '/', '/', '/',
};
const aligned_char16 kSharpPattern = {
  '#', '#', '#', '#', '#', '#', '#', '#',
  '#', '#', '#', '#', '#', '#', '#', '#',
};
#endif  // NO_SSE2

}  // anonymous namespace

namespace devtools_goma {

// static
bool CppTokenizer::TokenizeAll(const std::string& str,
                               bool skip_space,
                               ArrayTokenList* result) {
  std::unique_ptr<Content> content = Content::CreateFromString(str);
  CppInputStream stream(content.get(), "<content>");

  string error_reason;
  ArrayTokenList tokens;
  while (true) {
    CppToken token;
    if (!NextTokenFrom(&stream, skip_space, &token, &error_reason)) {
      break;
    }
    if (token.type == CppToken::END) {
      break;
    }
    tokens.push_back(std::move(token));
  }

  if (!error_reason.empty()) {
    LOG(ERROR) << "failed to tokenize:"
               << " input=" << str
               << " error=" << error_reason;
    return false;
  }

  *result = std::move(tokens);
  return true;
}

// static
bool CppTokenizer::NextTokenFrom(CppInputStream* stream,
                                 bool skip_space,
                                 CppToken* token,
                                 std::string* error_reason) {
  for (;;) {
    const char* cur = stream->cur();
    int c = stream->GetChar();
    if (c == EOF) {
      *token = CppToken(CppToken::END);
      return true;
    }
    if (c >= 128) {
      *token = CppToken(CppToken::PUNCTUATOR, static_cast<char>(c));
      return true;
    }
    if (IsCppBlank(c)) {
      if (skip_space) {
        stream->SkipWhiteSpaces();
        continue;
      }
      *token = CppToken(CppToken::SPACE, static_cast<char>(c));
      return true;
    }
    int c1 = stream->PeekChar();
    switch (c) {
      case '/':
        if (c1 == '/') {
          SkipUntilLineBreakIgnoreComment(stream);
          *token = CppToken(CppToken::NEWLINE);
          return true;
        }
        if (c1 == '*') {
          stream->Advance(1, 0);
          if (!SkipComment(stream, error_reason)) {
            *token = CppToken(CppToken::END);
            return false;
          }
          *token = CppToken(CppToken::SPACE, ' ');
          return true;
        }
        *token = CppToken(CppToken::DIV, '/');
        return true;
      case '%':
        if (c1 == ':') {
          stream->Advance(1, 0);
          if (stream->PeekChar(0) == '%' &&
              stream->PeekChar(1) == ':') {
            stream->Advance(2, 0);
            *token = CppToken(CppToken::DOUBLESHARP);
            return true;
          }
          *token = CppToken(CppToken::SHARP, '#');
          return true;
        }
        *token = CppToken(CppToken::MOD, '%');
        return true;
      case '.':
        if (c1 >= '0' && c1 <= '9') {
          *token = ReadNumber(stream, c, cur);
          return true;
        }
        if (c1 == '.' && stream->PeekChar(1) == '.') {
          stream->Advance(2, 0);
          *token = CppToken(CppToken::TRIPLEDOT);
          return true;
        }
        *token = CppToken(CppToken::PUNCTUATOR, '.');
        return true;
      case '\\':
        c = stream->GetChar();
        if (c != '\r' && c != '\n') {
          *token = CppToken(CppToken::ESCAPED, static_cast<char>(c));
          return true;
        }
        if (c == '\r' && stream->PeekChar() == '\n')
          stream->Advance(1, 1);
        break;
      case '"': {
        *token = CppToken(CppToken::STRING);
        if (!ReadString(stream, token, error_reason)) {
          return false;
        }
        return true;
      }
      case '\'':
        if (ReadCharLiteral(stream, token)) {
          return true;
        }
        // Non-ended single quotation is valid in preprocessor.
        // e.g. 'A will be PUNCTUATOR '\'' and IDENTIFIER('A).
        FALLTHROUGH_INTENDED;
      default:
        if (c == '_'  || c == '$' || absl::ascii_isalpha(c)) {
          *token = ReadIdentifier(stream, cur);
          return true;
        }
        if (c >= '0' && c <= '9') {
          *token = ReadNumber(stream, c, cur);
          return true;
        }
        if (c1 == EOF) {
          *token = CppToken(TypeFrom(c, 0), static_cast<char>(c));
          return true;
        }
        if ((c1 & ~0x7f) == 0 && TypeFrom(c, c1) != CppToken::PUNCTUATOR) {
          stream->Advance(1, 0);
          *token = CppToken(TypeFrom(c, c1),
                            static_cast<char>(c), static_cast<char>(c1));
          return true;
        }
        *token = CppToken(TypeFrom(c, 0), static_cast<char>(c));
        return true;
    }
  }
}

// static
bool CppTokenizer::ReadStringUntilDelimiter(CppInputStream* stream,
                                            std::string* result_str,
                                            char delimiter,
                                            std::string* error_reason) {
  const char* begin = stream->cur();
  for (;;) {
    int c = stream->PeekChar();
    if (c == EOF) {
      return true;
    }
    if (c == delimiter) {
      const char* cur = stream->cur() - 1;
      stream->Advance(1, 0);
      if (*cur != '\\') {
        result_str->append(begin, stream->cur() - begin - 1);
        return true;
      }
    } else if (c == '\n') {
      const char* cur = stream->cur() - 1;
      stream->Advance(1, 1);
      cur -= (*cur == '\r');
      if (*cur != '\\') {
        *error_reason = "missing terminating character";
        return false;
      }
      result_str->append(begin, stream->cur() - begin - 2);
      begin = stream->cur();
    } else {
      stream->Advance(1, 0);
    }
  }
}

// static
CppToken CppTokenizer::ReadIdentifier(CppInputStream* stream,
                                      const char* begin) {
  CppToken token(CppToken::IDENTIFIER);
  for (;;) {
    int c = stream->GetChar();
    if (absl::ascii_isalnum(c) || c == '_' || c == '$' ||
        (c == '\\' && HandleLineFoldingWithToken(stream, &token, &begin))) {
      continue;
    }
    token.Append(begin, stream->GetLengthToCurrentFrom(begin, c));
    stream->UngetChar(c);
    return token;
  }
}

// (6.4.2) Preprocessing numbers
// pp-number :
//    digit
//    .digit
//    pp-number digit
//    pp-number nondigit
//    pp-number [eEpP] sign  ([pP] is new in C99)
//    pp-number .
//
// static
CppToken CppTokenizer::ReadNumber(CppInputStream* stream, int c0,
                                  const char* begin) {
  CppToken token(CppToken::NUMBER);

  bool maybe_int_constant = (c0 != '.');
  int base = 10;
  int value = 0;
  std::string suffix;
  int c;

  // Handle base prefix.
  if (c0 == '0') {
    base = 8;
    int c1 = stream->PeekChar();
    if (c1 == 'x' || c1 == 'X') {
      stream->Advance(1, 0);
      base = 16;
    }
  } else {
    value = c0 - '0';
  }

  if (maybe_int_constant) {
    // Read the digits part.
    c = absl::ascii_tolower(stream->GetChar());
    while ((c >= '0' && c <= ('0' + std::min(9, base - 1))) ||
           (base == 16 && c >= 'a' && c <= 'f')) {
      value = value * base + ((c >= 'a') ? (c - 'a' + 10) : (c - '0'));
      c = absl::ascii_tolower(stream->GetChar());
    }
    stream->UngetChar(c);
  }

  // (digit | [a-zA-Z_] | . | [eEpP][+-])*
  for (;;) {
    c = stream->GetChar();
    if (c == '\\' && HandleLineFoldingWithToken(stream, &token, &begin)) {
      continue;
    }
    if ((c >= '0' && c <= '9') || c == '.' || c == '_') {
      maybe_int_constant = false;
      continue;
    }
    c = absl::ascii_tolower(c);
    if (c >= 'a' && c <= 'z') {
      if (maybe_int_constant) {
        suffix += static_cast<char>(c);
      }
      if (c == 'e' || c == 'p') {
        int c1 = stream->PeekChar();
        if (c1 == '+' || c1 == '-') {
          maybe_int_constant = false;
          stream->Advance(1, 0);
        }
      }
      continue;
    }
    break;
  }

  token.Append(begin, stream->GetLengthToCurrentFrom(begin, c));
  stream->UngetChar(c);
  if (maybe_int_constant && (suffix.empty() || IsValidIntegerSuffix(suffix))) {
    token.v.int_value = value;
  }
  return token;
}

// static
bool CppTokenizer::ReadString(CppInputStream* stream,
                              CppToken* result_token,
                              std::string* error_reason) {
  CppToken token(CppToken::STRING);
  if (!ReadStringUntilDelimiter(stream, &token.string_value,
                                '"', error_reason)) {
    return false;
  }

  *result_token = std::move(token);
  return true;
}

int hex2int(int ch) {
  if (ch >= '0' && ch <= '9') {
    return ch - '0';
  }
  if (ch >= 'a' && ch <= 'f') {
    return ch - 'a' + 10;
  }
  return ch - 'A' + 10;
}

// http://www.iso-9899.info/n1256.html#6.4.4.4
// static
bool CppTokenizer::ReadCharLiteral(CppInputStream* stream,
                                   CppToken* result_token) {
  // TODO: preserve original literal in token.string_value?
  CppToken token(CppToken::CHAR_LITERAL);
  const char* cur = stream->cur();
  const ptrdiff_t cur_len = stream->end() - cur;

  if (cur_len >= 3 && cur[0] == '\\' && cur[2] == '\'') {
    switch (cur[1]) {
      case '0': case '1': case '2': case '3': case '4':
      case '5': case '6': case '7':
        // \ octal-digit
        token.v.int_value = cur[1] - '0';
        break;
      case '\'':
        token.v.int_value = '\'';
        break;
      case '"':
        token.v.int_value = '"';
        break;
      case '?':
        token.v.int_value = '?';
        break;
      case '\\':
        token.v.int_value = '\\';
        break;
      case 'a':
        token.v.int_value = '\a';
        break;
      case 'b':
        token.v.int_value = '\b';
        break;
      case 'f':
        token.v.int_value = '\f';
        break;
      case 'n':
        token.v.int_value = '\n';
        break;
      case 'r':
        token.v.int_value = '\r';
        break;
      case 't':
        token.v.int_value = '\t';
        break;
      case 'v':
        token.v.int_value = '\v';
        break;
      default:
        LOG(ERROR) << "Unexpected escaped char literal?: " << cur[1]
                   << " in line " << stream->line()
                   << " of file: " << stream->filename();
        return false;
    }

    stream->Advance(3, 0);
  } else if (cur_len >= 2 &&
             cur[0] != '\\' && cur[0] != '\'' &&
             cur[0] != '\n' &&
             cur[1] == '\'') {
    // c-char
    token.v.int_value = cur[0];

    stream->Advance(2, 0);
  } else if (cur_len >= 5 && cur[0] == '\\'
        && cur[1] == 'x' &&
        absl::ascii_isxdigit(cur[2]) &&
        absl::ascii_isxdigit(cur[3]) &&
        cur[4] == '\'') {
      // \x hexadecimal-digit hexadecimal-digit
    token.v.int_value = hex2int(cur[2]) << 4 | hex2int(cur[3]);
    stream->Advance(5, 0);
  } else if (cur_len >= 4 && cur[0] == '\\' &&
             cur[1] >= '0' && cur[1] < '8' &&
             cur[2] >= '0' && cur[2] < '8' &&
             cur[3] == '\'') {
    // \ octal-digit octal-digit
    token.v.int_value =
        (cur[1] - '0') << 3 |
        (cur[2] - '0');
    stream->Advance(4, 0);
  } else if (cur_len >= 5 && cur[0] == '\\' &&
             cur[1] >= '0' && cur[1] < '8' &&
             cur[2] >= '0' && cur[2] < '8' &&
             cur[3] >= '0' && cur[3] < '8' &&
             cur[4] == '\'') {
    // \ octal-digit octal-digit octal-digit
    token.v.int_value =
        (cur[1] - '0') << 6 |
        (cur[2] - '0') << 3 |
        (cur[3] - '0');
    stream->Advance(5, 0);
  } else if (cur_len >= 3 &&
             cur[0] != '\'' && cur[0] != '\\' &&
             cur[1] != '\'' && cur[1] != '\\' &&
             cur[2] == '\'') {
    // c-char-sequence
    // support only 2 char sequence here
    // Windows winioctl.h uses such sequence. http://b/74048713
    // winioctl.h in win_sdk
    //  #define IRP_EXT_TRACK_OFFSET_HEADER_VALIDATION_VALUE 'TO'
    token.v.int_value =
         (cur[0] <<  8) |
         cur[1];
    stream->Advance(3, 0);
  } else if (cur_len >= 5 &&
             cur[0] != '\'' && cur[0] != '\\' &&
             cur[1] != '\'' && cur[1] != '\\' &&
             cur[2] != '\'' && cur[2] != '\\' &&
             cur[3] != '\'' && cur[3] != '\\' &&
             cur[4] == '\'') {
    // c-char-sequence
    // support only 4 char sequence here.
    // MacOSX system header uses such sequence. http://b/74048713
    // - PMPrintAETypes.h in PrintCore.framework
    //   #define kPMPrintSettingsAEType                  'pset'
    //   etc
    // - Debugging.h in CarbonCore.framework
    //   #define COMPONENT_SIGNATURE '?*?*'
    //
    // The value of an integer character constant containing more than
    // one character (e.g., 'ab'), or containing a character or escape
    // sequence that does not map to a single-byte execution character,
    // is implementation-defined.
    token.v.int_value =
         (cur[0] << 24) |
         (cur[1] << 16) |
         (cur[2] <<  8) |
         cur[3];
    stream->Advance(5, 0);
  } else {
    // TODO: Support other literal form if necessary.
    LOG(ERROR) << "Unsupported char literal?: "
               << absl::string_view(cur, std::min<ptrdiff_t>(10, cur_len))
               << " in line " << stream->line()
               << " of file: " << stream->filename();
    return false;
  }

  *result_token = std::move(token);
  return true;
}

// static
bool CppTokenizer::HandleLineFoldingWithToken(CppInputStream* stream,
                                              CppToken* token,
                                              const char** begin) {
  int c = stream->PeekChar();
  if (c != '\r' && c != '\n')
    return false;
  stream->ConsumeChar();
  token->Append(*begin, stream->cur() - *begin - 2);
  if (c == '\r' && stream->PeekChar() == '\n')
    stream->Advance(1, 1);
  *begin = stream->cur();
  return true;
}

// static
bool CppTokenizer::SkipComment(CppInputStream* stream,
                               std::string* error_reason) {
  const char* begin = stream->cur();
#ifndef NO_SSE2
  __m128i slash_pattern = *(__m128i*)kSlashPattern;
  __m128i newline_pattern = *(__m128i*)kNewlinePattern;
  while (stream->cur() + 16 < stream->end()) {
    __m128i s = _mm_loadu_si128((__m128i const*)stream->cur());
    __m128i slash_test = _mm_cmpeq_epi8(s, slash_pattern);
    __m128i newline_test = _mm_cmpeq_epi8(s, newline_pattern);
    int result = _mm_movemask_epi8(slash_test);
    int newline_result = _mm_movemask_epi8(newline_test);
    while (result) {
      int index = CountZero(result);
      unsigned int shift = (1 << index);
      result &= ~shift;
      const char* cur = stream->cur() + index - 1;
      if (*cur == '*') {
        unsigned int mask = shift - 1;
        stream->Advance(index + 1, PopCount(newline_result & mask));
        return true;
      }
    }
    stream->Advance(16, PopCount(newline_result));
  }
#endif  // NO_SSE2
  for (;;) {
    int c = stream->PeekChar();
    if (c == EOF) {
      *error_reason = "missing terminating '*/' for comment";
      return false;
    }
    if (c == '/' && stream->cur() != begin &&
        *(stream->cur() - 1) == '*') {
      stream->Advance(1, 0);
      return true;
    }
    stream->ConsumeChar();
  }
}

// static
bool CppTokenizer::SkipUntilDirective(CppInputStream* stream,
                                      std::string* error_reason) {
  const char* begin = stream->cur();
#ifndef NO_SSE2
  // TODO: String index instruction (pcmpestri) would work better
  // on sse4.2 enabled platforms.
  __m128i slash_pattern = *(__m128i*)kSlashPattern;
  __m128i sharp_pattern = *(__m128i*)kSharpPattern;
  __m128i newline_pattern = *(__m128i*)kNewlinePattern;
  while (stream->cur() + 16 < stream->end()) {
    __m128i s = _mm_loadu_si128((__m128i const*)stream->cur());
    __m128i slash_test = _mm_cmpeq_epi8(s, slash_pattern);
    __m128i sharp_test = _mm_cmpeq_epi8(s, sharp_pattern);
    __m128i newline_test = _mm_cmpeq_epi8(s, newline_pattern);
    int slash_result = _mm_movemask_epi8(slash_test);
    int sharp_result = _mm_movemask_epi8(sharp_test);
    int newline_result = _mm_movemask_epi8(newline_test);
    int result = slash_result | sharp_result;
    while (result) {
      int index = CountZero(result);
      unsigned int shift = (1 << index);
      result &= ~shift;
      unsigned int mask = shift - 1;
      const char* cur = stream->cur() + index;
      if (*cur == '/') {
        int c1 = *(cur + 1);
        if (c1 == '/') {
          stream->Advance(index + 2, PopCount(newline_result & mask));
          SkipUntilLineBreakIgnoreComment(stream);
          goto done;
        } else if (c1 == '*') {
          stream->Advance(index + 2, PopCount(newline_result & mask));
          if (!SkipComment(stream, error_reason))
            return false;
          goto done;
        }
      } else if (*cur == '#') {
        if (IsAfterEndOfLine(cur, stream->begin())) {
          stream->Advance(index + 1, PopCount(newline_result & mask));
          return true;
        }
      }
    }
    stream->Advance(16, PopCount(newline_result));
  done:
    continue;
  }
#endif  // NO_SSE2
  for (;;) {
    int c = stream->PeekChar();
    if (c == EOF)
      return false;
    if (stream->cur() != begin) {
      int c0 = *(stream->cur() - 1);
      if (c0 == '/' && c == '/') {
        stream->Advance(1, 0);
        SkipUntilLineBreakIgnoreComment(stream);
        continue;
      }
      if (c0 == '/' && c == '*') {
        stream->Advance(1, 0);
        if (!SkipComment(stream, error_reason))
          return false;
      }
    }
    if (c == '#') {
      if (IsAfterEndOfLine(stream->cur(),
                           stream->begin())) {
        stream->Advance(1, 0);
        return true;
      }
      stream->Advance(1, 0);
      continue;
    }
    stream->ConsumeChar();
  }

  return false;
}

// static
void CppTokenizer::SkipUntilLineBreakIgnoreComment(CppInputStream* stream) {
#ifndef NO_SSE2
  __m128i newline_pattern = *(__m128i*)kNewlinePattern;
  while (stream->cur() + 16 < stream->end()) {
    __m128i s = _mm_loadu_si128((__m128i const*)stream->cur());
    __m128i newline_test = _mm_cmpeq_epi8(s, newline_pattern);
    int newline_result = _mm_movemask_epi8(newline_test);
    int result = newline_result;
    while (result) {
      int index = CountZero(result);
      unsigned int shift = (1 << index);
      result &= ~shift;
      unsigned int mask = shift - 1;
      const char* cur = stream->cur() + index - 1;
      cur -= (*cur == '\r');
      if (*cur != '\\') {
        stream->Advance(index + 1, PopCount(newline_result & mask));
        return;
      }
    }
    stream->Advance(16, PopCount(newline_result));
  }
#endif  // NO_SSE2
  for (;;) {
    int c = stream->PeekChar();
    if (c == EOF)
      return;
    if (c == '\n') {
      const char* cur = stream->cur() - 1;
      stream->Advance(1, 1);
      cur -= (*cur == '\r');
      if (*cur != '\\')
        return;
    } else {
      stream->Advance(1, 0);
    }
  }
}

// static
bool CppTokenizer::IsAfterEndOfLine(const char* cur, const char* begin) {
  for (;;) {
    if (cur == begin)
      return true;
    int c = *--cur;
    if (!IsCppBlank(c))
      break;
  }

  while (begin <= cur) {
    int c = *cur;
    if (c == '\n') {
      if (--cur < begin)
        return true;
      cur -= (*cur == '\r');
      if (cur < begin || *cur != '\\')
        return true;

      --cur;
      continue;
    }

    if (c == '/') {
      if (--cur < begin || *cur != '*')
        return false;

      --cur;
      bool block_comment_start_found = false;
      // Move backward until "/*" is found.
      while (cur - 1 >= begin) {
        if (*(cur - 1) == '/' && *cur == '*') {
          cur -= 2;
          block_comment_start_found = true;
          break;
        }
        --cur;
      }

      if (block_comment_start_found)
        continue;

      // When '/*' is not found, it's not after end of line.
      return false;
    }

    if (IsCppBlank(c)) {
      --cur;
      continue;
    }

    return false;
  }

  return true;
}

// static
bool CppTokenizer::IsValidIntegerSuffix(const std::string& s) {
  switch (s.size()) {
    case 1:
      return s == "u" || s == "l";
    case 2:
      return s == "ul" || s == "lu" || s == "ll";
    case 3:
      return s == "ull" || s == "llu";
    default:
      return false;
  }
}

// static
CppToken::Type CppTokenizer::TypeFrom(int c1, int c2) {
  switch (c1) {
  case '!':
    if (c2 == '=') { return CppToken::NE; }
    break;
  case '#':
    if (c2 == 0) { return CppToken::SHARP; }
    if (c2 == '#') { return CppToken::DOUBLESHARP; }
    break;
  case '&':
    if (c2 == 0) { return CppToken::AND; }
    if (c2 == '&') { return CppToken::LAND; }
    break;
  case '*':
    if (c2 == 0) { return CppToken::MUL; }
    break;
  case '+':
    if (c2 == 0) { return CppToken::ADD; }
    break;
  case '-':
    if (c2 == 0) { return CppToken::SUB; }
    break;
  case '<':
    if (c2 == 0) { return CppToken::LT; }
    if (c2 == '<') { return CppToken::LSHIFT; }
    if (c2 == '=') { return CppToken::LE; }
    break;
  case '=':
    if (c2 == '=') { return CppToken::EQ; }
    break;
  case '>':
    if (c2 == 0) { return CppToken::GT; }
    if (c2 == '=') { return CppToken::GE; }
    if (c2 == '>') { return CppToken::RSHIFT; }
    break;
  case '\n':
    if (c2 == 0) { return CppToken::NEWLINE; }
    break;
  case '\r':
    if (c2 == '\n') { return CppToken::NEWLINE; }
    break;
  case '^':
    if (c2 == 0) { return CppToken::XOR; }
    break;
  case '|':
    if (c2 == 0) { return CppToken::OR; }
    if (c2 == '|') { return CppToken::LOR; }
    break;
  }

  return CppToken::PUNCTUATOR;
}

}  // namespace devtools_goma
