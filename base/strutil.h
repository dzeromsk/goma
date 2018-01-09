// Copyright 2010 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_BASE_STRUTIL_H_
#define DEVTOOLS_GOMA_BASE_STRUTIL_H_

#include <string>
#include <vector>

#include "string_piece.h"

using std::string;

// ----------------------------------------------------------------------
// StringReplace()
//    Give me a string and two patterns "old" and "new", and I replace
//    the first instance of "old" in the string with "new", if it
//    exists.  RETURN a new string, regardless of whether the replacement
//    happened or not.
// ----------------------------------------------------------------------

string StringReplace(StringPiece s, StringPiece oldsub,
                     StringPiece newsub, bool replace_all);

// ----------------------------------------------------------------------
// StringReplace()
//    Replace the "old" pattern with the "new" pattern in a string,
//    and append the result to "res".  If replace_all is false,
//    it only replaces the first instance of "old."
//
//    Here is a couple of notes on the "res" string:
//      The "res" should not point to any of the input strings.
//      Reserving enough capacity for the "res" prior to calling this
//      function will improve the speed.
// ----------------------------------------------------------------------

void StringReplace(StringPiece s, StringPiece oldsub,
                   StringPiece newsub, bool replace_all,
                   string* res);

// Matches a case-insensitive prefix (up to the first needle_size bytes of
// needle) in the first haystack_size byte of haystack. Returns a pointer past
// the prefix, or NULL if the prefix wasn't matched.
//
// Always returns either NULL or haystack + needle_size.
const char* strncaseprefix(const char* haystack, int haystack_size,
                           const char* needle, int needle_size);

// Matches a case-insensitive prefix; returns a pointer past the prefix,
// or NULL if not found.
template<class CharStar>
inline CharStar var_strcaseprefix(CharStar str, const char* prefix) {
  return strncaseprefix(str, strlen(str), prefix, strlen(prefix));
}

#endif  // DEVTOOLS_GOMA_BASE_STRUTIL_H_
