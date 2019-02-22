// Copyright 2019 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "task/input_file_task.h"

#include "absl/base/call_once.h"
#include "absl/strings/match.h"
#include "absl/time/clock.h"
#include "compile_task.h"
#include "glog/logging.h"
#include "path.h"

namespace devtools_goma {

namespace {

// For file sizes no larger than this limit, embed it in the request instead of
// uploading separately.
constexpr size_t kLargeFileThreshold = 2 * 1024 * 1024UL;  // 2MB

// For file sizes smaller than this limit, embed it in the request even if only
// the hash key was requested.
constexpr size_t kTinyFileThreshold = 512;

}  // anonymous namespace

// static
InputFileTask* InputFileTask::NewInputFileTask(
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
    ExecReq_Input* input) {
  DCHECK(file::IsAbsolutePath(filename)) << filename;

  absl::call_once(init_once_, &InputFileTask::InitializeStaticOnce);

  InputFileTask* input_file_task = nullptr;
  {
    AUTOLOCK(lock, &global_mu_);
    std::pair<std::unordered_map<string, InputFileTask*>::iterator, bool> p =
        task_by_filename_->insert(std::make_pair(filename, input_file_task));
    if (p.second) {
      p.first->second = new InputFileTask(
          wm, std::move(blob_uploader), file_hash_cache, file_stat, filename,
          missed_content, linking, is_new_file, old_hash_key);
    }
    input_file_task = p.first->second;
    DCHECK(input_file_task != nullptr);
    input_file_task->SetTaskInput(task, input);
  }
  DCHECK_GT(input_file_task->num_tasks(), 0U);
  VLOG(1) << task->trace_id_ << " start input " << task->num_input_file_task_
          << " " << filename;
  task->StartInputFileTask();
  return input_file_task;
}

void InputFileTask::Run(CompileTask* task, OneshotClosure* closure) {
  WorkerThread::ThreadId thread_id = task->thread_id_;
  {
    AUTOLOCK(lock, &mu_);
    switch (state_) {
      case INIT:  // first run.
        state_ = RUN;
        break;
      case RUN:
        VLOG(1) << task->trace_id() << " input running (" << tasks_.size()
                << " tasks)";
        callbacks_.emplace_back(thread_id, closure);
        return;
      case DONE:
        VLOG(1) << task->trace_id() << " input done";
        wm_->RunClosureInThread(FROM_HERE, thread_id, closure,
                                WorkerThread::PRIORITY_LOW);
        return;
    }
  }

  if (missed_content_) {
    LOG(INFO) << task->trace_id() << " (" << num_tasks() << " tasks)"
              << " input " << filename_ << " [missed content]";
  } else {
    VLOG(1) << task->trace_id() << " (" << num_tasks() << " tasks)"
            << " input " << filename_;
  }
  bool uploaded_in_side_channel = false;
  // TODO: use string_view in file_hash_cache methods.
  string hash_key = old_hash_key_;
  if (need_to_compute_key()) {
    VLOG(1) << task->trace_id() << " (" << num_tasks() << " tasks)"
            << " compute hash key:" << filename_ << " size:" << file_stat_.size;
    success_ = blob_uploader_->ComputeKey();
    if (success_) {
      hash_key = blob_uploader_->hash_key();
      new_cache_key_ = !file_hash_cache_->IsKnownCacheKey(hash_key);
    }
  }

  if (need_to_upload_content(hash_key)) {
    if (need_hash_only_ || file_stat_.size > kLargeFileThreshold) {
      // upload in side channel.
      LOG(INFO) << task->trace_id() << "(" << num_tasks() << " tasks)"
                << " upload:" << filename_ << " size:" << file_stat_.size
                << " reason:" << upload_reason(hash_key);
      success_ = blob_uploader_->Upload();
      if (success_) {
        uploaded_in_side_channel = true;
      }
    } else {
      // upload embedded.
      LOG(INFO) << task->trace_id() << " (" << num_tasks() << " tasks)"
                << " embed:" << filename_ << " size:" << file_stat_.size
                << " reason:" << upload_reason(hash_key);
      success_ = blob_uploader_->Embed();
    }
  } else if (file_stat_.size < kTinyFileThreshold) {
    // For small size of file blob, embed it even if the compile task
    // requested hash key only.
    LOG(INFO) << task->trace_id() << " (" << num_tasks() << " tasks)"
              << " embed:" << filename_ << " size:" << file_stat_.size
              << " reason:small";
    need_hash_only_ = false;
    success_ = blob_uploader_->Embed();
  } else {
    VLOG(1) << task->trace_id() << " (" << num_tasks() << " tasks)"
            << " hash only:" << filename_ << " size:" << file_stat_.size
            << " missed_content:" << missed_content_
            << " is_new_file:" << is_new_file_
            << " new_cache_key:" << new_cache_key_ << " success:" << success_;
  }

  if (!success_) {
    LOG(WARNING) << task->trace_id() << " (" << num_tasks() << " tasks)"
                 << " input file failed:" << filename_;
  } else {
    hash_key = blob_uploader_->hash_key();
    CHECK(!hash_key.empty())
        << task->trace_id() << " (" << num_tasks() << " tasks)"
        << " no hash key?" << filename_;
    // Stores file cache key only if we have already uploaded the blob
    // in side channel, or we assume the blob has already been uploaded
    // since it's old enough.
    // When we decide to upload the blob by embedding it to the request,
    // we have to store file cache key after the compile request without no
    // missing inputs error. If missing inputs error happens, it's safer to
    // resend the blob since we might send the second request to
    // the different cluster. That cluster might not have the cache.
    // If blob is old enough, we assume that the file has already been
    // uploaded. In that case, we register file hash id to
    // |file_hash_cache_|.
    // See b/11261931
    //     b/12087209
    if (uploaded_in_side_channel || !is_new_file_) {
      // Set upload_timestamp_ms only if we have uploaded the content.
      absl::optional<absl::Time> upload_timestamp_ms;
      if (uploaded_in_side_channel) {
        upload_timestamp_ms = absl::Now();
      }
      new_cache_key_ = file_hash_cache_->StoreFileCacheKey(
          filename_, hash_key, upload_timestamp_ms, file_stat_);
      VLOG(1) << task->trace_id() << " (" << num_tasks() << " tasks)"
              << " input file ok: " << filename_
              << (uploaded_in_side_channel ? " upload" : " hash only");
    } else {
      VLOG(1) << task->trace_id() << " (" << num_tasks() << " tasks)"
              << " input file ok: " << filename_
              << (new_cache_key_ ? " embedded upload" : " already uploaded");
    }
  }

  {
    AUTOLOCK(lock, &global_mu_);
    std::unordered_map<string, InputFileTask*>::iterator found =
        task_by_filename_->find(filename_);
    DCHECK(found != task_by_filename_->end());
    DCHECK(found->second == this);
    task_by_filename_->erase(found);
    VLOG(1) << task->trace_id() << " (" << num_tasks() << " tasks)"
            << " clear task by filename" << filename_;
  }
  std::vector<std::pair<WorkerThread::ThreadId, OneshotClosure*>> callbacks;

  {
    AUTOLOCK(lock, &mu_);
    DCHECK_EQ(RUN, state_);
    state_ = DONE;
    callbacks.swap(callbacks_);
  }
  wm_->RunClosureInThread(FROM_HERE, thread_id, closure,
                          WorkerThread::PRIORITY_LOW);
  for (const auto& callback : callbacks)
    wm_->RunClosureInThread(FROM_HERE, callback.first, callback.second,
                            WorkerThread::PRIORITY_LOW);
}

void InputFileTask::Done(CompileTask* task) {
  bool all_finished = false;
  {
    AUTOLOCK(lock, &mu_);
    std::map<CompileTask*, ExecReq_Input*>::iterator found = tasks_.find(task);
    CHECK(found != tasks_.end());
    tasks_.erase(found);
    all_finished = tasks_.empty();
  }
  task->MaybeRunInputFileCallback(true);
  if (all_finished)
    delete this;
}

bool InputFileTask::UpdateInputInTask(CompileTask* task) const {
  ExecReq_Input* input = GetInputForTask(task);
  CHECK(input != nullptr) << task->trace_id() << " filename:" << filename_;
  return blob_uploader_->GetInput(input);
}

ExecReq_Input* InputFileTask::GetInputForTask(CompileTask* task) const {
  AUTOLOCK(lock, &mu_);
  std::map<CompileTask*, ExecReq_Input*>::const_iterator found =
      tasks_.find(task);
  if (found != tasks_.end()) {
    return found->second;
  }
  return nullptr;
}

bool InputFileTask::need_to_compute_key() const {
  if (need_to_upload_content(old_hash_key_)) {
    // we'll calculate hash key during uploading.
    return false;
  }
  return file_stat_.size >= kTinyFileThreshold;
}

bool InputFileTask::need_to_upload_content(absl::string_view hash_key) const {
  if (missed_content_) {
    return true;
  }
  if (absl::EndsWith(filename_, ".rsp")) {
    return true;
  }
  if (is_new_file_) {
    if (new_cache_key_) {
      return true;
    }
  }
  if (old_hash_key_.empty()) {
    // old file and first check. we assume the file was already uploaded.
    return false;
  }
  return old_hash_key_ != hash_key;
}

const char* InputFileTask::upload_reason(absl::string_view hash_key) const {
  if (missed_content_) {
    return "missed content";
  }
  if (absl::EndsWith(filename_, ".rsp")) {
    return "rsp file";
  }
  if (is_new_file_) {
    if (new_cache_key_) {
      return "new file cache_key";
    }
  }
  if (old_hash_key_.empty()) {
    return "no need to upload - maybe already in cache.";
  }
  if (old_hash_key_ != hash_key) {
    return "update cache_key";
  }
  return "no need to upload - cache_key matches";
}

InputFileTask::InputFileTask(
    WorkerThreadManager* wm,
    std::unique_ptr<BlobClient::Uploader> blob_uploader,
    FileHashCache* file_hash_cache,
    const FileStat& file_stat,
    string filename,
    bool missed_content,
    bool linking,
    bool is_new_file,
    string old_hash_key)
    : wm_(wm),
      blob_uploader_(std::move(blob_uploader)),
      file_hash_cache_(file_hash_cache),
      file_stat_(file_stat),
      filename_(std::move(filename)),
      state_(INIT),
      missed_content_(missed_content),
      need_hash_only_(linking),  // we need hash key only in linking.
      is_new_file_(is_new_file),
      old_hash_key_(std::move(old_hash_key)),
      success_(false),
      new_cache_key_(false) {
  timer_.Start();
}

InputFileTask::~InputFileTask() {
  CHECK(tasks_.empty());
}

void InputFileTask::SetTaskInput(CompileTask* task, ExecReq_Input* input) {
  AUTOLOCK(lock, &mu_);
  tasks_.insert(std::make_pair(task, input));
}

void InputFileTask::InitializeStaticOnce() {
  AUTOLOCK(lock, &global_mu_);
  task_by_filename_ = new std::unordered_map<string, InputFileTask*>;
}

// static
absl::once_flag InputFileTask::init_once_;

// static
Lock InputFileTask::global_mu_;

// static
std::unordered_map<string, InputFileTask*>* InputFileTask::task_by_filename_;

}  // namespace devtools_goma
