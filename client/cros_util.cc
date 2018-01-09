// Copyright 2014 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef __linux__
#error "We only expect this is used by Linux."
#endif

#include "cros_util.h"

#include <sys/time.h>
#include <time.h>

#include <memory>

#include "glog/logging.h"
#include "glog/stl_logging.h"

#include "basictypes.h"
#include "file_helper.h"
#include "ioutil.h"
#include "scoped_fd.h"
#include "split.h"
#include "string_piece.h"

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
  std::vector<string> lines;
  SplitStringUsing(contents, "\r\n", &lines);

  std::vector<string> parsed;
  for (const auto& line : lines) {
    StringPiece stripped_line = StringStrip(line);
    if (!stripped_line.empty())
      parsed.push_back(string(stripped_line));
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

  std::vector<string> loadavgs;
  SplitStringUsing(buf, " \t", &loadavgs);
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

int RandInt(int a, int b) {
  static bool initialized = false;
  if (!initialized) {
    // I chose gettimeofday because I believe it is more unlikely to cause the
    // same random number pattern than srand(time(nullptr)).
    struct timeval tv;
    CHECK_EQ(gettimeofday(&tv, nullptr), 0);
    srandom(tv.tv_usec);
  }
  return a + random() % (b - a + 1);
}

bool CanGomaccHandleCwd() {
  const std::vector<string> blacklist = GetBlacklist();
  std::unique_ptr<char, decltype(&free)> cwd(getcwd(nullptr, 0), free);
  if (IsBlacklisted(cwd.get(), blacklist) || getuid() == 0) {
    return false;
  }
  return true;
}

void WaitUntilLoadAvgLowerThan(float load, int max_sleep_time) {
  CHECK_GT(load, 0.0)
      << "load must be larger than 0.  Or, this function won't finish."
      << " load=" << load;
  CHECK_GT(max_sleep_time, 0)
      << "Max sleep time should be larger than 0."
      << " max_sleep_time=" << max_sleep_time;
  time_t current_time, last_update;
  current_time = last_update = time(nullptr);

  int sleep_time = 1;
  for (;;) {
    float current_loadavg = GetLoadAverage();
    CHECK_GE(current_loadavg, 0.0)
        << "load average < 0.  Possibly GetLoadAverage is broken."
        << " current_loadavg=" << current_loadavg;
    if (current_loadavg < load)
      break;

    current_time = time(nullptr);
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
    sleep(RandInt(1, sleep_time));
  }
}

}  // namespace devtools_goma
