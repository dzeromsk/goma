// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "log_cleaner.h"

#include <stdlib.h>

#ifndef _WIN32
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#else
#include "config_win.h"
#include <stack>
#include "filetime_win.h"
#endif

#include "absl/strings/match.h"
#include "file_dir.h"
#include "file_stat.h"
#include "glog/logging.h"
#include "glog/stl_logging.h"
#include "path.h"
#include "scoped_fd.h"

namespace devtools_goma {

LogCleaner::LogCleaner() {
}

LogCleaner::~LogCleaner() {
}

void LogCleaner::AddLogBasename(const string& basename) {
  LOG(INFO) << "log basename:" << basename;
  basenames_.push_back(basename);
}

void LogCleaner::CleanOldLogs(absl::Time time) {
  const std::vector<string>& log_dirs = google::GetLoggingDirectories();
  LOG(INFO) << "clean old logs in " << log_dirs;

  std::set<string> old_logs;
  for (const auto& dir : log_dirs) {
    FindOldLogsInDir(dir, time, &old_logs);
  }
  if (old_logs.empty()) {
    LOG(INFO) << "no old logs found.";
    return;
  }
  for (const auto& old_log : old_logs) {
    LOG(INFO) << "remove old log:" << old_log;
    if (remove(old_log.c_str()) != 0) {
      PLOG(WARNING) << "delete:" << old_log;
    }
  }
}

void LogCleaner::FindOldLogsInDir(const string& log_dir, absl::Time time,
                                  std::set<string>* old_logs) {
  VLOG(1) << "log_dir:" << log_dir;
  std::vector<DirEntry> entries;
  if (!ListDirectory(log_dir, &entries))
    return;

  for (const auto& entry : entries) {
    if (entry.is_dir)
      continue;
    if (!IsMyLogFile(entry.name))
      continue;

    string fullname = file::JoinPath(log_dir, entry.name);
#ifndef _WIN32
    char real_fullname[PATH_MAX];
    if (realpath(fullname.c_str(), real_fullname) == nullptr) {
      VLOG(1) << "realpath:" << fullname;
      continue;
    }
    string log_filename = real_fullname;
#else
    string log_filename = fullname;
#endif

    FileStat file_stat(log_filename);
    if (!file_stat.IsValid()) {
      LOG(ERROR) << "Failed to get file id:" << log_filename;
    } else if (*file_stat.mtime < time) {
      VLOG(1) << "old log:" << log_filename;
      old_logs->insert(log_filename);
    } else {
      VLOG(1) << "new log:" << log_filename;
    }
  }
}

bool LogCleaner::IsMyLogFile(const string& name) const {
  static const char *kLogLevel[] = {
    "INFO", "WARNING", "ERROR", "FATAL"
  };
  for (const auto& basename : basenames_) {
    if (absl::StartsWith(name, basename) &&
        name.size() > basename.size() &&
        name[basename.size()] == '.') {
      for (const auto& level : kLogLevel) {
        if (strstr(name.c_str(), level) != nullptr) {
          return true;
        }
      }
    }
  }
  return false;
}

}  // namespace devtools_goma
