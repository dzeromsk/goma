// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "autolock_timer.h"

#include <algorithm>
#include <iomanip>

namespace devtools_goma {

AutoLockStats* g_auto_lock_stats;

AutoLockStat* AutoLockStats::NewStat(const char* name) {
  AutoLock lock(&mu_);
  std::unique_ptr<AutoLockStat> statp(new AutoLockStat(name));
  AutoLockStat* statp_ptr = statp.get();
  stats_.push_back(std::move(statp));
  return statp_ptr;
}

void AutoLockStats::TextReport(std::ostringstream* ss) {
  struct Stat {
    const char* name;
    int count;

    absl::Duration total_wait, max_wait, total_hold, max_hold;
  };

  std::vector<Stat> stats;

  {
    AutoLock lock(&mu_);
    for (size_t i = 0; i < stats_.size(); ++i) {
      AutoLockStat* stat = stats_[i].get();

      Stat s;
      stat->GetStats(&s.count, &s.total_wait, &s.max_wait,
                     &s.total_hold, &s.max_hold);
      s.name = stat->name;
      stats.push_back(s);
    }
  }

  std::sort(stats.begin(), stats.end(),
            [](const Stat& l, const Stat& r) {
              return l.total_wait > r.total_wait;
            });

  for (const auto& s : stats) {
    (*ss) << s.name
          << " count: " << s.count
          << " total-wait: " << s.total_wait
          << " max-wait: " << s.max_wait
          << " ave-wait: " << s.total_wait / std::max(s.count, 1)
          << " total-hold: " << s.total_hold
          << " max-hold: " << s.max_hold
          << " ave-hold: " << s.total_hold / std::max(s.count, 1)
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
      AutoLockStat* stat = stats_[i].get();
      if (skip_names.find(stat->name) != skip_names.end()) {
        continue;
      }

      int count = 0;
      absl::Duration total_wait_time;
      absl::Duration max_wait_time;
      absl::Duration total_hold_time;
      absl::Duration max_hold_time;
      stat->GetStats(&count, &total_wait_time, &max_wait_time, &total_hold_time,
                     &max_hold_time);

      absl::Duration avg_wait_time;
      if (count != 0) {
        avg_wait_time = total_wait_time / count;
      }
      absl::Duration avg_hold_time;
      if (count != 0) {
        avg_hold_time = total_hold_time / count;
      }

      double total_wait_secs = absl::ToDoubleSeconds(total_wait_time);
      double max_wait_secs = absl::ToDoubleSeconds(max_wait_time);
      double ave_wait_secs = absl::ToDoubleSeconds(avg_wait_time);
      double total_hold_secs = absl::ToDoubleSeconds(total_hold_time);
      double max_hold_secs = absl::ToDoubleSeconds(max_hold_time);
      double ave_hold_secs = absl::ToDoubleSeconds(avg_hold_time);

      // Keep number data in data-to-compare. It can be used for comparison.
      (*ss) << "<tr><td>" << stat->name << "</td>"
            << "<td class=\"count\" data-to-compare=\"" << count << "\">"
            << count << "</td>"
            << "<td class=\"total-wait\" data-to-compare=\"" << total_wait_secs
            << "\">" << total_wait_time << "</td>"
            << "<td class=\"max-wait\" data-to-compare=\"" << max_wait_secs
            << "\">" << max_wait_time << "</td>"
            << "<td class=\"ave-wait\" data-to-compare=\"" << ave_wait_secs
            << "\">" << avg_wait_time << "</td>"
            << "<td class=\"total-hold\" data-to-compare=\"" << total_hold_secs
            << "\">" << total_hold_time << "</td>"
            << "<td class=\"max-hold\" data-to-compare=\"" << max_hold_secs
            << "\">" << max_hold_time << "</td>"
            << "<td class=\"ave-hold\" data-to-compare=\"" << ave_hold_secs
            << "\">" << avg_hold_time << "</td>"
            << "</tr>\n";
    }
  }

  (*ss) << "</tbody>"
        << "</table></body></html>";
}

}  // namespace devtools_goma
