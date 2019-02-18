// Copyright 2019 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "task/local_output_file_task.h"

#include "absl/time/clock.h"
#include "callback.h"
#include "compile_task.h"
#include "file_hash_cache.h"
#include "glog/logging.h"
#include "worker_thread_manager.h"

namespace devtools_goma {

LocalOutputFileTask::LocalOutputFileTask(WorkerThreadManager* wm,
                    std::unique_ptr<BlobClient::Uploader> blob_uploader,
                    FileHashCache* file_hash_cache,
                    const FileStat& file_stat,
                    CompileTask* task,
                    string filename)
    : wm_(wm),
      thread_id_(wm_->GetCurrentThreadId()),
      blob_uploader_(std::move(blob_uploader)),
      file_hash_cache_(file_hash_cache),
      file_stat_(file_stat),
      task_(task),
      filename_(std::move(filename)),
      success_(false) {
  timer_.Start();
  task_->StartLocalOutputFileTask();
}

LocalOutputFileTask::~LocalOutputFileTask() {
  task_->MaybeRunLocalOutputFileCallback(true);
}

void LocalOutputFileTask::Run(OneshotClosure* closure) {
  // Store hash_key of output file.  This file would be used in link phase.
  VLOG(1) << task_->trace_id() << " local output " << filename_;
  success_ = blob_uploader_->Upload();
  if (success_) {
    string hash_key = blob_uploader_->hash_key();
    bool new_cache_key = file_hash_cache_->StoreFileCacheKey(
        filename_, hash_key, absl::Now(), file_stat_);
    if (new_cache_key) {
      LOG(INFO) << task_->trace_id()
                << " local output store:" << filename_
                << " size=" << file_stat_.size;
      success_ = blob_uploader_->Store();
    }
  }
  if (!success_) {
    LOG(WARNING) << task_->trace_id()
                 << " local output read failed:" << filename_;
  }
  wm_->RunClosureInThread(FROM_HERE, thread_id_, closure,
                          WorkerThread::PRIORITY_LOW);
}

}  // namespace devtools_goma
