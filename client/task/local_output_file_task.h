// Copyright 2019 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_TASK_LOCAL_OUTPUT_FILE_TASK_H_
#define DEVTOOLS_GOMA_CLIENT_TASK_LOCAL_OUTPUT_FILE_TASK_H_

#include <memory>
#include <string>

#include "base/basictypes.h"
#include "file_stat.h"
#include "goma_blob.h"
#include "simple_timer.h"

using std::string;

namespace devtools_goma {

class CompileTask;
class FileHashCache;
class OneshotClosure;
class WorkerThreadManager;

class LocalOutputFileTask {
 public:
  LocalOutputFileTask(WorkerThreadManager* wm,
                      std::unique_ptr<BlobClient::Uploader> blob_uploader,
                      FileHashCache* file_hash_cache,
                      const FileStat& file_stat,
                      CompileTask* task,
                      string filename);
  ~LocalOutputFileTask();

  void Run(OneshotClosure* closure);

  CompileTask* task() const { return task_; }
  const string& filename() const { return filename_; }
  const SimpleTimer& timer() const { return timer_; }
  const FileStat& file_stat() const { return file_stat_; }
  bool success() const { return success_; }

 private:
  WorkerThreadManager* wm_;
  WorkerThread::ThreadId thread_id_;
  std::unique_ptr<BlobClient::Uploader> blob_uploader_;
  FileHashCache* file_hash_cache_;
  const FileStat file_stat_;
  CompileTask* task_;
  const string filename_;
  SimpleTimer timer_;
  bool success_;

  DISALLOW_COPY_AND_ASSIGN(LocalOutputFileTask);
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_TASK_LOCAL_OUTPUT_FILE_TASK_H_
