// Copyright 2010 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "strutil.h"

#include <assert.h>
#include <cstring>

using std::string;

// ----------------------------------------------------------------------
// StringReplace()
//    Give me a string and two patterns "old" and "new", and I replace
//    the first instance of "old" in the string with "new", if it
//    exists.  If "global" is true; call this repeatedly until it
//    fails.  RETURN a new string, regardless of whether the replacement
//    happened or not.
// ----------------------------------------------------------------------

string StringReplace(StringPiece s, StringPiece oldsub,
                     StringPiece newsub, bool replace_all) {
  string ret;
  StringReplace(s, oldsub, newsub, replace_all, &ret);
  return ret;
}


// ----------------------------------------------------------------------
// StringReplace()
//    Replace the "old" pattern with the "new" pattern in a string,
//    and append the result to "res".  If replace_all is false,
//    it only replaces the first instance of "old."
// ----------------------------------------------------------------------

void StringReplace(StringPiece s, StringPiece oldsub,
                   StringPiece newsub, bool replace_all,
                   string* res) {
  if (oldsub.empty()) {
    res->append(s.data(), s.length());  // If empty, append the given string.
    return;
  }

  StringPiece::size_type start_pos = 0;
  StringPiece::size_type pos;
  do {
    pos = s.find(oldsub, start_pos);
    if (pos == StringPiece::npos) {
      break;
    }
    res->append(s.data() + start_pos, pos - start_pos);
    res->append(newsub.data(), newsub.length());
    // Start searching again after the "old".
    start_pos = pos + oldsub.length();
  } while (replace_all);
  res->append(s.data() + start_pos, s.length() - start_pos);
}

// TODO: adapted from Chrome base, remove when base is here.
#ifdef _WIN32
static int strncasecmp(const char* s1, const char* s2, size_t count) {
  return _strnicmp(s1, s2, count);
}
#endif

const char* strncaseprefix(const char* haystack, int haystack_size,
                           const char* needle, int needle_size) {
  if (haystack_size < needle_size) {
    return nullptr;
  }
  if (strncasecmp(haystack, needle, needle_size) == 0) {
    return haystack + needle_size;
  }
  return nullptr;
}
