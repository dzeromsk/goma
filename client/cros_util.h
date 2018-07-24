// Copyright 2014 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_CROS_UTIL_H_
#define DEVTOOLS_GOMA_CLIENT_CROS_UTIL_H_

#include <string>
#include <vector>

#include "absl/time/time.h"

using std::string;

namespace devtools_goma {

#ifdef __linux__

// Parses blacklist file contents and returns a list of blacklised directories.
std::vector<string> ParseBlacklistContents(const string& contents);

// Returns list of portage package names to blacklist.
//  If non-empty $GOMACC_BLACKLIST is set, we use its file contents in the file
//  specified by $GOMACC_BLACKLIST instead of the default one.
//  If $GOMACC_BLACKLIST is an empty string, we use _DEFAULT_BLACKLIST.
//  The blacklist contents should be list of directories like:
//  /dev-libs/nss
//  /sys-fs/mtools
//
//  Note that empty line is just ignored.
std::vector<string> GetBlacklist();

// Returns true if |path| matches with one of path name in |blacklist|.
bool IsBlacklisted(const string& path, const std::vector<string>& blacklist);

// Returns load average in 1 min.  Returns negative value on error.
float GetLoadAverage();

// Returns random integer x such as |a| <= x <= |b|.
// Note: a returned random number is not uniform.
//       I think it enough for randomizing a sleep time.
int64_t RandInt64(int64_t a, int64_t b);

// Returns true if current working directory is not in the black list.
// If in the black list, gomacc won't send the request to compiler_proxy.
bool CanGomaccHandleCwd();

// Waits the load average becomes less than |load|.
// This function may make the program asleep at most |max_sleep_time|.
void WaitUntilLoadAvgLowerThan(float load, absl::Duration max_sleep_time);

#else

// Provide dummy function for non-linux.
bool CanGomaccHandle(const std::vector<string>& args) {
  return true;
}
void WaitUntilLoadAvgLowerThan(float load, absl::Duration max_sleep_time) {}

#endif

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_CROS_UTIL_H_
