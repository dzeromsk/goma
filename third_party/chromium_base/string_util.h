// This file is extracted from chromium/src/base/strings/string_util.h
// and modified for goma/client.
// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This file defines utility functions for working with strings.

#ifndef BASE_STRINGS_STRING_UTIL_H_
#define BASE_STRINGS_STRING_UTIL_H_

#include <string>

#include "string_piece.h"

// ASCII-specific tolower.  The standard library's tolower is locale sensitive,
// so we don't want to use it here.
inline char ToLowerASCII(char c) {
  return (c >= 'A' && c <= 'Z') ? (c + ('a' - 'A')) : c;
}

inline std::string ToLower(StringPiece sp) {
  std::string str;
  str.reserve(sp.size());
  for (auto c : sp) {
    str.push_back(ToLowerASCII(c));
  }
  return str;
}

// ASCII-specific toupper.  The standard library's toupper is locale sensitive,
// so we don't want to use it here.
inline char ToUpperASCII(char c) {
  return (c >= 'a' && c <= 'z') ? (c + ('A' - 'a')) : c;
}

// Determines the type of ASCII character, independent of locale (the C
// library versions will change based on locale).
template <typename Char>
inline bool IsCppBlank(Char c) {
  return c == ' ' || c == '\t' || c == '\f' || c == '\v';
}
template <typename Char>
inline bool IsAsciiAlpha(Char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}
template <typename Char>
inline bool IsAsciiUpper(Char c) {
  return c >= 'A' && c <= 'Z';
}
template <typename Char>
inline bool IsAsciiLower(Char c) {
  return c >= 'a' && c <= 'z';
}
template <typename Char>
inline bool IsAsciiDigit(Char c) {
  return c >= '0' && c <= '9';
}

template <typename Char>
inline bool IsHexDigit(Char c) {
  return (c >= '0' && c <= '9') ||
         (c >= 'A' && c <= 'F') ||
         (c >= 'a' && c <= 'f');
}

template <typename Char>
inline bool IsAsciiAlphaDigit(Char c) {
  return (c >= '0' && c <= '9') ||
      (c >= 'A' && c <= 'Z') ||
      (c >= 'a' && c <= 'z');
}

#endif  // BASE_STRINGS_STRING_UTIL_H_
