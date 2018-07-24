// Copyright 2014 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef __linux__
#error "We only expect this is used by Linux."
#endif

#include "cros_util.h"

#include <memory>

#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "basictypes.h"
#include "file_helper.h"
#include "glog/logging.h"
#include "glog/stl_logging.h"
#include "ioutil.h"
#include "scoped_fd.h"
#include "util.h"

namespace {
const char* const kDefaultBlacklist[] = {
  "/dev-libs/nss",  // make -j fails
  "/app-crypt/nss",  // make -j fails
  "/dev-libs/m17n-lib",  // make -j fails
  "/sys-fs/mtools",  // make -j fails
  "/dev-java/icedtea",  // make -j fails
  "/dev-libs/openssl",  // Makefile force -j1
};

}  // namespace

namespace devtools_goma {

std::vector<string> ParseBlacklistContents(const string& contents) {
  std::vector<string> parsed;
  for (auto&& line : absl::StrSplit(contents,
                                    absl::ByAnyChar("\r\n"),
                                    absl::SkipEmpty())) {
    absl::string_view stripped_line = StringStrip(line);
    if (!stripped_line.empty()) {
      parsed.push_back(string(stripped_line));
    }
  }
  return parsed;
}

std::vector<string> GetBlacklist() {
  const char* blacklist_file = getenv("GOMACC_BLACKLIST");
  if (blacklist_file == nullptr) {
    std::vector<string> default_blacklist;
    for (const auto& it : kDefaultBlacklist) {
      default_blacklist.push_back(it);
    }
    return default_blacklist;
  }
  string contents;
  CHECK(ReadFileToString(blacklist_file, &contents))
    << "Failed to read GOMACC_BLACKLIST=" << blacklist_file;
  return ParseBlacklistContents(contents);
}

bool IsBlacklisted(const string& path, const std::vector<string>& blacklist) {
  for (size_t i = 0; i < blacklist.size(); ++i) {
    if (path.find(blacklist[i]) != string::npos) {
      LOG(INFO) << "The path is blacklisted. "
                << " path=" << path;
      return true;
    }
  }
  return false;
}

float GetLoadAverage() {
  string line;
  ScopedFd fd(ScopedFd::OpenForRead("/proc/loadavg"));
  if (!fd.valid()) {
    PLOG(ERROR) << "failed to open /proc/loadavg";
    return -1;
  }
  char buf[1024];
  int r = fd.Read(buf, sizeof(buf) - 1);
  if (r < 5) {  // should read at least "x.yz "
    PLOG(ERROR) << "failed to read /proc/loadavg";
    return -1;
  }
  buf[r] = '\0';

  std::vector<string> loadavgs = ToVector(
      absl::StrSplit(buf, absl::ByAnyChar(" \t"), absl::SkipEmpty()));
  if (loadavgs.empty()) {
    LOG(ERROR) << "failed to get load average.";
    return -1;
  }
  char* endptr;
  float load = strtof(loadavgs[0].c_str(), &endptr);
  if (loadavgs[0].c_str() == endptr) {
    LOG(ERROR) << "failed to parse load average."
        << " buf=" << buf
        << " loadavgs[0]=" << loadavgs[0];
    return -1;
  }
  return load;
}

int64_t RandInt64(int64_t a, int64_t b) {
  static bool initialized = false;
  if (!initialized) {
    srand(absl::ToInt64Nanoseconds(
        absl::Now().In(absl::UTCTimeZone()).subsecond));
  }
  int64_t rand_value = (static_cast<uint64_t>(rand()) << 32) + rand();
  return a + rand_value % (b - a + 1);
}

bool CanGomaccHandleCwd() {
  const std::vector<string> blacklist = GetBlacklist();
  std::unique_ptr<char, decltype(&free)> cwd(getcwd(nullptr, 0), free);
  if (IsBlacklisted(cwd.get(), blacklist) || getuid() == 0) {
    return false;
  }
  return true;
}

void WaitUntilLoadAvgLowerThan(float load, absl::Duration max_sleep_time) {
  CHECK_GT(load, 0.0)
      << "load must be larger than 0.  Or, this function won't finish."
      << " load=" << load;
  CHECK(max_sleep_time > absl::ZeroDuration())
      << "Max sleep time should be larger than 0 seconds."
      << " max_sleep_time=" << max_sleep_time;
  absl::Time current_time, last_update;
  current_time = last_update = absl::Now();

  absl::Duration sleep_time = absl::Seconds(1);
  for (;;) {
    float current_loadavg = GetLoadAverage();
    CHECK_GE(current_loadavg, 0.0)
        << "load average < 0.  Possibly GetLoadAverage is broken."
        << " current_loadavg=" << current_loadavg;
    if (current_loadavg < load)
      break;

    current_time = absl::Now();
    if (current_time - last_update > max_sleep_time) {
      LOG(WARNING) << "waiting."
                   << " load=" << load
                   << " current_loadavg=" << current_loadavg
                   << " max_sleep_time=" << max_sleep_time;
      last_update = current_time;
    }
    sleep_time *= 2;
    if (sleep_time > max_sleep_time)
      sleep_time = max_sleep_time;
    absl::SleepFor(
        absl::Nanoseconds(
            RandInt64(absl::ToInt64Nanoseconds(absl::Seconds(1)),
                      absl::ToInt64Nanoseconds(sleep_time))));
  }
}

}  // namespace devtools_goma
