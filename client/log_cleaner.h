// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_LOG_CLEANER_H_
#define DEVTOOLS_GOMA_CLIENT_LOG_CLEANER_H_

#include <ctime>
#include <set>
#include <string>
#include <vector>

#include "basictypes.h"

using std::string;

namespace devtools_goma {

class LogCleaner {
 public:
  LogCleaner();
  ~LogCleaner();

  // Adds log's basename to be cleaned.
  void AddLogBasename(const string& basename);

  // Cleans log files older than t.
  void CleanOldLogs(time_t t);

 private:
  friend class LogCleanerTest;
  void FindOldLogsInDir(const string& log_dir, time_t t,
                        std::set<string>* old_logs);
  bool IsMyLogFile(const string& name) const;

  std::vector<string> basenames_;

  DISALLOW_COPY_AND_ASSIGN(LogCleaner);
};
}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_LOG_CLEANER_H_
