// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "autolock_timer.h"

#include <algorithm>
#include <iomanip>

namespace devtools_goma {

AutoLockStats* g_auto_lock_stats;

static const double kNanosecondsPerSecond = 1000000000;

AutoLockStat* AutoLockStats::NewStat(const char* name) {
  AutoLock lock(&mu_);
  AutoLockStat* statp = new AutoLockStat(name);
  stats_.push_back(statp);
  return statp;
}

AutoLockStats::~AutoLockStats() {
  for (size_t i = 0; i < stats_.size(); ++i) {
    delete stats_[i];
  }
  stats_.clear();
}

void AutoLockStats::TextReport(std::ostringstream* ss) {

  AutoLock lock(&mu_);
  for (size_t i = 0; i < stats_.size(); ++i) {
    AutoLockStat* stat = stats_[i];

    int count = 0;
    int64_t total_wait_time_ns = 0;
    int64_t max_wait_time_ns = 0;
    int64_t total_hold_time_ns = 0;
    int64_t max_hold_time_ns = 0;
    stat->GetStats(&count, &total_wait_time_ns, &max_wait_time_ns,
                   &total_hold_time_ns, &max_hold_time_ns);
    (*ss) << stat->name
          << " count: " << count
          << " total-wait: "
          << total_wait_time_ns / kNanosecondsPerSecond
          << " max-wait:"
          << max_wait_time_ns / kNanosecondsPerSecond
          << " ave-wait:"
          << total_wait_time_ns / std::max(count, 1) / kNanosecondsPerSecond
          << " total-hold:"
          << total_hold_time_ns / kNanosecondsPerSecond
          << " max-hold:"
          << max_hold_time_ns / kNanosecondsPerSecond
          << " ave-hold:"
          << total_hold_time_ns / std::max(count, 1) / kNanosecondsPerSecond
          << "\n";
  }
}

void AutoLockStats::Report(std::ostringstream* ss,
                           const std::unordered_set<std::string>& skip_names) {
  (*ss) << "<html>"
        << "<script src=\"/static/jquery.min.js\"></script>"
        << "<script src=\"/static/compiler_proxy_contentionz_script.js\">"
        << "</script>"
        << "<body onload=\"init()\">"
        << (skip_names.empty() ?
            "<a href=\"./contentionz\">simplified</a>" :
            "<a href=\"./contentionz?detailed=1\">detailed</a>")
        << "<table border=\"1\"><thead>"
        << "<tr><th>name</th>"
        << "<th class=\"count\">count</th>"
        << "<th class=\"total-wait\">total wait</th>"
        << "<th class=\"max-wait\">max wait</th>"
        << "<th class=\"ave-wait\">ave wait</th>"
        << "<th class=\"total-hold\">total hold</th>"
        << "<th class=\"max-hold\">max hold</th>"
        << "<th class=\"ave-hold\">ave hold</th>"
        << "</tr></thead>\n"
        << "<tbody>"
        << std::fixed << std::setprecision(9);

  {
    AutoLock lock(&mu_);
    for (size_t i = 0; i < stats_.size(); ++i) {
      AutoLockStat* stat = stats_[i];
      if (skip_names.find(stat->name) != skip_names.end()) {
        continue;
      }

      int count = 0;
      int64_t total_wait_time_ns = 0;
      int64_t max_wait_time_ns = 0;
      int64_t total_hold_time_ns = 0;
      int64_t max_hold_time_ns = 0;
      stat->GetStats(&count, &total_wait_time_ns, &max_wait_time_ns,
                     &total_hold_time_ns, &max_hold_time_ns);
      (*ss) << "<tr><td>" << stat->name << "</td>"
            << "<td class=\"count\">" << count << "</td>"
            << "<td class=\"total-wait\">"
            << total_wait_time_ns / kNanosecondsPerSecond << "</td>"
            << "<td class=\"max-wait\">"
            << max_wait_time_ns / kNanosecondsPerSecond << "</td>"
            << "<td class=\"ave-wait\">"
            << total_wait_time_ns / count / kNanosecondsPerSecond << "</td>"
            << "<td class=\"total-hold\">"
            << total_hold_time_ns / kNanosecondsPerSecond << "</td>"
            << "<td class=\"max-hold\">"
            << max_hold_time_ns / kNanosecondsPerSecond << "</td>"
            << "<td class=\"ave-hold\">"
            << total_hold_time_ns / count / kNanosecondsPerSecond << "</td>"
            << "</tr>\n";
    }
  }

  (*ss) << "</tbody>"
        << "</table></body></html>";
}

}  // namespace devtools_goma
