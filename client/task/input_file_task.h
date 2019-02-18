// Copyright 2019 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_TASK_INPUT_FILE_TASK_H_
#define DEVTOOLS_GOMA_CLIENT_TASK_INPUT_FILE_TASK_H_

#include <memory>
#include <unordered_map>

#include "absl/memory/memory.h"
#include "absl/time/time.h"
#include "base/basictypes.h"
#include "base/lockhelper.h"
#include "compile_task.h"
#include "file_hash_cache.h"
#include "goma_blob.h"
#include "prototmp/goma_data.pb.h"
#include "simple_timer.h"
#include "worker_thread_manager.h"

namespace devtools_goma {

class InputFileTask {
 public:
  // Gets InputFileTask for the filename.
  // If an InputFileTask for the same filename already exists, use the same
  // InputFileTask.
  static InputFileTask* NewInputFileTask(
      WorkerThreadManager* wm,
      std::unique_ptr<BlobClient::Uploader> blob_uploader,
      FileHashCache* file_hash_cache,
      const FileStat& file_stat,
      const string& filename,
      bool missed_content,
      bool linking,
      bool is_new_file,
      const string& old_hash_key,
      CompileTask* task,
      ExecReq_Input* input);

  void Run(CompileTask* task, OneshotClosure* closure);

  void Done(CompileTask* task);

  const string& filename() const { return filename_; }
  bool missed_content() const { return missed_content_; }
  bool need_hash_only() const { return need_hash_only_; }
  const absl::optional<absl::Time>& mtime() const { return file_stat_.mtime; }
  const SimpleTimer& timer() const { return timer_; }
  ssize_t file_size() const { return file_stat_.size; }
  const string& old_hash_key() const { return old_hash_key_; }
  const string& hash_key() const { return blob_uploader_->hash_key(); }
  bool success() const { return success_; }
  bool new_cache_key() const { return new_cache_key_; }

  size_t num_tasks() const {
    AUTOLOCK(lock, &mu_);
    return tasks_.size();
  }

  bool UpdateInputInTask(CompileTask* task) const;

  ExecReq_Input* GetInputForTask(CompileTask* task) const;

  bool need_to_compute_key() const;

  bool need_to_upload_content(absl::string_view hash_key) const;

  const char* upload_reason(absl::string_view hash_key) const;

  const HttpClient::Status& http_status() const {
    // TODO: blob_uploader should support this API?
    return blob_uploader_->http_status();
  }

 private:
  enum State {
    INIT,
    RUN,
    DONE,
  };

  InputFileTask(WorkerThreadManager* wm,
                std::unique_ptr<BlobClient::Uploader> blob_uploader,
                FileHashCache* file_hash_cache,
                const FileStat& file_stat,
                string filename,
                bool missed_content,
                bool linking,
                bool is_new_file,
                string old_hash_key);
  ~InputFileTask();

  void SetTaskInput(CompileTask* task, ExecReq_Input* input);

  static void InitializeStaticOnce();

  WorkerThreadManager* wm_;
  std::unique_ptr<BlobClient::Uploader> blob_uploader_;
  FileHashCache* file_hash_cache_;
  const FileStat file_stat_;

  const string filename_;
  State state_;

  mutable Lock mu_;
  std::map<CompileTask*, ExecReq_Input*> tasks_ GUARDED_BY(mu_);
  std::vector<std::pair<WorkerThread::ThreadId, OneshotClosure*>> callbacks_
      GUARDED_BY(mu_);

  // true if goma servers couldn't find the content, so we must upload it.
  const bool missed_content_;

  // true if we'll use hash key only in ExecReq to prevent from bloating it.
  // false to embed content in ExecReq.
  bool need_hash_only_;

  // true if the file is considered as new file, so the file might not be
  // in goma cache yet.
  // false means the file is old enough, so we could think someone else already
  // uploaded the content in goma cache.
  const bool is_new_file_;

  // hash key stored in file_hash_cache.
  const string old_hash_key_;

  SimpleTimer timer_;

  // true if goma file ops is succeeded.
  bool success_;

  // true if the hash_key_ is first inserted in file hash cache.
  bool new_cache_key_;

  static absl::once_flag init_once_;

  static Lock global_mu_;
  static std::unordered_map<string, InputFileTask*>* task_by_filename_
      GUARDED_BY(global_mu_);

  DISALLOW_COPY_AND_ASSIGN(InputFileTask);
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_TASK_INPUT_FILE_TASK_H_
