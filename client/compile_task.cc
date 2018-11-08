// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "compile_task.h"

#ifndef _WIN32
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#endif

#include <algorithm>
#include <iomanip>
#include <memory>
#include <sstream>
#include <unordered_map>
#include <utility>

#include <google/protobuf/text_format.h>
#include <json/json.h>

#include "absl/base/call_once.h"
#include "absl/memory/memory.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/time/clock.h"
#include "autolock_timer.h"
#include "callback.h"
#include "clang_tidy_flags.h"
#include "compilation_database_reader.h"
#include "compile_service.h"
#include "compile_stats.h"
#include "compiler_flag_type_specific.h"
#include "compiler_flags.h"
#include "compiler_flags_parser.h"
#include "compiler_flags_util.h"
#include "compiler_info.h"
#include "compiler_proxy_info.h"
#include "compiler_specific.h"
#include "compiler_type_specific_collection.h"
#include "cxx/include_processor/cpp_include_processor.h"
#include "cxx/include_processor/include_file_utils.h"
#include "file_dir.h"
#include "file_hash_cache.h"
#include "file_helper.h"
#include "file_path_util.h"
#include "filesystem.h"
#include "gcc_flags.h"
#include "glog/logging.h"
#include "glog/stl_logging.h"
#include "goma_blob.h"
#include "goma_data_util.h"
#include "goma_file.h"
#include "goma_file_dump.h"
#include "goma_file_http.h"
#include "http_rpc.h"
#include "ioutil.h"
#include "java/jar_parser.h"
#include "java_flags.h"
#include "linker/linker_input_processor/linker_input_processor.h"
#include "linker/linker_input_processor/thinlto_import_processor.h"
#include "local_output_cache.h"
#include "lockhelper.h"
#include "multi_http_rpc.h"
#include "mypath.h"
#include "path.h"
#include "path_resolver.h"
#include "path_util.h"
#include "simple_timer.h"
#include "subprocess_task.h"
#include "time_util.h"
#include "util.h"
#include "vc_flags.h"
#include "worker_thread.h"

MSVC_PUSH_DISABLE_WARNING_FOR_PROTO()
#include "prototmp/goma_data.pb.h"
#include "prototmp/subprocess.pb.h"
MSVC_POP_WARNING()

#ifdef _WIN32
# include "posix_helper_win.h"
#endif

namespace devtools_goma {

namespace {

constexpr int kMaxExecRetry = 4;

string GetLastErrorMessage() {
  char error_message[1024];
#ifndef _WIN32
  // Meaning of returned value of strerror_r is different between
  // XSI and GNU. Need to ignore.
  (void)strerror_r(errno, error_message, sizeof(error_message));
#else
  FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, 0, GetLastError(), 0,
                 error_message, sizeof error_message, 0);
#endif
  return error_message;
}

bool IsFatalError(ExecResp::ExecError error_code) {
  return error_code == ExecResp::BAD_REQUEST;
}

void DumpSubprograms(
    const google::protobuf::RepeatedPtrField<SubprogramSpec>& subprogram_specs,
    std::ostringstream* ss) {
  for (int i = 0; i < subprogram_specs.size(); ++i) {
    const SubprogramSpec& spec = subprogram_specs.Get(i);
    if (i > 0)
      *ss << ", ";
    *ss << "path=" << spec.path() << " hash=" << spec.binary_hash();
  }
}

void LogCompilerOutput(const string& trace_id, const string& name,
                       absl::string_view out) {
  LOG(INFO) << trace_id << " " << name << ": size=" << out.size();
  static const int kMaxLines = 32;
  static const size_t kMaxCols = 200;
  static const char* kClExeShowIncludePrefix = "Note: including file:";
  if (out.size() == 0)
    return;
  if (out.size() < kMaxCols) {
    LOG(INFO) << trace_id << " " << name << ":" << out;
    return;
  }
  for (int i = 0; out.size() > 0 && i < kMaxLines;) {
    size_t end = out.find_first_of("\r\n");
    absl::string_view line;
    if (end == string::npos) {
      line = out;
      out = absl::string_view();
    } else if (end == 0) {
      out.remove_prefix(1);
      continue;
    } else {
      line = out.substr(0, end);
      out.remove_prefix(end + 1);
    }
    if (line.size() == 0)
      continue;
    if (absl::StartsWith(line, kClExeShowIncludePrefix))
      continue;
    size_t found = line.find("error");
    if (found == string::npos)
      found = line.find("warning");
    if (found != string::npos) {
      ++i;
      if (line.size() > kMaxCols) {
        LOG(INFO) << trace_id << " " << name << ":"
                  << line.substr(0, kMaxCols) << "...";
      } else {
        LOG(INFO) << trace_id << " " << name << ":" << line;
      }
    }
  }
}

void ReleaseMemoryForExecReqInput(ExecReq* req) {
  ExecReq new_req;
  new_req.Swap(req);
  new_req.clear_input();
  *req = new_req;
}

string CreateCommandVersionString(const CommandSpec& spec) {
  return spec.name() + ' ' + spec.version() + " (" + spec.binary_hash() + ")";
}

string StateName(CompileTask::State state) {
  static const char* names[] = {
    "INIT",
    "SETUP",
    "FILE_REQ",
    "CALL_EXEC",
    "LOCAL_OUTPUT",
    "FILE_RESP",
    "FINISHED",
    "LOCAL_RUN",
    "LOCAL_FINISHED",
  };

  static_assert(CompileTask::NUM_STATE == arraysize(names),
                "CompileTask::NUM_STATE and arraysize(names) is not matched");

  CHECK_GE(state, 0);
  CHECK_LT(state, CompileTask::NUM_STATE);
  return names[state];
}

template <typename Iter>
void NormalizeSystemIncludePaths(const string& home, const string& cwd,
                                 Iter path_begin, Iter path_end) {
  if (home.empty())
    return;

  for (Iter it = path_begin; it != path_end; ++it) {
    if (HasPrefixDir(*it, home)) {
      it->assign(PathResolver::WeakRelativePath(*it, cwd));
    }
  }
}

// Returns true if |buf| is bigobj format header.
// |buf| should contain 32 byte at least.
bool IsBigobjFormat(const unsigned char* buf) {
  static const unsigned char kV1UUID[16] = {
    0x38, 0xFE, 0xB3, 0x0C, 0xA5, 0xD9, 0xAB, 0x4D,
    0xAC, 0x9B, 0xD6, 0xB6, 0x22, 0x26, 0x53, 0xC2,
  };

  static const unsigned char kV2UUID[16] = {
    0xC7, 0xA1, 0xBA, 0xD1, 0xEE, 0xBA, 0xA9, 0x4B,
    0xAF, 0x20, 0xFA, 0xF6, 0x6A, 0xA4, 0xDC, 0xB8
  };

  if (*reinterpret_cast<const unsigned short*>(buf) != 0)
    return false;
  if (*reinterpret_cast<const unsigned short*>(buf + 2) != 0xFFFF)
    return false;

  // UUID can be different by bigobj version.
  const unsigned char* uuid = nullptr;
  if (*reinterpret_cast<const unsigned short*>(buf + 4) == 0x0001) {
    uuid = kV1UUID;
  } else if (*reinterpret_cast<const unsigned short*>(buf + 4) == 0x0002) {
    uuid = kV2UUID;
  } else {
    // Unknown bigobj version
    return false;
  }

  unsigned short magic = *reinterpret_cast<const unsigned short*>(buf + 6);
  if (!(magic == 0x014C || magic == 0x8664))
    return false;

  for (int i = 0; i < 16; ++i) {
    if (buf[12 + i] != uuid[i])
      return false;
  }

  return true;
}

}  // namespace

absl::once_flag CompileTask::init_once_;
Lock CompileTask::global_mu_;

std::deque<CompileTask*>* CompileTask::link_file_req_tasks_ = nullptr;

class CompileTask::InputFileTask {
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
      ExecReq_Input* input) {
    DCHECK(file::IsAbsolutePath(filename)) << filename;

    absl::call_once(init_once_,
                    &CompileTask::InputFileTask::InitializeStaticOnce);

    InputFileTask* input_file_task = nullptr;
    {
      AUTOLOCK(lock, &global_mu_);
      std::pair<std::unordered_map<string, InputFileTask*>::iterator, bool> p =
          task_by_filename_->insert(std::make_pair(filename, input_file_task));
      if (p.second) {
        p.first->second = new InputFileTask(
            wm, std::move(blob_uploader), file_hash_cache, file_stat,
            filename, missed_content, linking, is_new_file, old_hash_key);
      }
      input_file_task = p.first->second;
      DCHECK(input_file_task != nullptr);
      input_file_task->SetTaskInput(task, input);
    }
    DCHECK_GT(input_file_task->num_tasks(), 0U);
    VLOG(1) << task->trace_id_ << " start input "
            << task->num_input_file_task_ << " " << filename;
    task->StartInputFileTask();
    return input_file_task;
  }

  void Run(CompileTask* task, OneshotClosure* closure) {
    WorkerThread::ThreadId thread_id = task->thread_id_;
    {
      AUTOLOCK(lock, &mu_);
      switch (state_) {
        case INIT:  // first run.
          state_ = RUN;
          break;
        case RUN:
          VLOG(1) << task->trace_id() << " input running ("
                  << tasks_.size() << " tasks)";
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
      VLOG(1) << task->trace_id()
              << " (" << num_tasks() << " tasks)"
              << " compute hash key:" << filename_
              << " size:" << file_stat_.size;
      success_ = blob_uploader_->ComputeKey();
      if (success_) {
        hash_key = blob_uploader_->hash_key();
        new_cache_key_ = !file_hash_cache_->IsKnownCacheKey(hash_key);
      }
    }

    if (need_to_upload_content(hash_key)) {
      if (need_hash_only_ || file_stat_.size > 2*1024*1024) {
        // upload in side channel.
        LOG(INFO) << task->trace_id()
                  << "(" << num_tasks() << " tasks)"
                  << " upload:" << filename_
                  << " size:" << file_stat_.size
                  << " reason:" << upload_reason(hash_key);
        success_ = blob_uploader_->Upload();
        if (success_) {
          uploaded_in_side_channel = true;
        }
      } else {
        // upload embedded.
        LOG(INFO) << task->trace_id()
                  << " (" << num_tasks() << " tasks)"
                  << " embed:" << filename_
                  << " size:" << file_stat_.size
                  << " reason:" << upload_reason(hash_key);
        success_ = blob_uploader_->Embed();
      }
    } else if (file_stat_.size < 512) {
      // For small size of file blob, embed it even if the copmile task
      // requested hash key only.
      LOG(INFO) << task->trace_id()
                << " (" << num_tasks() << " tasks)"
                << " embed:" << filename_
                << " size:" << file_stat_.size
                << " reason:small";
      need_hash_only_ = false;
      success_ = blob_uploader_->Embed();
    } else {
      VLOG(1) << task->trace_id()
              << " (" << num_tasks() << " tasks)"
              << " hash only:" << filename_
              << " size:" << file_stat_.size
              << " missed_content:" << missed_content_
              << " is_new_file:" << is_new_file_
              << " new_cache_key:" << new_cache_key_
              << " success:" << success_;
    }

    if (!success_) {
      LOG(WARNING) << task->trace_id()
                   << " (" << num_tasks() << " tasks)"
                   << " input file failed:" << filename_;
    } else {
      hash_key = blob_uploader_->hash_key();
      CHECK(!hash_key.empty())
          << task->trace_id()
          << " (" << num_tasks() << " tasks)"
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
                << (new_cache_key_ ? " embedded upload"
                    : " already uploaded");
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
    std::vector<std::pair<WorkerThread::ThreadId,
                          OneshotClosure*>> callbacks;

    {
      AUTOLOCK(lock, &mu_);
      DCHECK_EQ(RUN, state_);
      state_ = DONE;
      callbacks.swap(callbacks_);
    }
    wm_->RunClosureInThread(FROM_HERE, thread_id, closure,
                            WorkerThread::PRIORITY_LOW);
    for (const auto& callback : callbacks)
      wm_->RunClosureInThread(FROM_HERE,
                              callback.first, callback.second,
                              WorkerThread::PRIORITY_LOW);
  }

  void Done(CompileTask* task) {
    bool all_finished = false;
    {
      AUTOLOCK(lock, &mu_);
      std::map<CompileTask*, ExecReq_Input*>::iterator found =
          tasks_.find(task);
      CHECK(found != tasks_.end());
      tasks_.erase(found);
      all_finished = tasks_.empty();
    }
    task->MaybeRunInputFileCallback(true);
    if (all_finished)
      delete this;
  }

  const string& filename() const { return filename_; }
  bool missed_content() const { return missed_content_; }
  bool need_hash_only() const { return need_hash_only_; }
  const absl::optional<absl::Time>& mtime() const {
    return file_stat_.mtime;
  }
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

  bool UpdateInputInTask(CompileTask* task) const {
    ExecReq_Input* input = GetInputForTask(task);
    CHECK(input != nullptr) << task->trace_id() << " filename:" << filename_;
    return blob_uploader_->GetInput(input);
  }

  ExecReq_Input* GetInputForTask(CompileTask* task) const {
    AUTOLOCK(lock, &mu_);
    std::map<CompileTask*, ExecReq_Input*>::const_iterator found =
        tasks_.find(task);
    if (found != tasks_.end()) {
      return found->second;
    }
    return nullptr;
  }

  bool need_to_compute_key() const {
    if (need_to_upload_content(old_hash_key_)) {
      // we'll calculate hash key during uploading.
      return false;
    }
    return file_stat_.size >= 512;
  }

  bool need_to_upload_content(absl::string_view hash_key) const {
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

  const char* upload_reason(absl::string_view hash_key) const {
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
  ~InputFileTask() {
    CHECK(tasks_.empty());
  }

  void SetTaskInput(CompileTask* task, ExecReq_Input* input) {
    AUTOLOCK(lock, &mu_);
    tasks_.insert(std::make_pair(task, input));
  }

  static void InitializeStaticOnce() {
    AUTOLOCK(lock, &global_mu_);
    task_by_filename_ = new std::unordered_map<string, InputFileTask*>;
  }

  WorkerThreadManager* wm_;
  std::unique_ptr<BlobClient::Uploader> blob_uploader_;
  FileHashCache* file_hash_cache_;
  const FileStat file_stat_;

  const string filename_;
  State state_;

  mutable Lock mu_;
  std::map<CompileTask*, ExecReq_Input*> tasks_ GUARDED_BY(mu_);
  std::vector<std::pair<WorkerThread::ThreadId, OneshotClosure*>>
      callbacks_ GUARDED_BY(mu_);

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

absl::once_flag CompileTask::InputFileTask::init_once_;
Lock CompileTask::InputFileTask::global_mu_;

std::unordered_map<string, CompileTask::InputFileTask*>*
    CompileTask::InputFileTask::task_by_filename_;

// Returns true if all outputs are FILE blob (so no need of further http_rpc).
bool IsOutputFileEmbedded(const ExecResult& result) {
  for (const auto& output : result.output()) {
    if (output.blob().blob_type() != FileBlob::FILE)
      return false;
  }
  return true;
}

// TODO: move to BlobClient::Downloader?
struct CompileTask::OutputFileInfo {
  OutputFileInfo() : mode(0666), size(0) {}
  // actual output filename.
  string filename;
  // file mode/permission.
  int mode;

  size_t size;

  // tmp_filename is filename written by OutputFileTask.
  // tmp_filename may be the same as output filename (when !need_rename), or
  // rename it to real output filename in CommitOutput().
  // if tmp file was not written in OutputFileTask, because it holds content
  // in content field, tmp_filename will be "".
  string tmp_filename;

  // hash_key is hash of output filename. It will be stored in file hash cache
  // once output file is committed.
  // TODO: fix this to support cas digest.
  string hash_key;

  // content is output content.
  // it is used to hold output content in memory while output file task.
  // it will be used iff tmp_filename == "".
  string content;
};

class CompileTask::OutputFileTask {
 public:
  // Doesn't take ownership of |info|.
  OutputFileTask(WorkerThreadManager* wm,
                 std::unique_ptr<BlobClient::Downloader> blob_downloader,
                 CompileTask* task,
                 int output_index,
                 const ExecResult_Output& output,
                 OutputFileInfo* info)
      : wm_(wm),
        thread_id_(wm->GetCurrentThreadId()),
        blob_downloader_(std::move(blob_downloader)),
        task_(task),
        output_index_(output_index),
        output_(output),
        output_size_(output.blob().file_size()),
        info_(info),
        success_(false) {
    timer_.Start();
    task_->StartOutputFileTask();
  }
  ~OutputFileTask() {
    task_->MaybeRunOutputFileCallback(output_index_, true);
  }

  void Run(OneshotClosure* closure) {
    VLOG(1) << task_->trace_id() << " output " << info_->filename;
    if (info_->tmp_filename.empty()) {
      success_ = blob_downloader_->DownloadInBuffer(output_, &info_->content);
    } else {
      // TODO: We might want to restrict paths this program may write?
      success_ =
          blob_downloader_->Download(output_, info_->tmp_filename, info_->mode);
    }
    if (success_) {
      // TODO: fix to support cas digest.
      info_->hash_key = FileServiceClient::ComputeHashKey(output_.blob());
    } else {
      LOG(WARNING) << task_->trace_id()
                   << " " << (task_->cache_hit() ? "cached" : "no-cached")
                   << " output file failed:" << info_->filename;
    }
    wm_->RunClosureInThread(FROM_HERE, thread_id_, closure,
                            WorkerThread::PRIORITY_LOW);
  }

  CompileTask* task() const { return task_; }
  const ExecResult_Output& output() const { return output_; }
  const SimpleTimer& timer() const { return timer_; }
  bool success() const { return success_; }
  bool IsInMemory() const {
    return info_->tmp_filename.empty();
  }

  int num_rpc() const {
    // TODO: need this?
    return blob_downloader_->num_rpc();
  }
  const HttpClient::Status& http_status() const {
    // TODO: blob_uploader should support this API?
    return blob_downloader_->http_status();
  }

 private:
  WorkerThreadManager* wm_;
  WorkerThread::ThreadId thread_id_;
  std::unique_ptr<BlobClient::Downloader> blob_downloader_;
  CompileTask* task_;
  int output_index_;
  const ExecResult_Output& output_;
  size_t output_size_;
  OutputFileInfo* info_;
  SimpleTimer timer_;
  bool success_;

  DISALLOW_COPY_AND_ASSIGN(OutputFileTask);
};

class CompileTask::LocalOutputFileTask {
 public:
  LocalOutputFileTask(WorkerThreadManager* wm,
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
  ~LocalOutputFileTask() {
    task_->MaybeRunLocalOutputFileCallback(true);
  }

  void Run(OneshotClosure* closure) {
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

/* static */
void CompileTask::InitializeStaticOnce() {
  AUTOLOCK(lock, &global_mu_);
  link_file_req_tasks_ = new std::deque<CompileTask*>;
}

CompileTask::CompileTask(CompileService* service, int id)
    : service_(service),
      id_(id),
      rpc_(nullptr),
      caller_thread_id_(service->wm()->GetCurrentThreadId()),
      done_(nullptr),
      stats_(new CompileStats),
      response_code_(0),
      state_(INIT),
      abort_(false),
      finished_(false),
      req_(new ExecReq),
      linking_(false),
      precompiling_(false),
      compiler_type_specific_(nullptr),
      gomacc_pid_(SubProcessState::kInvalidPid),
      canceled_(false),
      resp_(new ExecResp),
      exit_status_(0),
      http_rpc_status_(absl::make_unique<HttpRPC::Status>()),
      delayed_setup_subproc_(nullptr),
      subproc_(nullptr),
      subproc_weight_(SubProcessReq::LIGHT_WEIGHT),
      subproc_exit_status_(0),
      want_fallback_(false),
      should_fallback_(false),
      verify_output_(false),
      fail_fallback_(false),
      local_run_(false),
      local_killed_(false),
      depscache_used_(false),
      gomacc_revision_mismatched_(false),
      replied_(false),
      input_file_callback_(nullptr),
      num_input_file_task_(0),
      input_file_success_(false),
      output_file_callback_(nullptr),
      num_output_file_task_(0),
      output_file_success_(false),
      local_output_file_callback_(nullptr),
      num_local_output_file_task_(0),
      refcnt_(0) {
  thread_id_ = GetCurrentThreadId();
  absl::call_once(init_once_, InitializeStaticOnce);
  Ref();
  std::ostringstream ss;
  ss << "Task:" << id_;
  trace_id_ = ss.str();

  stats_->set_start_time(absl::ToTimeT(absl::Now()));
  stats_->set_compiler_proxy_user_agent(kUserAgentString);
}

void CompileTask::Ref() {
  AUTOLOCK(lock, &refcnt_mu_);
  refcnt_++;
}

void CompileTask::Deref() {
  int refcnt;
  {
    AUTOLOCK(lock, &refcnt_mu_);
    refcnt_--;
    refcnt = refcnt_;
  }
  if (refcnt == 0)
    delete this;
}

void CompileTask::Init(CompileService::RpcController* rpc,
                       const ExecReq& req,
                       ExecResp* resp,
                       OneshotClosure* done) {
  VLOG(1) << trace_id_ << " init";
  CHECK_EQ(INIT, state_);
  CHECK(service_ != nullptr);
  CHECK_EQ(caller_thread_id_, service_->wm()->GetCurrentThreadId());
  rpc_ = rpc;
  rpc_resp_ = resp;
  done_ = done;
  *req_ = req;
#ifdef _WIN32
  pathext_ = GetEnvFromEnvIter(req.env().begin(), req.env().end(), "PATHEXT",
                               true);
#endif
}

void CompileTask::Start() {
  VLOG(1) << trace_id_ << " start";
  CHECK_EQ(INIT, state_);
  const absl::Duration pending_time = handler_timer_.GetDuration();
  stats_->set_pending_time(DurationToIntMs(pending_time));
  stats_->pending_time = pending_time;

  // We switched to new thread.
  DCHECK(!BelongsToCurrentThread());
  thread_id_ = GetCurrentThreadId();

  input_file_stat_cache_ = absl::make_unique<FileStatCache>();
  output_file_stat_cache_ = absl::make_unique<FileStatCache>();

  rpc_->NotifyWhenClosed(NewCallback(this, &CompileTask::GomaccClosed));

  int api_version = req_->requester_info().api_version();
  if (api_version != RequesterInfo::CURRENT_VERSION) {
    LOG(ERROR) << trace_id_ << " unexpected api_version=" << api_version
               << " want=" << RequesterInfo::CURRENT_VERSION;
  }
#if defined(ENABLE_REVISION_CHECK)
  if (req_->requester_info().has_goma_revision() &&
      req_->requester_info().goma_revision() != kBuiltRevisionString) {
    LOG(WARNING) << trace_id_ << " goma revision mismatch:"
                 << " gomacc=" << req_->requester_info().goma_revision()
                 << " compiler_proxy=" << kBuiltRevisionString;
    gomacc_revision_mismatched_ = true;
  }
#endif
  CopyEnvFromRequest();
  InitCompilerFlags();
  if (flags_.get() == nullptr) {
    LOG(ERROR) << trace_id_ << " Start error: CompilerFlags is nullptr";
    AddErrorToResponse(TO_USER, "Unsupported command", true);
    ProcessFinished("Unsupported command");
    return;
  }
  if (!IsLocalCompilerPathValid(trace_id_, *req_, flags_->compiler_name())) {
    LOG(ERROR) << trace_id_ << " Start error: invalid local compiler."
               << " path=" << req_->command_spec().local_compiler_path();
    AddErrorToResponse(TO_USER, "Invalid command", true);
    ProcessFinished("Invalid command");
    return;
  }
  if (!flags_->is_successful()) {
    LOG(WARNING) << trace_id_ << " Start error:" << flags_->fail_message();
    // It should fallback.
  } else if (precompiling_) {
    LOG(INFO) << trace_id_ << " Start precompile "
              << (flags_->input_filenames().empty() ? "(no input)" :
                  flags_->input_filenames()[0])
              << " gomacc_pid=" << gomacc_pid_;
    if (!flags_->input_filenames().empty() && !flags_->output_files().empty()) {
      DCHECK_EQ(1U, flags_->input_filenames().size()) << trace_id_;
      const string& input_filename =
          file::JoinPathRespectAbsolute(flags_->cwd(),
                                        flags_->input_filenames()[0]);
      string output_filename;
      for (const auto& output_file : flags_->output_files()) {
        if (absl::EndsWith(output_file, ".gch")) {
          int output_filelen = output_file.size();
          // Full path and strip ".gch".
          output_filename =
              file::JoinPathRespectAbsolute(
                  flags_->cwd(),
                  output_file.substr(0, output_filelen - 4));
          break;
        }
      }
      // Copy the header file iff precompiling header to *.gch.
      if (!output_filename.empty()) {
        LOG(INFO) << trace_id_ << " copy " << input_filename
                  << " " << output_filename;
        if (input_filename != output_filename) {
          if (file::Copy(input_filename, output_filename, file::Overwrite())
                  .ok()) {
            VLOG(1) << trace_id_ << " copy ok";
            resp_->mutable_result()->set_exit_status(0);
          } else {
            AddErrorToResponse(TO_USER,
                               "Failed to copy " + input_filename + " to " +
                               output_filename, true);
          }
        }
      } else {
        AddErrorToResponse(TO_LOG, "Precompile to no *.gch output", false);
      }
    }
  } else if (linking_) {
    // build_dir will be used to infer the build directory
    // in `goma_ctl.py report`. See b/25487955.
    LOG(INFO) << trace_id_ << " Start linking "
              << (flags_->output_files().empty() ? "(no output)" :
                  flags_->output_files()[0])
              << " gomacc_pid=" << gomacc_pid_
              << " build_dir=" << flags_->cwd();
  } else {
    // build_dir will be used to infer the build directory
    // in `goma_ctl.py report`. See b/25487955.
    LOG(INFO) << trace_id_ << " Start "
              << (flags_->input_filenames().empty() ? "(no input)" :
                  flags_->input_filenames()[0])
              << " gomacc_pid=" << gomacc_pid_
              << " build_dir=" << flags_->cwd();
  }
  if (!FindLocalCompilerPath()) {
    // Unable to fallback.
    LOG(ERROR) << trace_id_ << " Failed to find local compiler path:"
               << req_->DebugString()
               << " env:" << requester_env_.DebugString();
    AddErrorToResponse(TO_USER, "Failed to find local compiler path", true);
    ProcessFinished("fail to find local compiler");
    return;
  }
  VLOG(1) << "local_compiler:" << req_->command_spec().local_compiler_path();
  local_compiler_path_ = req_->command_spec().local_compiler_path();

  verify_output_ = ShouldVerifyOutput();
  should_fallback_ = ShouldFallback();
  subproc_weight_ = GetTaskWeight();
  int ramp_up = service_->http_client()->ramp_up();

  if (verify_output_) {
    VLOG(1) << trace_id_ << " verify_output";
    SetupSubProcess();
    RunSubProcess("verify output");
    service_->RecordForcedFallbackInSetup(CompileService::kRequestedByUser);
    // we run both local and goma backend.
    return;
  }
  if (should_fallback_) {
    VLOG(1) << trace_id_ << " should fallback";
    SetupSubProcess();
    RunSubProcess("should fallback");
    // we don't call goma rpc.
    return;
  }
  if ((rand() % 100) >= ramp_up) {
    LOG(WARNING) << trace_id_ << " http disabled "
                 << " ramp_up=" << ramp_up;
    should_fallback_ = true;
    service_->RecordForcedFallbackInSetup(CompileService::kHTTPDisabled);
    SetupSubProcess();
    RunSubProcess("http disabled");
    // we don't call goma rpc.
    return;
  }
  if (precompiling_ && service_->enable_gch_hack()) {
    VLOG(1) << trace_id_ << " gch hack";
    SetupSubProcess();
    RunSubProcess("gch hack");
    // we run both local and goma backend in parallel.
  } else if (!requester_env_.fallback()) {
    stats_->set_local_run_reason("should not run under GOMA_FALLBACK=false");
    LOG(INFO) << trace_id_ << " GOMA_FALLBACK=false";
  } else if (subproc_weight_ == SubProcessReq::HEAVY_WEIGHT) {
    stats_->set_local_run_reason("should not start running heavy subproc.");
  } else if (requester_env_.use_local()) {
    int num_pending_subprocs = SubProcessTask::NumPending();
    bool is_failed_input = false;
    if (service_->local_run_for_failed_input()) {
      is_failed_input = service_->ContainFailedInput(flags_->input_filenames());
    }
    const absl::Duration subproc_delay =
        service_->GetEstimatedSubprocessDelayTime();
    if (num_pending_subprocs == 0) {
      stats_->set_local_run_reason("local idle");
      SetupSubProcess();
    } else if (is_failed_input) {
      stats_->set_local_run_reason("previous failed");
      SetupSubProcess();
      // TODO: RunSubProcess to run it soon?
    } else if (subproc_delay <= absl::ZeroDuration()) {
      stats_->set_local_run_reason("slow goma");
      SetupSubProcess();
    } else if (!service_->http_client()->IsHealthyRecently()) {
      stats_->set_local_run_reason("goma unhealthy");
      SetupSubProcess();
    } else {
      stats_->set_local_run_reason("should not run while delaying subproc");
      stats_->set_local_delay_time(absl::ToInt64Milliseconds(subproc_delay));
      stats_->local_delay_time = subproc_delay;
      VLOG(1) << trace_id_ << " subproc_delay=" << subproc_delay;
      DCHECK(delayed_setup_subproc_ == nullptr) << trace_id_ << " subproc";
      delayed_setup_subproc_ =
          service_->wm()->RunDelayedClosureInThread(
              FROM_HERE,
              thread_id_,
              subproc_delay,
              NewCallback(
                  this,
                  &CompileTask::SetupSubProcess));
    }
  } else {
    stats_->set_local_run_reason("should not run under GOMA_USE_LOCAL=false");
    LOG(INFO) << trace_id_ << " GOMA_USE_LOCAL=false";
  }
  if (subproc_ != nullptr && ShouldStopGoma()) {
    state_ = LOCAL_RUN;
    stats_->set_local_run_reason("slow goma, local run started in INIT");
    return;
  }
  ProcessSetup();
}

CompileTask::~CompileTask() {
  CHECK_EQ(0, refcnt_);
  CHECK(output_file_.empty());
}

bool CompileTask::BelongsToCurrentThread() const {
  return THREAD_ID_IS_SELF(thread_id_);
}

bool CompileTask::IsGomaccRunning() {
  if (gomacc_pid_ == SubProcessState::kInvalidPid)
    return false;
#ifndef _WIN32
  int ret = kill(gomacc_pid_, 0);
  if (ret != 0) {
    if (errno == ESRCH) {
      gomacc_pid_ = SubProcessState::kInvalidPid;
    } else {
      PLOG(ERROR) << trace_id_ << " kill 0 failed with unexpected errno."
                  << " gomacc_pid=" << gomacc_pid_;
    }
  }
#else
  SimpleTimer timer;
  bool running = false;
  {
    ScopedFd proc(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                              gomacc_pid_));
    running = proc.valid();
  }
  const absl::Duration duration = timer.GetDuration();
  LOG_IF(WARNING, duration > absl::Milliseconds(100))
        << trace_id_ << " SLOW IsGomaccRunning in " << duration;
  if (!running) {
    gomacc_pid_ = SubProcessState::kInvalidPid;
  }
#endif
  return gomacc_pid_ != SubProcessState::kInvalidPid;
}

void CompileTask::GomaccClosed() {
  LOG(INFO) << trace_id_ << " gomacc closed "
            << "at state=" << StateName(state_)
            << " subproc pid="
            << (subproc_ != nullptr ? subproc_->started().pid() : 0);
  canceled_ = true;
  gomacc_pid_ = SubProcessState::kInvalidPid;
  // Kill subprocess either it is running, or pending.
  if (subproc_ != nullptr) {
    KillSubProcess();
  }
}

bool CompileTask::IsSubprocRunning() const {
  return subproc_ != nullptr &&
      subproc_->started().pid() != SubProcessState::kInvalidPid;
}

void CompileTask::ProcessSetup() {
  VLOG(1) << trace_id_ << " setup";
  CHECK(BelongsToCurrentThread());
  CHECK_EQ(INIT, state_);
  CHECK(!abort_);
  CHECK(!should_fallback_);
  state_ = SETUP;
  if (ShouldStopGoma()) {
    state_ = LOCAL_RUN;
    stats_->set_local_run_reason("slow goma, local run started in SETUP");
    return;
  }
  FillCompilerInfo();
}

void CompileTask::TryProcessFileRequest() {
  file_request_timer_.Start();
  if (linking_) {
    AUTOLOCK(lock, &global_mu_);
    DCHECK(link_file_req_tasks_ != nullptr);
    link_file_req_tasks_->push_back(this);
    if (link_file_req_tasks_->front() != this) {
      VLOG(1) << trace_id_ << " pending file req "
              << link_file_req_tasks_->size();
      return;
    }
  }
  ProcessFileRequest();
}

void CompileTask::ProcessFileRequest() {
  VLOG(1) << trace_id_ << " file req";
  CHECK(BelongsToCurrentThread());
  // SETUP: first pass
  // FILE_REQ: failed in input file task, and retry
  // FILE_RESP: failed with missing inputs, and retry
  CHECK(state_ == SETUP || state_ == FILE_REQ || state_ == FILE_RESP)
      << trace_id_ << " " << StateName(state_);
  const absl::Duration fileload_pending_time =
      file_request_timer_.GetDuration();
  stats_->include_fileload_pending_time += fileload_pending_time;
  stats_->add_include_fileload_pending_time(
      DurationToIntMs(fileload_pending_time));
  file_request_timer_.Start();
  if (abort_) {
    ProcessPendingFileRequest();
    ProcessFinished("aborted before file req");
    return;
  }
  if (canceled_) {
    ProcessPendingFileRequest();
    ProcessFinished("canceled before file req");
    return;
  }
  state_ = FILE_REQ;
  if (ShouldStopGoma()) {
    ProcessPendingFileRequest();
    state_ = LOCAL_RUN;
    stats_->set_local_run_reason("slow goma, local run started in FILE_REQ");
    return;
  }
  VLOG(1) << trace_id_
          << " start processing of input files "
          << required_files_.size();

  std::set<string> missed_content_files;
  for (const auto& filename : resp_->missing_input()) {
    missed_content_files.insert(filename);
    VLOG(2) << trace_id_ << " missed content: " << filename;
    if (interleave_uploaded_files_.find(filename) !=
        interleave_uploaded_files_.end()) {
      LOG(WARNING) << trace_id_ << " interleave-uploaded file missing:"
                   << filename;
    }
  }

  // InputFileTask assumes that filename is unique in single compile task.
  RemoveDuplicateFiles(flags_->cwd(), &required_files_);

  // TODO: We don't need to clear the input when we are retrying.
  req_->clear_input();
  interleave_uploaded_files_.clear();
  SetInputFileCallback();
  std::vector<OneshotClosure*> closures;
  const absl::Time now = absl::Now();
  stats_->set_num_total_input_file(required_files_.size());

  for (const string& filename : required_files_) {
    ExecReq_Input* input = req_->add_input();
    input->set_filename(filename);
    const std::string abs_filename =
        file::JoinPathRespectAbsolute(flags_->cwd(), filename);
    bool missed_content =
        missed_content_files.find(filename) != missed_content_files.end();
    absl::optional<absl::Time> mtime;
    string hash_key;
    bool hash_key_is_ok = false;
    absl::optional<absl::Time> missed_timestamp;
    if (missed_content) {
      missed_timestamp = last_req_timestamp_;
    }

    // If the file was reported as missing, we need to send the file content.
    //
    // Otherwise,
    //  if hash_key_is_ok is true, we can believe hash_key is valid,
    //  so uses hash_key only (no content uploading here)
    //
    //  if hash_key_is_ok is false, we're not sure hash_key is valid or not,
    //  so try reading the content.  InputFileTaskFinished determines whether
    //  we should upload the content or not, based on mtime and hash_key.
    //  if the content's hash matches with this hash_key, we can believe
    //  hash_key is valid, so don't upload content in this session.
    //
    // If we believed hash_key is valid, but goma servers couldn't find the
    // content, then it would be reported as missing_inputs_ and we'll set
    // missed_content to true in the retry session.
    // Even in this case, we need to consider the race condition of upload and
    // execution. If the file is uploaded by the other task during the task is
    // getting missing_inputs_, we do not have to upload the file again. We use
    // the timestamp of file upload and execution to identify this condition.
    // If upload time is later than execution time (last_req_timestamp_),
    // we can assume the file is uploaded by others.
    const FileStat input_file_stat = input_file_stat_cache_->Get(abs_filename);
    if (input_file_stat.IsValid()) {
      mtime = *input_file_stat.mtime;
    }
    hash_key_is_ok = service_->file_hash_cache()->GetFileCacheKey(
        abs_filename, missed_timestamp, input_file_stat, &hash_key);
    if (missed_content) {
      if (hash_key_is_ok) {
        VLOG(2) << trace_id_ << " interleave uploaded: "
                << " filename=" << abs_filename;
        // TODO: warn if interleave uploaded file is missing.
        interleave_uploaded_files_.insert(filename);
      } else {
        LOG(INFO) << trace_id_ << " missed content:" << abs_filename;
      }
    }
    if (mtime.has_value() &&
        *mtime > absl::FromTimeT(stats_->latest_input_mtime())) {
      stats_->set_latest_input_filename(abs_filename);
      stats_->set_latest_input_mtime(absl::ToTimeT(*mtime));
    }
    if (hash_key_is_ok) {
      input->set_hash_key(hash_key);
      continue;
    }
    // In linking, we'll use hash_key instead of content in ExecReq to prevent
    // from bloating ExecReq.
    VLOG(1) << trace_id_ << " input file:" << abs_filename
            << (linking_ ? " [linking]" : "");
    bool is_new_file = false;
    if (mtime.has_value()) {
      if (linking_) {
        // For linking, we assume input files is old if it is older than
        // compiler_proxy start time. (i.e. it would be built in previous
        // build session, so that the files were generated by goma backends
        // or uploaded by previous compiler_proxy.
        is_new_file = *mtime > service_->start_time();
      } else {
        is_new_file = (now - *mtime < service_->new_file_threshold_duration());
      }
    }
    // If need_to_send_content is set to true, we consider all file is new file.
    if (service_->need_to_send_content())
      is_new_file = true;

    InputFileTask* input_file_task = InputFileTask::NewInputFileTask(
        service_->wm(),
        service_->blob_client()->NewUploader(
            abs_filename, requester_info_, trace_id_),
        service_->file_hash_cache(), input_file_stat_cache_->Get(abs_filename),
        abs_filename, missed_content, linking_, is_new_file, hash_key, this,
        input);
    closures.push_back(
        NewCallback(
            input_file_task,
            &InputFileTask::Run,
            this,
            NewCallback(
                this,
                &CompileTask::InputFileTaskFinished,
                input_file_task)));
    DCHECK_EQ(closures.size(), static_cast<size_t>(num_input_file_task_));
  }
  DCHECK_EQ(closures.size(), static_cast<size_t>(num_input_file_task_));
  stats_->add_num_uploading_input_file(closures.size());
  stats_->add_num_file_uploaded_during_exec_failure(
      interleave_uploaded_files_.size());
  if (closures.empty()) {
    MaybeRunInputFileCallback(false);
    return;
  }
  for (auto* closure : closures)
    service_->wm()->RunClosure(
        FROM_HERE, closure, WorkerThread::PRIORITY_LOW);
}

void CompileTask::ProcessFileRequestDone() {
  VLOG(1) << trace_id_ << " file req done";
  CHECK(BelongsToCurrentThread());
  CHECK_EQ(FILE_REQ, state_);
  const absl::Duration fileload_run_time = file_request_timer_.GetDuration();
  stats_->add_include_fileload_run_time(DurationToIntMs(fileload_run_time));
  stats_->include_fileload_run_time += fileload_run_time;

  const absl::Duration include_fileload_time =
      include_timer_.GetDuration() - stats_->include_preprocess_time;
  stats_->include_fileload_time = include_fileload_time;

  VLOG(1) << trace_id_
          << " input files processing preprocess "
          << stats_->include_preprocess_time
          << ", loading " << stats_->include_fileload_time;

  ProcessPendingFileRequest();

  if (abort_) {
    ProcessFinished("aborted in file req");
    return;
  }
  if (canceled_) {
    ProcessFinished("canceled in file req");
    return;
  }
  if (!input_file_success_) {
    if (IsSubprocRunning()) {
      VLOG(1) << trace_id_ << " file request failed,"
              << " but subprocess running";
      state_ = LOCAL_RUN;
      stats_->set_local_run_reason("fail goma, local run started in FILE_REQ");
      return;
    }
    AddErrorToResponse(TO_LOG, "Failed to process file request", true);
    if (service_->http_client()->IsHealthyRecently() &&
        stats_->num_uploading_input_file_size() > 0 &&
        stats_->num_uploading_input_file(
            stats_->num_uploading_input_file_size() - 1) > 0) {
      // TODO: don't retry for permanent error (no such file, etc).
      stats_->set_exec_request_retry(stats_->exec_request_retry() + 1);
      if (stats_->exec_request_retry() <= kMaxExecRetry) {
        std::ostringstream ss;
        ss << "Failed to upload "
           << stats_->num_uploading_input_file(
               stats_->num_uploading_input_file_size() - 1)
           << " files";
        stats_->add_exec_request_retry_reason(ss.str());
        LOG(INFO) << trace_id_ << " retry in FILE_REQ";
        resp_->clear_error_message();

        service_->wm()->RunClosureInThread(
            FROM_HERE,
            thread_id_,
            NewCallback(this, &CompileTask::TryProcessFileRequest),
            WorkerThread::PRIORITY_LOW);
        return;
      }
    }
    ProcessFinished("fail in file request");
    return;
  }

  // Fix for GOMA_GCH.
  // We're sending *.gch.goma on local disk, but it must appear as *.gch
  // on backend.
  if (service_->enable_gch_hack()) {
    for (auto& input : *req_->mutable_input()) {
      if (absl::EndsWith(input.filename(), GOMA_GCH_SUFFIX)) {
        input.mutable_filename()->resize(
            input.filename().size() - strlen(".goma"));
      }
    }
  }

  // Here, |req_| is all prepared.
  // TODO: Instead of here, maybe we need to call this
  // in end of ProcessFileRequest?
  if (LocalOutputCache::IsEnabled()) {
    local_output_cache_key_ = LocalOutputCache::MakeCacheKey(*req_);
    if (LocalOutputCache::instance()->Lookup(local_output_cache_key_,
                                             resp_.get(),
                                             trace_id_)) {
      LOG(INFO) << trace_id_ << " lookup succeeded";
      stats_->set_cache_hit(true);
      stats_->set_cache_source(ExecLog::LOCAL_OUTPUT_CACHE);
      ReleaseMemoryForExecReqInput(req_.get());
      state_ = LOCAL_OUTPUT;
      ProcessFileResponse();
      return;
    }
  }

  ProcessCallExec();
}

void CompileTask::ProcessPendingFileRequest() {
  if (!linking_)
    return;

  CompileTask* pending_task = nullptr;
  {
    AUTOLOCK(lock, &global_mu_);
    DCHECK_EQ(this, link_file_req_tasks_->front());
    link_file_req_tasks_->pop_front();
    if (!link_file_req_tasks_->empty()) {
      pending_task = link_file_req_tasks_->front();
    }
  }
  if (pending_task != nullptr) {
    VLOG(1) << pending_task->trace_id_ << " start file req";
    service_->wm()->RunClosureInThread(
        FROM_HERE,
        pending_task->thread_id_,
        NewCallback(pending_task, &CompileTask::ProcessFileRequest),
        WorkerThread::PRIORITY_LOW);
  }
}

void CompileTask::ProcessCallExec() {
  VLOG(1) << trace_id_ << " call exec";
  CHECK(BelongsToCurrentThread());
  CHECK_EQ(FILE_REQ, state_);
  if (abort_) {
    ProcessFinished("aborted before call exec");
    return;
  }
  if (canceled_) {
    ProcessFinished("canceled before call exec");
    return;
  }
  CHECK(!requester_env_.verify_command().empty() ||
        req_->input_size() > 0) << trace_id_ << " call exec";
  state_ = CALL_EXEC;
  if (ShouldStopGoma()) {
    state_ = LOCAL_RUN;
    stats_->set_local_run_reason("slow goma, local run started in CALL_EXEC");
    return;
  }

  if (req_->trace()) LOG(INFO) << trace_id_ << " requesting remote trace";
  rpc_call_timer_.Start();
  req_->mutable_requester_info()->set_retry(stats_->exec_request_retry());
  VLOG(2) << trace_id_
          << " request string to send:" << req_->DebugString();
  {
    AUTOLOCK(lock, &mu_);
    http_rpc_status_ = absl::make_unique<HttpRPC::Status>();
    http_rpc_status_->trace_id = trace_id_;
    const auto& timeouts = service_->timeouts();
    for (const auto& timeout : timeouts) {
      http_rpc_status_->timeouts.push_back(timeout);
    }
  }

  exec_resp_ = absl::make_unique<ExecResp>();
  service_->exec_service_client()->ExecAsync(
      req_.get(), exec_resp_.get(), http_rpc_status_.get(),
      NewCallback(this, &CompileTask::ProcessCallExecDone));

  last_req_timestamp_ = absl::Now();
  if (requester_env_.use_local() &&
      (subproc_weight_ == SubProcessReq::HEAVY_WEIGHT) &&
      subproc_ == nullptr) {
    // now, it's ok to run subprocess.
    stats_->set_local_run_reason("slow goma linking");
    SetupSubProcess();
  }
}

void CompileTask::ProcessCallExecDone() {
  VLOG(1) << trace_id_ << " call exec done";
  CHECK(BelongsToCurrentThread());
  CHECK_EQ(CALL_EXEC, state_);
  exit_status_ = exec_resp_->result().exit_status();
  resp_->Swap(exec_resp_.get());
  exec_resp_.reset();
  string retry_reason;
  for (const auto& msg : resp_->error_message()) {
    exec_error_message_.push_back(msg);
    if (!retry_reason.empty()) {
      retry_reason += "\n";
    }
    retry_reason += msg;
  }
  // clear error_message from server.
  // server error message logged, but not send back to user.
  resp_->clear_error_message();

  const absl::Duration rpc_call_timer_duration = rpc_call_timer_.GetDuration();
  stats_->add_rpc_call_time(DurationToIntMs(rpc_call_timer_duration));
  stats_->total_rpc_call_time += rpc_call_timer_duration;

  stats_->AddStatsFromHttpStatus(*http_rpc_status_);
  stats_->AddStatsFromExecResp(*resp_);

  stats_->set_cache_hit(resp_->cache_hit() == ExecResp::LOCAL_OUTPUT_CACHE ||
                        (http_rpc_status_->finished && resp_->has_cache_hit() &&
                         resp_->cache_hit() != ExecResp::NO_CACHE));

  if (stats_->cache_hit()) {
    if (!resp_->has_cache_hit()) {
      // for old backends.
      stats_->set_cache_source(ExecLog::UNKNOWN_CACHE);
    } else {
      switch (resp_->cache_hit()) {
        case ExecResp::NO_CACHE:
          LOG(ERROR) << trace_id_ << " cache_hit, but NO_CACHE";
          break;
        case ExecResp::MEM_CACHE:
          stats_->set_cache_source(ExecLog::MEM_CACHE);
          break;
        case ExecResp::STORAGE_CACHE:
          stats_->set_cache_source(ExecLog::STORAGE_CACHE);
          break;
        case ExecResp::LOCAL_OUTPUT_CACHE:
          stats_->set_cache_source(ExecLog::LOCAL_OUTPUT_CACHE);
          break;
        default:
          LOG(ERROR) << trace_id_ << " unknown cache_source="
                     << resp_->cache_hit();
          stats_->set_cache_source(ExecLog::UNKNOWN_CACHE);
      }
    }
  }


  if (resp_->has_cache_key())
    resp_cache_key_ = resp_->cache_key();

  if (abort_) {
    ProcessFinished("aborted in call exec");
    return;
  }
  if (canceled_) {
    ProcessFinished("canceled in call exec");
    return;
  }

  if (!http_rpc_status_->enabled) {
    stats_->set_network_failure_type(ExecLog::DISABLED);
  } else if (http_rpc_status_->err == 0) {
    stats_->set_network_failure_type(ExecLog::NO_NETWORK_ERROR);
  } else {   // i.e. http_rpc_status_->err != 0.
    stats_->set_network_failure_type(ExecLog::UNKNOWN_NETWORK_ERROR);
    switch (http_rpc_status_->state) {
      case HttpClient::Status::INIT:  FALLTHROUGH_INTENDED;
      case HttpClient::Status::PENDING:
        stats_->set_network_failure_type(ExecLog::CONNECT_FAILED);
        break;
      case HttpClient::Status::SENDING_REQUEST:
        stats_->set_network_failure_type(ExecLog::SEND_FAILED);
        break;
      case HttpClient::Status::REQUEST_SENT:
        stats_->set_network_failure_type(ExecLog::TIMEDOUT_AFTER_SEND);
        break;
      case HttpClient::Status::RECEIVING_RESPONSE:
        stats_->set_network_failure_type(ExecLog::RECEIVE_FAILED);
        break;
      case HttpClient::Status::RESPONSE_RECEIVED:
        if (http_rpc_status_->http_return_code != 200) {
          stats_->set_network_failure_type(ExecLog::BAD_HTTP_STATUS_CODE);
        }
        break;
    }
  }

  const int err = http_rpc_status_->err;
  if (err < 0) {
    LOG(WARNING) << trace_id_ << " rpc err=" << err << " "
                 << (err == ERR_TIMEOUT ? " timed out" : " failed")
                 << " " << http_rpc_status_->err_message;
    if (IsSubprocRunning() &&
        http_rpc_status_->state != HttpClient::Status::RECEIVING_RESPONSE) {
      // If rpc was failed while receiving response, goma should retry Exec call
      // because the reponse will be replied from cache with high probability.
      LOG(WARNING) << trace_id_ << " goma failed, but subprocess running.";
      state_ = LOCAL_RUN;
      stats_->set_local_run_reason("fail goma, local run started in CALL_EXEC");
      return;
    }
    AddErrorToResponse(TO_LOG, "", true);
    // Don't Retry if it is client error: 3xx or 4xx.
    // Retry if it is server error: 5xx (e.g. 502 error from GFE)
    //
    // Also, OK to retry on socket timeout occurred during reciving response.
    if (((http_rpc_status_->http_return_code / 100) == 5) ||
        (http_rpc_status_->state == HttpClient::Status::RECEIVING_RESPONSE)) {
      std::ostringstream ss;
      ss << "RPC failed http=" << http_rpc_status_->http_return_code
         << ": " << http_rpc_status_->err_message;
      if (!retry_reason.empty()) {
        retry_reason += "\n";
      }
      retry_reason += ss.str();
    } else {
      // No retry for client error: 3xx, 4xx (302, 403 for dos block,
      // 401 for auth error, etc).
      LOG(WARNING) << trace_id_ << " RPC failed http="
                   << http_rpc_status_->http_return_code
                   << " state="
                   << HttpClient::Status::StateName(http_rpc_status_->state)
                   << ": " << http_rpc_status_->err_message
                   << ": no retry";
    }
  }
  if (err == OK && resp_->missing_input_size() > 0) {
    // missing input will be handled in ProcessFileResponse and
    // ProcessFileRequest will retry the request with uploading
    // contents of missing inputs.
    // Just retrying the request here would not upload contents
    // so probably fails with missing input again, so don't retry here.
    LOG_IF(WARNING, !retry_reason.empty())
        << trace_id_ << " missing inputs:" << resp_->missing_input_size()
        << " but retry_reason set:" << retry_reason;
  } else if (!retry_reason.empty()) {
    if (service_->http_client()->IsHealthyRecently()) {
      LOG(INFO) << trace_id_ << " exec retry:"
                << stats_->exec_request_retry()
                << " error=" << resp_->error()
                << " " << retry_reason;
      stats_->set_exec_request_retry(stats_->exec_request_retry() + 1);
      if (stats_->exec_request_retry() <= kMaxExecRetry &&
          !(resp_->has_error() && IsFatalError(resp_->error()))) {
        stats_->add_exec_request_retry_reason(retry_reason);
        LOG(INFO) << trace_id_ << " retry in CALL_EXEC";
        resp_->clear_error_message();
        resp_->clear_error();
        state_ = FILE_REQ;
        service_->wm()->RunClosureInThread(
            FROM_HERE,
            thread_id_,
            NewCallback(this, &CompileTask::ProcessCallExec),
            WorkerThread::PRIORITY_LOW);
        return;
      }
      if (service_->should_fail_for_unsupported_compiler_flag() &&
          resp_->bad_request_reason_code() ==
              ExecResp::UNSUPPORTED_COMPILER_FLAGS) {
        // TODO: Make a simple test for this after goma server has
        // started returning bad request reason code.
        string msg = "compile request was rejected by goma server. "
            "The request might contain unsupported compiler flag.\n"
            "If you want to continue compile with local fallback, set "
            "environment variable "
            "GOMA_FAIL_FOR_UNSUPPORTED_COMPILER_FLAGS=false and "
            "restart the compiler_proxy.\n";
        AddErrorToResponse(TO_USER, msg, true);
        want_fallback_ = false;
      } else {
        LOG(WARNING) << trace_id_ << " exec error:"
                     << resp_->error()
                     << " " << retry_reason
                     << " but http is healthy";
      }
    } else {
      LOG(WARNING) << trace_id_
                   << " won't retry because http client is not healthy.";
    }
    CheckNoMatchingCommandSpec(retry_reason);
    ProcessFinished("fail in call exec");
    return;
  }

  if (err < 0) {
    ProcessFinished("fail in call exec");
    return;
  }

  // Saves embedded upload information. We have to call this before
  // clearing inputs.
  StoreEmbeddedUploadInformationIfNeeded();

  ReleaseMemoryForExecReqInput(req_.get());

  if (resp_->missing_input_size() == 0) {
    // Check command spec when not missing input response.
    CheckCommandSpec();
  }
  ProcessFileResponse();
}

void CompileTask::ProcessFileResponse() {
  VLOG(1) << trace_id_ << " file resp";
  CHECK(BelongsToCurrentThread());
  CHECK(state_ == CALL_EXEC || state_ == LOCAL_OUTPUT) << state_;
  if (abort_) {
    ProcessFinished("aborted before file resp");
    return;
  }
  if (canceled_) {
    ProcessFinished("canceled before file resp");
    return;
  }
  state_ = FILE_RESP;
  if (ShouldStopGoma()) {
    state_ = LOCAL_RUN;
    stats_->set_local_run_reason("slow goma, local run started in FILE_RESP");
    return;
  }
  file_response_timer_.Start();
  if (resp_->missing_input_size() > 0) {
    stats_->add_num_missing_input_file(resp_->missing_input_size());
    LOG(WARNING) << trace_id_
                 << " request didn't have full content:"
                 << resp_->missing_input_size()
                 << " in "
                 << required_files_.size()
                 << " : retry=" << stats_->exec_request_retry();
    for (const auto& filename : resp_->missing_input()) {
      std::ostringstream ss;
      ss << "Required file not on goma cache:" << filename;
      if (interleave_uploaded_files_.find(filename)
          != interleave_uploaded_files_.end()) {
        ss << " (interleave uploaded)";
      }
      AddErrorToResponse(TO_LOG, ss.str(), true);
    }
    for (const auto& reason : resp_->missing_reason()) {
      AddErrorToResponse(TO_LOG, reason, true);
    }
    int need_to_send_content_threshold = required_files_.size() / 2;
    if (!service_->need_to_send_content()
        && (resp_->missing_input_size() > need_to_send_content_threshold)) {
      LOG(WARNING) << trace_id_
                   << " Lots of missing files. Will send file contents"
                   << " even if it's old enough.";
      service_->SetNeedToSendContent(true);
    }
    output_file_success_ = false;
    ProcessFileResponseDone();
    return;
  }
  if (stats_->exec_request_retry() == 0 && service_->need_to_send_content()) {
    LOG(INFO) << trace_id_ << " no missing files."
              << " Turn off to force sending old file contents";
    service_->SetNeedToSendContent(false);
  }

  // No missing input files.
  if (!IsGomaccRunning()) {
    PLOG(WARNING) << trace_id_
                  << " pid:" << gomacc_pid_ << " does not receive signal 0 "
                  << " abort=" << abort_;
    // user may not receive the error message, because gomacc already killed.
    AddErrorToResponse(TO_LOG, "gomacc killed?", true);
    // If the requesting process was already dead, we should not write output
    // files.
    ProcessFinished("gomacc killed");
    return;
  }

  // Decide if it could use in-memory output or not and should write output
  // in tmp file or not.
  bool want_in_memory_output = true;
  string need_rename_reason;
  if (verify_output_) {
    VLOG(1) << trace_id_ << " output need_rename for verify_output";
    want_in_memory_output = false;
    need_rename_reason = "verify_output";
  } else if (!success()) {
    VLOG(1) << trace_id_ << " output need_rename for fail exec";
    // TODO: we don't need to write remote output for fail exec?
    want_in_memory_output = false;
    need_rename_reason = "fail exec";
  } else {
    // resp_ contains whole output data, and no need to more http_rpc to
    // fetch output file data, so no need to run local compiler any more.
    if (delayed_setup_subproc_ != nullptr) {
      delayed_setup_subproc_->Cancel();
      delayed_setup_subproc_ = nullptr;
    }
    if (subproc_ != nullptr) {
      // racing between remote and local.
      // even if subproc_->started().pid() == kInvalidPid, subproc might
      // have started (because compile_proxy and subproc is async).
      // The compile task wants in_memory output by default, but when it
      // couldn't use in memory output because of lack of memory, it
      // should write output in tmp file (i.e. need to rename).
      // TODO: cancel subproc if it was not started yet,
      //             or use local subproc if it has already started.
      VLOG(1) << trace_id_ << " output need_rename for local_subproc "
              << subproc_->started().pid();
      std::ostringstream ss;
      ss << "local_subproc pid=" << subproc_->started().pid();
      need_rename_reason = ss.str();
    }
  }

  exec_output_file_.clear();
  ClearOutputFile();
  output_file_.resize(resp_->result().output_size());
  SetOutputFileCallback();
  std::vector<OneshotClosure*> closures;
  for (int i = 0; i < resp_->result().output_size(); ++i) {
    const string& output_filename = resp_->result().output(i).filename();
    CheckOutputFilename(output_filename);

    exec_output_file_.push_back(output_filename);
    string filename = file::JoinPathRespectAbsolute(
        stats_->cwd(), output_filename);
    // TODO: check output paths matches with flag's output filenames?
    if (service_->enable_gch_hack() && absl::EndsWith(filename, ".gch"))
      filename += ".goma";

    OutputFileInfo* output_info = &output_file_[i];
    output_info->filename = filename;
    bool try_acquire_output_buffer = want_in_memory_output;
    if (FileServiceClient::IsValidFileBlob(resp_->result().output(i).blob())) {
      output_info->size = resp_->result().output(i).blob().file_size();
    } else {
      LOG(ERROR) << trace_id_ << " output is invalid:"
                 << filename;
      try_acquire_output_buffer = false;
    }
    if (try_acquire_output_buffer && service_->AcquireOutputBuffer(
            output_info->size, &output_info->content)) {
      output_info->tmp_filename.clear();
      VLOG(1) << trace_id_ << " output in buffer:"
              << filename
              << " size="
              << output_info->size;
    } else {
      if (!need_rename_reason.empty()) {
        std::ostringstream ss;
        ss << filename << ".tmp." << id();
        output_info->tmp_filename = ss.str();
        LOG(INFO) << trace_id_ << " output in tmp file:"
                  << output_info->tmp_filename
                  << " for " << need_rename_reason;
      } else {
        // no need to rename, so write output directly to the output file.
        output_info->tmp_filename = filename;
        LOG(INFO) << trace_id_ << " output in file:" << filename;
      }
    }
    if (resp_->result().output(i).is_executable())
      output_info->mode = 0777;
    if (requester_env_.has_umask()) {
      output_info->mode &= ~requester_env_.umask();
      VLOG(1) << trace_id_ << " output file mode is updated."
              << " filename=" << filename
              << " mode=" << std::oct << output_info->mode;
    }
    std::unique_ptr<OutputFileTask> output_file_task(new OutputFileTask(
        service_->wm(),
        service_->blob_client()->NewDownloader(requester_info_, trace_id_),
        this, i, resp_->result().output(i), output_info));

    OutputFileTask* output_file_task_pointer = output_file_task.get();
    closures.push_back(
        NewCallback(
            output_file_task_pointer,
            &OutputFileTask::Run,
            NewCallback(
                this,
                &CompileTask::OutputFileTaskFinished,
                std::move(output_file_task))));
  }
  stats_->set_num_output_file(closures.size());
  if (closures.empty()) {
    MaybeRunOutputFileCallback(-1, false);
  } else {
    for (auto* closure : closures) {
      service_->wm()->RunClosure(
          FROM_HERE, closure, WorkerThread::PRIORITY_LOW);
    }
  }
}

void CompileTask::ProcessFileResponseDone() {
  VLOG(1) << trace_id_ << " file resp done";
  CHECK(BelongsToCurrentThread());
  CHECK_EQ(FILE_RESP, state_);

  const absl::Duration file_response_time = file_response_timer_.GetDuration();
  stats_->file_response_time += file_response_time;
  stats_->set_file_response_time(DurationToIntMs(file_response_time));

  if (abort_) {
    ProcessFinished("aborted in file resp");
    return;
  }
  if (canceled_) {
    ProcessFinished("canceled in file resp");
    return;
  }
  if (!output_file_success_) {
    // TODO: remove following if (!abort_).
    // I belive it should always be true, or abort_ must be protected by mutex.
    if (!abort_) {
      if (!(precompiling_ && service_->enable_gch_hack()) &&
          IsSubprocRunning()) {
        VLOG(1) << trace_id_ << " failed to process file response,"
                << " but subprocess running";
        state_ = LOCAL_RUN;
        stats_->set_local_run_reason(
            "fail goma, local run started in FILE_RESP");
        return;
      }

      // For missing input error, we don't make it as error but warning
      // when this is the first try and we will retry it later.
      bool should_error = stats_->exec_request_retry() > 0;
      std::ostringstream ss;
      ss << "Try:" << stats_->exec_request_retry() << ": ";
      if (resp_->missing_input_size() > 0) {
        // goma server replied with missing inputs.
        // retry: use the list of missing files in response to fill in
        // needed files
        ss << "Missing " << resp_->missing_input_size() << " input files.";
      } else {
        should_error = true;
        ss << "Failed to download "
           << stats_->num_output_file()
           << " files"
           << " in " << (cache_hit() ? "cached" : "no-cached") << "result";
      }

      bool do_retry = false;
      std::ostringstream no_retry_reason;
      if (compiler_info_state_.disabled()) {
        no_retry_reason << "compiler disabled. no retry."
                        << " disabled_reason="
                        << compiler_info_state_.GetDisabledReason();
      } else if (!service_->http_client()->IsHealthyRecently()) {
        no_retry_reason << "http is unhealthy. no retry."
                        << " health_status="
                        << service_->http_client()->GetHealthStatusMessage();
      } else {
        stats_->set_exec_request_retry(stats_->exec_request_retry() + 1);
        do_retry = stats_->exec_request_retry() <= kMaxExecRetry;
        if (!do_retry) {
          no_retry_reason << "too many retry";
        }
      }

      if (!do_retry)
        should_error = true;
      AddErrorToResponse(TO_LOG, ss.str(), should_error);

      if (do_retry) {
        if (!service_->http_client()->IsHealthy()) {
          LOG(WARNING) << trace_id_ << " http is unhealthy, but retry."
                       << " health_status="
                       << service_->http_client()->GetHealthStatusMessage();
        }
        VLOG(2) << trace_id_
                << " Failed to process file response (we will retry):"
                << resp_->DebugString();
        stats_->add_exec_request_retry_reason(ss.str());
        LOG(INFO) << trace_id_ << " retry in FILE_RESP";
        resp_->clear_error_message();
        TryProcessFileRequest();
        return;
      }
      AddErrorToResponse(TO_LOG, no_retry_reason.str(), true);
    }
    VLOG(2) << trace_id_
            << " Failed to process file response (second time):"
            << resp_->DebugString();
    ProcessFinished("failed in file response");
    return;
  }

  if (verify_output_) {
    CHECK(subproc_ == nullptr);
    CHECK(delayed_setup_subproc_ == nullptr);
    for (const auto& info : output_file_) {
      const string& filename = info.filename;
      const string& tmp_filename = info.tmp_filename;
      if (!VerifyOutput(filename, tmp_filename)) {
        output_file_success_ = false;
      }
    }
    output_file_.clear();
    ProcessFinished("verify done");
    return;
  }
  if (success()) {
    ProcessFinished("");
  } else {
    ClearOutputFile();
    ProcessFinished("fail exec");
  }
}

void CompileTask::ProcessFinished(const string& msg) {
  if (abort_ || canceled_ || !msg.empty()) {
    LOG(INFO) << trace_id_ << " finished " << msg
              << " state=" << StateName(state_)
              << " abort=" << abort_
              << " canceled=" << canceled_;
  } else {
    VLOG(1) << trace_id_ << " finished " << msg
            << " state=" << StateName(state_);
    DCHECK(success()) << trace_id_ << " finished";
    DCHECK_EQ(FILE_RESP, state_) << trace_id_ << " finished";
  }
  CHECK(BelongsToCurrentThread());
  CHECK_LT(state_, FINISHED);
  DCHECK(!finished_);
  finished_ = true;
  if (state_ == INIT) {
    // failed to find local compiler path.
    // it also happens if user uses old gomacc.
    LOG(ERROR) << trace_id_ << " failed in INIT.";
    CHECK(subproc_ == nullptr);
    CHECK(delayed_setup_subproc_ == nullptr);
    CHECK(!abort_);
    state_ = FINISHED;
    ReplyResponse("failed in INIT");
    return;
  }
  if (!abort_)
    state_ = FINISHED;
  if (verify_output_) {
    VLOG(2) << trace_id_ << " verify response:" << resp_->DebugString();
    CHECK(subproc_ == nullptr);
    CHECK(delayed_setup_subproc_ == nullptr);
    ReplyResponse("verify done");
    return;
  }
  if (precompiling_ && service_->enable_gch_hack()) {
    // In gch hack mode, we'll run both local and remote simultaneously.
    if (subproc_ != nullptr) {
      // subprocess still running.
      // we'll reply response when subprocess is finished.
      return;
    }
    // subprocess finished first.
    CHECK(delayed_setup_subproc_ == nullptr);
    VLOG(1) << trace_id_ << " gch hack: local and goma finished.";
    ProcessReply();
    return;
  }

  if (!requester_env_.fallback()) {
    VLOG(1) << trace_id_ << " goma finished and no fallback.";
    CHECK(subproc_ == nullptr);
    CHECK(delayed_setup_subproc_ == nullptr);
    ProcessReply();
    return;
  }
  if (abort_) {
    // local finished first (race or verify output).
    if (local_output_file_callback_ == nullptr)
      Done();
    // If local_output_file_callback_ is not nullptr, uploading local output
    // file is on the fly, so ProcessLocalFileOutputDone() will be called
    // later.
    return;
  }
  CHECK_EQ(FINISHED, state_);
  if (success() || !IsGomaccRunning() || !want_fallback_) {
    if (!success() && !want_fallback_) {
      LOG(INFO) << trace_id_ << " failed and no need to fallback";
    } else {
      VLOG(1) << trace_id_ << " success or gomacc killed.";
    }
    stats_->clear_local_run_reason();
    if (delayed_setup_subproc_ != nullptr) {
      delayed_setup_subproc_->Cancel();
      delayed_setup_subproc_ = nullptr;
    }
    if (subproc_ != nullptr) {
      LOG(INFO) << trace_id_ << " goma finished, killing subproc pid="
                << subproc_->started().pid();
      KillSubProcess();  // FinishSubProcess will be called.
    } else {
      ProcessReply();  // GOMA_FALLBACK=false or GOMA_USE_LOCAL=false
    }
    return;
  }
  LOG(INFO) << trace_id_ << " fail fallback"
            << " exit=" << resp_->result().exit_status()
            << " cache_key=" << resp_->cache_key()
            << " flag=" << flag_dump_;
  DCHECK(requester_env_.fallback());
  DCHECK(!fail_fallback_);
  stdout_ = resp_->result().stdout_buffer();
  stderr_ = resp_->result().stderr_buffer();
  LogCompilerOutput(trace_id_, "stdout", stdout_);
  LogCompilerOutput(trace_id_, "stderr", stderr_);

  fail_fallback_ = true;
  // TODO: active fail fallback only for http error?
  // b/36576025 b/36577821
  if (!service_->IncrementActiveFailFallbackTasks()) {
    AddErrorToResponse(
        TO_USER, "reached max number of active fail fallbacks", true);
    if (delayed_setup_subproc_ != nullptr) {
      delayed_setup_subproc_->Cancel();
      delayed_setup_subproc_ = nullptr;
    }
    if (subproc_ != nullptr) {
      LOG(INFO) << trace_id_ << " killing subproc pid="
                << subproc_->started().pid();
      KillSubProcess();  // FinishSubProcess will be called.
    } else {
      ProcessReply();  // GOMA_FALLBACK=false or GOMA_USE_LOCAL=false
    }
    return;
  }
  if (subproc_ == nullptr) {
    // subproc_ might be nullptr (e.g. GOMA_USE_LOCAL=false).
    SetupSubProcess();
  }
  RunSubProcess(msg);
}

void CompileTask::ProcessReply() {
  VLOG(1) << trace_id_ << " process reply";
  DCHECK(BelongsToCurrentThread());
  CHECK_EQ(FINISHED, state_);
  CHECK(subproc_ == nullptr);
  CHECK(delayed_setup_subproc_ == nullptr);
  CHECK(!abort_);
  string msg;
  if (IsGomaccRunning()) {
    VLOG(2) << trace_id_ << " goma result:" << resp_->DebugString();
    if (local_run_ && service_->dont_kill_subprocess()) {
      // if we ran local process and dont_kill_subprocess is true, we just
      // use local results, so we don't need to rename remote outputs.
      CommitOutput(false);
      msg = "goma success, but local used";
    } else {
      CommitOutput(true);
      if (local_cache_hit()) {
        msg = "goma success (local cache hit)";
      } else if (cache_hit()) {
        msg = "goma success (cache hit)";
      } else {
        msg = "goma success";
      }
    }

    if (LocalOutputCache::IsEnabled()) {
      if (!local_cache_hit() && !local_output_cache_key_.empty() && success()) {
        // Here, local or remote output has been performed,
        // and output cache key exists.
        // Note: we need to save output before ReplyResponse. Otherwise,
        // output file might be removed by ninja.
        if (!LocalOutputCache::instance()->SaveOutput(local_output_cache_key_,
                                                      req_.get(),
                                                      resp_.get(),
                                                      trace_id_)) {
          LOG(ERROR) << trace_id_ << " failed to save localoutputcache";
        }
      }
    }
  } else {
    msg = "goma canceled";
  }

  if (!subproc_stdout_.empty()) remove(subproc_stdout_.c_str());
  if (!subproc_stderr_.empty()) remove(subproc_stderr_.c_str());
  ReplyResponse(msg);
}

struct CompileTask::RenameParam {
  string oldpath;
  string newpath;
};

void CompileTask::RenameCallback(RenameParam* param, string* err) {
  err->clear();
  int r = rename(param->oldpath.c_str(), param->newpath.c_str());
  if (r == 0) {
    return;
  }
  // if errno != EEXIST, log, AddErrorToResponse and returns without
  // setting *err (so no retry in DoOutput), since non-EEXIST error might
  // not be worth to retry?
  std::ostringstream ss;
  ss << "rename error:" << param->oldpath << " " << param->newpath
     << " errno=" << errno;
  *err = ss.str();
}

struct CompileTask::ContentOutputParam {
  ContentOutputParam() : info(nullptr) {}
  string filename;
  OutputFileInfo* info;
};

void CompileTask::ContentOutputCallback(
    ContentOutputParam* param, string* err) {
  err->clear();
  remove(param->filename.c_str());
  std::unique_ptr<FileServiceClient::Output> fout(
      FileServiceClient::FileOutput(param->filename, param->info->mode));
  if (!fout->IsValid()) {
    std::ostringstream ss;
    ss << "open for write error:" << param->filename;
    *err = ss.str();
    return;
  }
  if (!fout->WriteAt(0L, param->info->content) || !fout->Close()) {
    std::ostringstream ss;
    ss << "write error:" << param->filename;
    *err = ss.str();
    return;
  }
}

#ifdef _WIN32
void CompileTask::DoOutput(const string& opname, const string& filename,
                           PermanentClosure* closure, string* err) {
  static const int kMaxDeleteRetryForDoOutput = 5;
  // Large sleep time will not harm a normal user.
  // Followings are executed after termination of the child process,
  // and deletion usually succeeds without retrying.
  static const int kInitialRetrySleepInMs = 100;
  // On Posix, rename success if target file already exists and it is
  // in writable directory.
  // On Win32, rename will fail if target file already exists, so we
  // need to delete it explicitly before rename.
  // In this code, we assume a file is temporary locked by a process
  // like AntiVirus, and the lock will be released for a while.
  //
  // You may consider to use MoveFileEx with MOVEFILE_REPLACE_EXISTING.
  // Calling it may take forever and stall compiler_proxy if the process
  // having the lock is not behaving. As a result, we do not use it.
  int sleep_in_ms = kInitialRetrySleepInMs;
  for (int retry = 0; retry < kMaxDeleteRetryForDoOutput; ++retry) {
    closure->Run();
    if (err->empty()) {
      return;
    }
    LOG(WARNING) << trace_id_ << " DoOutput operation failed."
                 << " opname=" << opname
                 << " filename=" << filename
                 << " err=" << *err;

    // TODO: identify a process that has a file lock.
    // As far as I know, people seems to use NtQueryInformationProcess,
    // which is an obsoleted function, to list up processes.

    // http://msdn.microsoft.com/en-us/library/windows/desktop/aa364944(v=vs.85).aspx
    DWORD attr = GetFileAttributesA(filename.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) {
      LOG_SYSRESULT(GetLastError());
      std::ostringstream ss;
      ss << opname << " failed but GetFileAttributes "
         << "returns INVALID_FILE_ATTRIBUTES"
         << " filename=" << filename
         << " attr=" << attr;
      AddErrorToResponse(TO_USER, ss.str(), true);
      return;
    }

    LOG(INFO) << trace_id_ << " "
              << "The file exists. We need to remove."
              << " filename=" << filename
              << " attr=" << attr;
    if (remove(filename.c_str()) == 0) {
      LOG(INFO) << trace_id_ << " "
                << "Delete succeeds."
                << " filename=" << filename;
      continue;
    }

    LOG(WARNING) << trace_id_ << " "
                 << "Failed to delete file:"
                 << " filename=" << filename
                 << " retry=" << retry
                 << " sleep_in_ms=" << sleep_in_ms;
    Sleep(sleep_in_ms);
    sleep_in_ms *= 2;
  }
  if (err->empty()) {
    std::ostringstream ss;
    ss << opname << " failed but err is empty?";
    *err = ss.str();
  }
  PLOG(ERROR) << trace_id_ << " " << *err;
  AddErrorToResponse(TO_USER, *err, true);
}
#else
void CompileTask::DoOutput(const string& opname,
                           const string& filename,
                           PermanentClosure* closure,
                           string* err) {
  closure->Run();
  if (!err->empty()) {
    PLOG(ERROR) << trace_id_ << " DoOutput operation failed."
                << " opname=" << opname
                << " filename=" << filename
                << " err=" << *err;
    AddErrorToResponse(TO_USER, *err, true);
  }
}
#endif

void CompileTask::RewriteCoffTimestamp(const string& filename) {
  absl::string_view ext = file::Extension(filename);
  if (ext != "obj")
    return;

  ScopedFd fd(ScopedFd::OpenForRewrite(filename));
  if (!fd.valid()) {
    LOG(ERROR) << trace_id_ << " failed to open file for coff rewrite: "
               << filename;
    return;
  }

  // Check COFF file header. COFF header is like this.
  // c.f. http://delorie.com/djgpp/doc/coff/
  // 0-1   version. must be 0x014c for x86, 0x8664 for x64
  // 2-3   number of sections (not necessary for us)
  // 4-7   timestamp
  // ...
  //
  // All numeric fields are stored in host native order.
  // Currently we're checking magic is x86 or x64, all numeric
  // should be little endian here.
  //
  // When /bigobj is specified in cl.exe, microsoft extends COFF file format
  // to accept more sections.
  // In this case, the file header is like this:
  // 0-1   0x0000 (IMAGE_FILE_MACHINE_UNKNOWN)
  // 2-3   0xFFFF
  // 4-5   version (0x0001 or 0x0002)
  // 6-7   machine (0x014c or 0x8664)
  // 8-11  timestamp
  // 12-27 uuid: 38feb30ca5d9ab4dac9bd6b6222653c2 for version 0x0001
  //             c7a1bad1eebaa94baf20faf66aa4dcb8 for version 0x0002
  //
  // TODO: Find bigobj version 1 document and add link here.

  unsigned char buf[32];
  ssize_t read_byte = fd.Read(buf, sizeof(buf));
  if (read_byte != sizeof(buf)) {
    LOG(ERROR) << trace_id_
               << " couldn't read the first " << sizeof(buf)
               << " byte. file is too small?"
               << " filename=" << filename
               << " read_byte=" << read_byte;
    return;
  }

  unsigned short magic = *reinterpret_cast<unsigned short*>(buf);
  int offset = 0;
  if (magic == 0x014c || magic == 0x8664) {
    offset = 4;
  } else if (IsBigobjFormat(buf)) {
    offset = 8;
  }
  if (offset > 0) {
    unsigned int old = *reinterpret_cast<unsigned int*>(buf + offset);
    unsigned int now = time(nullptr);

    fd.Seek(offset, ScopedFd::SeekAbsolute);
    fd.Write(&now, 4);

    LOG(INFO) << trace_id_
              << " Rewriting timestamp:" << " file=" << filename
              << " offset=" << offset
              << " old=" << old << " new=" << now;
    return;
  }

  std::stringstream ss;
  for (size_t i = 0; i < sizeof(buf); ++i) {
    ss << std::hex << std::setw(2) << std::setfill('0')
       << (static_cast<unsigned int>(buf[i]) & 0xFF);
  }
  LOG(ERROR) << trace_id_
             << " Unknown COFF header."
             << " filename=" << filename
             << " first " << sizeof(buf) << "byte=" << ss.str();
  return;
}

void CompileTask::CommitOutput(bool use_remote) {
  VLOG(1) << trace_id_ << " commit output " << use_remote;
  DCHECK(BelongsToCurrentThread());
  CHECK(state_ == FINISHED);
  CHECK(!abort_);
  CHECK(subproc_ == nullptr);
  CHECK(delayed_setup_subproc_ == nullptr);

  std::vector<string> output_bases;
  bool has_obj = false;

  for (auto& info : output_file_) {
    SimpleTimer timer;
    const string& filename = info.filename;
    const string& tmp_filename = info.tmp_filename;
    // TODO: fix to support cas digest.
    const string& hash_key = info.hash_key;
    DCHECK(!hash_key.empty()) << filename;
    const bool use_content = tmp_filename.empty();
    bool need_rename = !tmp_filename.empty() && tmp_filename != filename;
    if (!use_remote) {
      // If use_remote is false, we should have outputs of local process.
      VLOG(1) << trace_id_ << " commit output (use local) in "
              << filename;
      if (access(filename.c_str(), R_OK) == 0) {
        if (need_rename) {
          // We might have written tmp file for remote output, but decided
          // to use local output.
          // In this case, we want to remove tmp file of remote output.
          remove(tmp_filename.c_str());
        }
      } else {
        // !use_remote, but local output doesn't exist?
        PLOG(ERROR) << trace_id_ << " " << filename;
      }
      if (use_content) {
        VLOG(1) << trace_id_ << " release buffer of remote output";
        service_->ReleaseOutputBuffer(info.size, &info.content);
      }
      need_rename = false;
    } else if (use_content) {
      // If use_remote is true, and use_content is true,
      // write content (remote output) in filename.
      VLOG(1) << trace_id_ << " commit output (use remote content) to "
              << filename;
      ContentOutputParam param;
      param.filename = filename;
      param.info = &info;
      string err;
      std::unique_ptr<PermanentClosure> callback(
          NewPermanentCallback(
              this,
              &CompileTask::ContentOutputCallback,
              &param, &err));
      DoOutput("content_output", filename, callback.get(), &err);
      service_->ReleaseOutputBuffer(info.size, &info.content);
      need_rename = false;
    } else if (need_rename) {
      // If use_remote is true, use_content is false, and
      // need_rename is true, we wrote remote output in
      // tmp_filename, and we need to rename tmp_filename
      // to filename.
      VLOG(1) << trace_id_ << " commit output (use remote tmp file) "
              << "rename " << tmp_filename << " => " << filename;
      RenameParam param;
      param.oldpath = tmp_filename;
      param.newpath = filename;
      string err;
      std::unique_ptr<PermanentClosure> callback(
          NewPermanentCallback(
             this, &CompileTask::RenameCallback, &param, &err));
      DoOutput("rename", filename, callback.get(), &err);
    } else {
      // If use_remote is true, use_content is false, and
      // need_rename is false, we wrote remote output in
      // filename, so do nothing here.
      VLOG(1) << trace_id_ << " commit output (use remote file) in "
              << filename;
    }

    // Incremental Link doesn't work well if object file timestamp is wrong.
    // If it's Windows object file (.obj) from remote,
    // we'd like to rewrite timestamp when the content is from remote cache.
    // According to our measurement, this doesn't have
    // measureable performance penalty.
    // see b/24388745
    if (use_remote && stats_->cache_hit() &&
        flags_->type() == CompilerFlagType::Clexe) {
      // We should not rewrite coff if /Brepro or something similar is set.
      // See b/72768585
      const VCFlags& vc_flag = static_cast<const VCFlags&>(*flags_);
      if (!vc_flag.has_Brepro()) {
        RewriteCoffTimestamp(filename);
      }
    }

    service_->RecordOutputRename(need_rename);
    // The output file is generated in goma cache, so we believe the cache_key
    // is valid.  It would be used in link phase.
    service_->file_hash_cache()->StoreFileCacheKey(
        filename, hash_key, absl::Now(),
        output_file_stat_cache_->Get(filename));
    VLOG(1) << trace_id_ << " "
            << tmp_filename << " -> " << filename
            << " " << hash_key;
    LOG_IF(ERROR, !info.content.empty())
        << trace_id_ << " content was not released: " << filename;
    const absl::Duration duration = timer.GetDuration();
    LOG_IF(WARNING, duration > absl::Milliseconds(100))
          << trace_id_
          << " CommitOutput " << duration
          << " size=" << info.size
          << " filename=" << info.filename;
    absl::string_view output_base = file::Basename(info.filename);
    output_bases.push_back(string(output_base));
    absl::string_view ext = file::Extension(output_base);
    if (flags_->type() == CompilerFlagType::Gcc && ext == "o") {
      has_obj = true;
    } else if (flags_->type() == CompilerFlagType::Clexe && ext == "obj") {
      has_obj = true;
    } else if (flags_->type() == CompilerFlagType::Javac && ext == "class") {
      has_obj = true;
    }

  }
  output_file_.clear();

  // TODO: For clang-tidy, maybe we don't need to output
  // no obj warning?

  if (has_obj) {
    LOG(INFO) << trace_id_ << " CommitOutput num=" << output_bases.size()
              << " cache_key=" << resp_->cache_key()
              << ": " << output_bases;
  } else {
    LOG(WARNING) << trace_id_ << " CommitOutput num=" << output_bases.size()
                 << " no obj: cache_key=" << resp_->cache_key()
                 << ": " << output_bases;
  }
}

void CompileTask::ReplyResponse(const string& msg) {
  LOG(INFO) << trace_id_ << " ReplyResponse: " << msg;
  DCHECK(BelongsToCurrentThread());
  CHECK(state_ == FINISHED || state_ == LOCAL_FINISHED || abort_);
  CHECK(rpc_ != nullptr);
  CHECK(rpc_resp_ != nullptr);
  CHECK(subproc_ == nullptr);
  CHECK(delayed_setup_subproc_ == nullptr);

  if (failed() || fail_fallback_) {
    auto allowed_error_duration = service_->AllowedNetworkErrorDuration();
    auto error_start_time = service_->http_client()->NetworkErrorStartedTime();
    if (allowed_error_duration.has_value() && error_start_time.has_value()) {
      if (absl::Now() > *error_start_time + *allowed_error_duration) {
        AddErrorToResponse(
            TO_USER, "network error continued for a long time", true);
      }
    }
  }

  if (resp_->has_result()) {
    VLOG(1) << trace_id_ << " exit=" << resp_->result().exit_status();
    stats_->set_exec_exit_status(resp_->result().exit_status());
  } else {
    LOG(WARNING) << trace_id_ << " empty result";
    stats_->set_exec_exit_status(-256);
  }
  if (service_->local_run_for_failed_input() && flags_.get() != nullptr) {
    service_->RecordInputResult(flags_->input_filenames(),
                                stats_->exec_exit_status() == 0);
  }
  if (resp_->error_message_size() != 0) {
    std::vector<string> errs(resp_->error_message().begin(),
                             resp_->error_message().end());
    LOG_IF(ERROR, resp_->result().exit_status() == 0)
        << trace_id_ << " should not have error message on exit_status=0."
        << " errs=" << errs;
    service_->RecordErrorsToUser(errs);
  }
  UpdateStats();
  *rpc_resp_ = *resp_;
  // Here, rpc_resp_ has created, so we can set gomacc_resp_size. b/109783082
  stats_->gomacc_resp_size = rpc_resp_->ByteSize();

  OneshotClosure* done = done_;
  done_ = nullptr;
  rpc_resp_ = nullptr;
  rpc_ = nullptr;
  if (done) {
    service_->wm()->RunClosureInThread(
        FROM_HERE,
        caller_thread_id_, done, WorkerThread::PRIORITY_IMMEDIATE);
  }
  if (!canceled_ && stats_->exec_exit_status() != 0) {
    if (exit_status_ == 0 && subproc_exit_status_ == 0) {
      stats_->set_compiler_proxy_error(true);
      LOG(ERROR) << trace_id_ << " compilation failure "
                 << "due to compiler_proxy error.";
    }
  }

  // The caching of the HTTP return code in |response_code_| might not be
  // required, but let's keep it to be safe. If we directly wrote
  // |http_rpc_status_->http_return_code| to the JSON output in DumpToJson(), it
  // will probably work. However, there is no guarantee that http_rpc_status_
  // won't be overwritten by a new call to ProcessCallExec() before calling
  // DumpToJson().
  response_code_ = http_rpc_status_->http_return_code;

  stats_->handler_time = handler_timer_.GetDuration();
  stats_->set_handler_time(DurationToIntMs(stats_->handler_time));
  gomacc_pid_ = SubProcessState::kInvalidPid;

  if (stats_->handler_time > absl::Minutes(5)) {
    ExecLog stats = *stats_;
    // clear non-stats fields.
    stats.clear_username();
    stats.clear_nodename();
    stats.clear_port();
    stats.clear_compiler_proxy_start_time();
    stats.clear_task_id();
    stats.clear_compiler_proxy_user_agent();
    stats.clear_start_time();
    stats.clear_arg();
    stats.clear_env();
    stats.clear_cwd();
    stats.clear_expanded_arg();
    stats.clear_command_version();
    stats.clear_command_target();
    LOG(ERROR) << trace_id_ << " SLOW:" << stats.DebugString();
  }

  // if abort_, remote process is still on the fly.
  // Done() will be called later in ProcessFinished.
  if (abort_)
    CHECK(!finished_);
  // if local_output_file_callback_ is not nullptr, uploading local output file
  // is on the fly, so ProcessLocalFileOutputDone() will be called later.
  if (finished_ && local_output_file_callback_ == nullptr) {
    CHECK_GE(state_, FINISHED);
    CHECK_EQ(0, num_local_output_file_task_);
    Done();
  }
}

void CompileTask::ProcessLocalFileOutput() {
  VLOG(1) << trace_id_ << " local output";
  CHECK(BelongsToCurrentThread());
  CHECK(local_output_file_callback_ == nullptr);
  CHECK_EQ(0, num_local_output_file_task_);
  if (!service_->store_local_run_output())
    return;

  SetLocalOutputFileCallback();
  std::vector<OneshotClosure*> closures;
  for (const auto& output_file : flags_->output_files()) {
    const string& filename =
        file::JoinPathRespectAbsolute(flags_->cwd(), output_file);
    // only uploads *.o
    if (!absl::EndsWith(filename, ".o"))
      continue;
    string hash_key;
    const FileStat& output_file_stat = output_file_stat_cache_->Get(filename);
    bool found_in_cache = service_->file_hash_cache()->GetFileCacheKey(
        filename, absl::nullopt, output_file_stat, &hash_key);
    if (found_in_cache) {
      VLOG(1) << "file:" << filename << " already on cache: " << hash_key;
      continue;
    }
    LOG(INFO) << trace_id_ << " local output:" << filename;
    std::unique_ptr<LocalOutputFileTask> local_output_file_task(
        new LocalOutputFileTask(
            service_->wm(),
            service_->blob_client()->NewUploader(
                filename, requester_info_, trace_id_),
            service_->file_hash_cache(), output_file_stat_cache_->Get(filename),
            this, filename));

    LocalOutputFileTask* local_output_file_task_pointer =
        local_output_file_task.get();

    closures.push_back(
        NewCallback(
            local_output_file_task_pointer,
            &LocalOutputFileTask::Run,
            NewCallback(
                this,
                &CompileTask::LocalOutputFileTaskFinished,
                std::move(local_output_file_task))));
  }
  if (closures.empty()) {
    VLOG(1) << trace_id_ << " no local output upload";
    service_->wm()->RunClosureInThread(
        FROM_HERE,
        thread_id_,
        NewCallback(
            this,
            &CompileTask::MaybeRunLocalOutputFileCallback, false),
        WorkerThread::PRIORITY_LOW);
    return;
  }
  for (auto* closure : closures)
    service_->wm()->RunClosure(
        FROM_HERE, closure, WorkerThread::PRIORITY_LOW);
}

void CompileTask::ProcessLocalFileOutputDone() {
  VLOG(1) << trace_id_ << " local output done";
  CHECK(BelongsToCurrentThread());
  local_output_file_callback_ = nullptr;
  if (finished_) {
    CHECK(subproc_ == nullptr);
    CHECK(delayed_setup_subproc_ == nullptr);
    Done();
    return;
  }
  // if !finished_, remote call is still on the fly, and eventually
  // ProcessFinished will be called, and Done will be called
  // because local_output_file_callback_ is already nullptr.
}

void CompileTask::Done() {
  VLOG(1) << trace_id_ << " Done";
  // FINISHED: normal case.
  // LOCAL_FINISHED: fallback by should_fallback_.
  // abort_: idle fallback.
  replied_ = true;
  if (!abort_)
    CHECK_GE(state_, FINISHED);
  CHECK(rpc_ == nullptr) << trace_id_
                      << " " << StateName(state_) << " abort:" << abort_;
  CHECK(rpc_resp_ == nullptr);
  CHECK(done_ == nullptr);
  CHECK(subproc_ == nullptr);
  CHECK(delayed_setup_subproc_ == nullptr);
  CHECK(input_file_callback_ == nullptr);
  CHECK(output_file_callback_ == nullptr);
  CHECK(local_output_file_callback_ == nullptr);
  ClearOutputFile();

  // If compile failed, delete deps cache entry here.
  if (DepsCache::IsEnabled()) {
    if ((failed() || fail_fallback_) && deps_identifier_.has_value()) {
      DepsCache::instance()->RemoveDependency(deps_identifier_);
      LOG(INFO) << trace_id_ << " remove deps cache entry.";
    }
  }

  SaveInfoFromInputOutput();
  service_->CompileTaskDone(this);
  VLOG(1) << trace_id_ << " finalized.";
}

void CompileTask::DumpToJson(bool need_detail, Json::Value* root) const {
  SubProcessState::State subproc_state = SubProcessState::NUM_STATE;
  pid_t subproc_pid = static_cast<pid_t>(SubProcessState::kInvalidPid);
  {
    AUTOLOCK(lock, &mu_);
    if (subproc_ != nullptr) {
      subproc_state = subproc_->state();
      subproc_pid = subproc_->started().pid();
    }
  }

  stats_->DumpToJson(root,
                     need_detail ? CompileStats::DumpDetailLevel::kDetailed
                                 : CompileStats::DumpDetailLevel::kNotDetailed);

  (*root)["id"] = id_;

  if ((state_ < FINISHED && !abort_) || state_ == LOCAL_RUN) {
    // Elapsed total time for current running process.
    // This field needs to be strictly in milliseconds so that it can be sorted.
    (*root)["elapsed"] =
        FormatDurationInMilliseconds(handler_timer_.GetDuration());
  }
  if (gomacc_pid_ != SubProcessState::kInvalidPid)
    (*root)["pid"] = gomacc_pid_;
  if (!flag_dump_.empty()) (*root)["flag"] = flag_dump_;
  (*root)["state"] = StateName(state_);
  if (abort_) (*root)["abort"] = 1;
  if (subproc_pid != static_cast<pid_t>(SubProcessState::kInvalidPid)) {
    (*root)["subproc_state"] =
        SubProcessState::State_Name(subproc_state);
    (*root)["subproc_pid"] = Json::Value::Int64(subproc_pid);
  }
  // for task color.
  if (response_code_) (*root)["http"] = response_code_;
  if (fail_fallback_) (*root)["fail_fallback"]= 1;
  if (canceled_)
    (*root)["canceled"] = 1;
  if (replied_)
    (*root)["replied"] = 1;

  // additional message
  if (gomacc_revision_mismatched_) {
    (*root)["gomacc_revision_mismatch"] = 1;
  }

  if (need_detail) {
    if (num_input_file_task_ > 0) {
      (*root)["num_input_file_task"] = num_input_file_task_;
    }
    {
      AUTOLOCK(lock, &mu_);
      if (!http_rpc_status_->response_header.empty()) {
        (*root)["response_header"] = http_rpc_status_->response_header;
      }
    }

    if (exec_output_file_.size() > 0) {
      Json::Value exec_output_file(Json::arrayValue);
      for (size_t i = 0; i < exec_output_file_.size(); ++i) {
        exec_output_file.append(exec_output_file_[i]);
      }
      (*root)["exec_output_file"] = exec_output_file;
    }
    if (!resp_cache_key_.empty())
      (*root)["cache_key"] = resp_cache_key_;

    if (exec_error_message_.size() > 0) {
      Json::Value error_message(Json::arrayValue);
      for (size_t i = 0; i < exec_error_message_.size(); ++i) {
        error_message.append(exec_error_message_[i]);
      }
      (*root)["error_message"] = error_message;
    }
    if (!orig_flag_dump_.empty())
        (*root)["orig_flag"] = orig_flag_dump_;
    if (!stdout_.empty())
      (*root)["stdout"] = stdout_;
    if (!stderr_.empty())
      (*root)["stderr"] = stderr_;

    Json::Value inputs(Json::arrayValue);
    for (std::set<string>::const_iterator iter = required_files_.begin();
         iter != required_files_.end();
         ++iter) {
      inputs.append(*iter);
    }
    (*root)["inputs"] = inputs;

    if (system_library_paths_.size() > 0) {
      Json::Value system_library_paths(Json::arrayValue);
      for (size_t i = 0; i < system_library_paths_.size(); ++i) {
        system_library_paths.append(system_library_paths_[i]);
      }
      (*root)["system_library_paths"] = system_library_paths;
    }

  } else {
    (*root)["summaryOnly"] = 1;
  }
}

// ----------------------------------------------------------------
// state_: INIT
void CompileTask::CopyEnvFromRequest() {
  CHECK_EQ(INIT, state_);
  requester_env_ = req_->requester_env();
  want_fallback_ = requester_env_.fallback();
  req_->clear_requester_env();

  for (const auto& arg : req_->arg())
    stats_->add_arg(arg);
  for (const auto& env : req_->env())
    stats_->add_env(env);
  stats_->set_cwd(req_->cwd());

  gomacc_pid_ = req_->requester_info().pid();

  if (service_->CanSendUserInfo()) {
    if (!service_->username().empty())
      req_->mutable_requester_info()->set_username(service_->username());
    stats_->set_username(req_->requester_info().username());
    stats_->set_nodename(service_->nodename());
  }
  if (req_->requester_info().has_build_id()) {
    stats_->set_build_id(req_->requester_info().build_id());
    LOG(INFO) << trace_id_ << " build_id:" << req_->requester_info().build_id();
  }
  req_->mutable_requester_info()->set_compiler_proxy_id(
      GenerateCompilerProxyId());

  // TODO: Is here the best position to set these requester info?
  // pathtype/dimension might be changed after compiler_proxy understand the
  // compile request well.
#ifdef _WIN32
  req_->mutable_requester_info()->add_dimensions("os:win");
  req_->mutable_requester_info()->set_path_style(RequesterInfo::WINDOWS_STYLE);
#elif defined(__MACH__)
  req_->mutable_requester_info()->add_dimensions("os:mac");
  req_->mutable_requester_info()->set_path_style(RequesterInfo::POSIX_STYLE);
#elif defined(__linux__)
  req_->mutable_requester_info()->add_dimensions("os:linux");
  req_->mutable_requester_info()->set_path_style(RequesterInfo::POSIX_STYLE);
#else
#error "unsupported platform"
#endif

  stats_->set_port(rpc_->server_port());
  // TODO: Convert field to protobuf/timestamp.
  stats_->set_compiler_proxy_start_time(absl::ToTimeT(service_->start_time()));
  stats_->set_task_id(id_);
  requester_info_ = req_->requester_info();
}

string CompileTask::GenerateCompilerProxyId() const {
  std::ostringstream s;
  s << service_->compiler_proxy_id_prefix() << id_;
  return s.str();
}

void CompileTask::InitCompilerFlags() {
  CHECK_EQ(INIT, state_);
  std::vector<string> args(req_->arg().begin(), req_->arg().end());
  VLOG(1) << trace_id_ << " " << args;
  flags_ = CompilerFlagsParser::New(args, req_->cwd());
  if (flags_.get() == nullptr) {
    return;
  }
  compiler_type_specific_ =
      service_->compiler_type_specific_collection()->Get(flags_->type());

  flag_dump_ = flags_->DebugString();
  if (flags_->type() == CompilerFlagType::Gcc) {
    const GCCFlags& gcc_flag = static_cast<const GCCFlags&>(*flags_);
    linking_ = (gcc_flag.mode() == GCCFlags::LINK);
    precompiling_ = gcc_flag.is_precompiling_header();
  } else if (flags_->type() == CompilerFlagType::Clexe) {
    // TODO: check linking_ etc.
  } else if (flags_->type() == CompilerFlagType::ClangTidy) {
    // Sets the actual gcc_flags for clang_tidy_flags here.
    ClangTidyFlags& clang_tidy_flags = static_cast<ClangTidyFlags&>(*flags_);
    if (clang_tidy_flags.input_filenames().size() != 1) {
      LOG(WARNING) << trace_id_ << " Input file is not unique.";
      clang_tidy_flags.set_is_successful(false);
      return;
    }
    const string& input_file = clang_tidy_flags.input_filenames()[0];
    const string input_file_abs =
        file::JoinPathRespectAbsolute(clang_tidy_flags.cwd(), input_file);
    string compdb_path = CompilationDatabaseReader::FindCompilationDatabase(
      clang_tidy_flags.build_path(), file::Dirname(input_file_abs));

    std::vector<string> clang_args;
    string build_dir;
    if (!CompilationDatabaseReader::MakeClangArgs(clang_tidy_flags,
                                                  compdb_path,
                                                  &clang_args,
                                                  &build_dir)) {
      // Failed to make clang args. Then Mark CompilerFlags unsuccessful.
      LOG(WARNING) << trace_id_
                   << " Failed to make clang args. local fallback.";
      clang_tidy_flags.set_is_successful(false);
      return;
    }

    DCHECK(!build_dir.empty());
    clang_tidy_flags.SetCompilationDatabasePath(compdb_path);
    clang_tidy_flags.SetClangArgs(clang_args, build_dir);
  }
}

bool CompileTask::FindLocalCompilerPath() {
  CHECK_EQ(INIT, state_);
  CHECK(flags_.get());

  // If gomacc sets local_compiler_path, just use it.
  if (!req_->command_spec().local_compiler_path().empty()) {
    string local_compiler = PathResolver::PlatformConvert(
        req_->command_spec().local_compiler_path());

    // TODO: confirm why local_compiler_path should not be
    //                    basename, and remove the code if possible.
    // local_compiler_path should not be basename only.
    if (local_compiler.find(PathResolver::kPathSep) == string::npos) {
      LOG(ERROR) << trace_id_ << " local_compiler_path should not be basename:"
                 << local_compiler;
    } else if (service_->FindLocalCompilerPath(
        requester_env_.gomacc_path(),
        local_compiler,
        stats_->cwd(),
        requester_env_.local_path(),
        pathext_,
        &local_compiler,
        &local_path_)) {
      // Since compiler_info resolves relative path to absolute path,
      // we do not need to make local_comiler_path to absolute path
      // any more. (b/6340137, b/28088682)
      if (!pathext_.empty() &&
          !absl::EndsWith(local_compiler,
                             req_->command_spec().local_compiler_path())) {
        // PathExt should be resolved on Windows.  Let me use it.
        req_->mutable_command_spec()->set_local_compiler_path(local_compiler);
      }
      return true;
    }
    return false;
  }

  if (!requester_env_.has_local_path() ||
      requester_env_.local_path().empty()) {
    LOG(ERROR) << "no PATH in requester env." << requester_env_.DebugString();
    AddErrorToResponse(TO_USER,
                       "no PATH in requester env.  Using old gomacc?", true);
    return false;
  }
  if (!requester_env_.has_gomacc_path()) {
    LOG(ERROR) << "no gomacc path in requester env."
               << requester_env_.DebugString();
    AddErrorToResponse(TO_USER,
                       "no gomacc in requester env.  Using old gomacc?", true);
    return false;
  }

  string local_compiler_path;
  if (service_->FindLocalCompilerPath(
          requester_env_.gomacc_path(),
          flags_->compiler_base_name(),
          stats_->cwd(),
          requester_env_.local_path(),
          pathext_,
          &local_compiler_path,
          &local_path_)) {
    req_->mutable_command_spec()->set_local_compiler_path(
          local_compiler_path);
    return true;
  }
  return false;
}

bool CompileTask::ShouldFallback() const {
  CHECK_EQ(INIT, state_);
  CHECK(flags_.get());
  if (!requester_env_.verify_command().empty())
    return false;
  if (!flags_->is_successful()) {
    service_->RecordForcedFallbackInSetup(CompileService::kFailToParseFlags);
    LOG(INFO) << trace_id_
              << " force fallback. failed to parse compiler flags.";
    return true;
  }
  if (flags_->input_filenames().empty()) {
    service_->RecordForcedFallbackInSetup(
        CompileService::kNoRemoteCompileSupported);
    LOG(INFO) << trace_id_
              << " force fallback. no input files give.";
    return true;
  }
  if (flags_->type() == CompilerFlagType::Gcc) {
    const GCCFlags& gcc_flag = static_cast<const GCCFlags&>(*flags_);
    if (gcc_flag.is_stdin_input()) {
      service_->RecordForcedFallbackInSetup(
          CompileService::kNoRemoteCompileSupported);
      LOG(INFO) << trace_id_
                << " force fallback."
                << " cannot use stdin as input in goma backend.";
      return true;
    }
    if (gcc_flag.has_wrapper()) {
      service_->RecordForcedFallbackInSetup(
          CompileService::kNoRemoteCompileSupported);
      LOG(INFO) << trace_id_
                << " force fallback. -wrapper is not supported";
      return true;
    }
    if (!verify_output_ && gcc_flag.mode() == GCCFlags::PREPROCESS) {
      service_->RecordForcedFallbackInSetup(
          CompileService::kNoRemoteCompileSupported);
      LOG(INFO) << trace_id_
                << " force fallback. preprocess is usually light-weight.";
      return true;
    }
    if (!service_->enable_gch_hack() && precompiling_) {
      service_->RecordForcedFallbackInSetup(
          CompileService::kNoRemoteCompileSupported);
      LOG(INFO) << trace_id_
                << " force fallback. gch hack is not enabled and precompiling.";
      return true;
    }
    if (!service_->enable_remote_link() && linking_) {
      service_->RecordForcedFallbackInSetup(
          CompileService::kNoRemoteCompileSupported);
      LOG(INFO) << trace_id_
                << " force fallback linking.";
      return true;
    }
    absl::string_view ext = file::Extension(flags_->input_filenames()[0]);
    if (ext == "s" || ext == "S") {
      service_->RecordForcedFallbackInSetup(
          CompileService::kNoRemoteCompileSupported);
      LOG(INFO) << trace_id_
                << " force fallback. assembler should be light-weight.";
      return true;
    }
  } else if (flags_->type() == CompilerFlagType::Clexe) {
    const VCFlags& vc_flag = static_cast<const VCFlags&>(*flags_);
    // GOMA doesn't work with PCH so we generate it only for local builds.
    if (!vc_flag.creating_pch().empty()) {
      service_->RecordForcedFallbackInSetup(
          CompileService::kNoRemoteCompileSupported);
      LOG(INFO) << trace_id_
                << " force fallback. cannot create pch in goma backend.";
      return true;
    }
    if (vc_flag.require_mspdbserv()) {
      service_->RecordForcedFallbackInSetup(
          CompileService::kNoRemoteCompileSupported);
      LOG(INFO) << trace_id_
                << " force fallback. cannot run mspdbserv in goma backend.";
      return true;
    }
  } else if (flags_->type() == CompilerFlagType::Javac) {
    const JavacFlags& javac_flag = static_cast<const JavacFlags&>(*flags_);
    // TODO: remove following code when goma backend get ready.
    // Force fallback a compile request with -processor (b/38215808)
    if (!javac_flag.processors().empty()) {
      service_->RecordForcedFallbackInSetup(
          CompileService::kNoRemoteCompileSupported);
      LOG(INFO) << trace_id_
                << " force fallback to avoid running annotation processor in"
                << " goma backend (b/38215808)";
      return true;
    }
  } else if (flags_->type() == CompilerFlagType::Java) {
    LOG(INFO) << trace_id_
              << " force fallback to avoid running java program in"
              << " goma backend";
    return true;
  }

#ifndef _WIN32
  // TODO: check "NUL", "CON", "AUX" on windows?
  for (const auto & input_filename : flags_->input_filenames()) {
    const string input = file::JoinPathRespectAbsolute(
        flags_->cwd(), input_filename);
    struct stat st;
    if (stat(input.c_str(), &st) != 0) {
      PLOG(INFO) << trace_id_ << " " << input << ": stat error";
      service_->RecordForcedFallbackInSetup(
          CompileService::kNoRemoteCompileSupported);
      return true;
    }
    if (!S_ISREG(st.st_mode)) {
      LOG(INFO) << trace_id_ << " " << input << " not regular file";
      service_->RecordForcedFallbackInSetup(
          CompileService::kNoRemoteCompileSupported);
      return true;
    }
  }
#endif

  // TODO: fallback input file should be flag of compiler proxy?
  if (requester_env_.fallback_input_file_size() == 0)
    return false;

  std::vector<string> fallback_input_files(
      requester_env_.fallback_input_file().begin(),
      requester_env_.fallback_input_file().end());
  std::sort(fallback_input_files.begin(), fallback_input_files.end());
  for (const auto& input_filename : flags_->input_filenames()) {
    if (binary_search(fallback_input_files.begin(),
                      fallback_input_files.end(),
                      input_filename)) {
      service_->RecordForcedFallbackInSetup(CompileService::kRequestedByUser);
      return true;
    }
  }
  return false;
}

bool CompileTask::ShouldVerifyOutput() const {
  CHECK_EQ(INIT, state_);
  return requester_env_.verify_output();
}

SubProcessReq::Weight CompileTask::GetTaskWeight() const {
  CHECK_EQ(INIT, state_);
  int weight_score = req_->arg_size();
  if (linking_)
    weight_score *= 10;

  if (weight_score > 1000)
    return SubProcessReq::HEAVY_WEIGHT;
  return SubProcessReq::LIGHT_WEIGHT;
}

bool CompileTask::ShouldStopGoma() const {
  if (verify_output_)
    return false;
  if (precompiling_ && service_->enable_gch_hack())
    return false;
  if (subproc_ == nullptr) {
    DCHECK(!abort_);
    return false;
  }
  if (IsSubprocRunning()) {
    if (service_->dont_kill_subprocess()) {
      // When dont_kill_subprocess is true, we'll ignore remote results and
      // always use local results, so calling remote is not useless when
      // subprocess is already running.
      return true;
    }
    if (service_->local_run_preference() >= state_)
      return true;
  }
  if (stats_->exec_request_retry() > 1) {
    int num_pending = SubProcessTask::NumPending();
    // Prefer local when pendings are few.
    return num_pending <= service_->max_subprocs_pending();
  }
  if (service_->http_client()->ramp_up() == 0) {
    // If http blocked (i.e. got 302, 403 error), stop calling remote.
    LOG(INFO) << trace_id_ << " stop goma. http disabled";
    return true;
  }
  return false;
}

// ----------------------------------------------------------------
// state_: SETUP
void CompileTask::FillCompilerInfo() {
  CHECK_EQ(SETUP, state_);

  compiler_info_timer_.Start();

  std::vector<string> key_envs(stats_->env().begin(), stats_->env().end());
  std::vector<string> run_envs(key_envs);
  if (!local_path_.empty())
    run_envs.push_back("PATH=" + local_path_);
#ifdef _WIN32
  if (!pathext_.empty())
    run_envs.push_back("PATHEXT=" + pathext_);
  if (flags_->type() == CompilerFlagType::Clexe) {
    run_envs.push_back("TMP=" + service_->tmp_dir());
    run_envs.push_back("TEMP=" + service_->tmp_dir());
  }
#endif
  std::unique_ptr<CompileService::GetCompilerInfoParam> param(
      new CompileService::GetCompilerInfoParam);
  param->thread_id = service_->wm()->GetCurrentThreadId();
  param->trace_id = trace_id_;
  DCHECK_NE(
      req_->command_spec().local_compiler_path().find(PathResolver::kPathSep),
      string::npos)
      << trace_id_ << " expect local_compiler_path is relative path"
      " or absolute path but " << req_->command_spec().local_compiler_path();
  param->key = CompilerInfoCache::CreateKey(
      *flags_,
      req_->command_spec().local_compiler_path(),
      key_envs);
  param->flags = flags_.get();
  param->run_envs = run_envs;

  CompileService::GetCompilerInfoParam* param_pointer = param.get();
  service_->GetCompilerInfo(
      param_pointer,
      NewCallback(
          this, &CompileTask::FillCompilerInfoDone, std::move(param)));
}

void CompileTask::FillCompilerInfoDone(
    std::unique_ptr<CompileService::GetCompilerInfoParam> param) {
  CHECK_EQ(SETUP, state_);

  const absl::Duration compiler_info_time = compiler_info_timer_.GetDuration();
  stats_->set_compiler_info_process_time(DurationToIntMs(compiler_info_time));
  stats_->compiler_info_process_time = compiler_info_time;
  std::ostringstream ss;
  ss << " cache_hit=" << param->cache_hit
     << " updated=" << param->updated
     << " state=" << param->state.get()
     << " in " << compiler_info_time;
  if (compiler_info_time > absl::Seconds(1)) {
    LOG(WARNING) << trace_id_ << " SLOW fill compiler info"
                 << ss.str();
  } else {
    LOG(INFO) << trace_id_ << " fill compiler info"
              << ss.str();
  }

  if (param->state.get() == nullptr) {
    AddErrorToResponse(TO_USER,
                       "something wrong trying to get compiler info.", true);
    service_->RecordForcedFallbackInSetup(
        CompileService::kFailToGetCompilerInfo);
    SetupRequestDone(false);
    return;
  }

  compiler_info_state_ = std::move(param->state);
  DCHECK(compiler_info_state_.get() != nullptr);

  if (compiler_info_state_.get()->info().HasError()) {
    // In this case, it found local compiler, but failed to get necessary
    // information, such as system include paths.
    // It would happen when multiple -arch options are used.
    if (requester_env_.fallback()) {
      // Force to fallback mode to handle this case.
      should_fallback_ = true;
      service_->RecordForcedFallbackInSetup(
          CompileService::kFailToGetCompilerInfo);
    }
    AddErrorToResponse(should_fallback_ ? TO_LOG : TO_USER,
                       compiler_info_state_.get()->info().error_message(),
                       true);
    SetupRequestDone(false);
    return;
  }
  if (compiler_info_state_.disabled()) {
    // In this case, it found local compiler, but not in server side
    // (by past compile task).
    if (service_->hermetic_fallback() || requester_env_.fallback()) {
      should_fallback_ = true;
      service_->RecordForcedFallbackInSetup(CompileService::kCompilerDisabled);
    }
    // we already responded "<local compiler path> is disabled" when it
    // was disabled the compiler info, so won't show the same error message
    // to user.
    AddErrorToResponse(TO_LOG, "compiler is disabled", true);
    SetupRequestDone(false);
    return;
  }
  if (service_->hermetic()) {
    req_->set_hermetic_mode(true);
  }
#ifndef _WIN32
  if (service_->use_relative_paths_in_argv()) {
    MakeWeakRelativeInArgv();
  }
#endif
  MayUpdateSubprogramSpec();
  UpdateExpandedArgs();
  if (service_->send_expected_outputs()) {
    SetExpectedOutputs();
  }
  ModifyRequestArgs();
  ModifyRequestEnvs();
  UpdateCommandSpec();
  stats_->set_command_version(req_->command_spec().version());
  stats_->set_command_target(req_->command_spec().target());

  UpdateRequiredFiles();
}

void CompileTask::UpdateRequiredFiles() {
  CHECK_EQ(SETUP, state_);
  include_timer_.Start();
  include_wait_timer_.Start();

  // Go to the general include processor phase.
  StartIncludeProcessor();
}

void CompileTask::UpdateRequiredFilesDone(bool ok) {
  if (!ok) {
    // Failed to update required_files.
    if (requester_env_.verify_command().empty()) {
      LOG(INFO) << trace_id_ << " failed to update required files. ";
      should_fallback_ = true;
      SetupRequestDone(false);
      return;
    }
    VLOG(1) << trace_id_ << "verify_command="
            << requester_env_.verify_command();
  }
  // Add the input files as well.
  for (const auto& input_filename : flags_->input_filenames()) {
    required_files_.insert(input_filename);
  }
  for (const auto& opt_input_filename: flags_->optional_input_filenames()) {
    const string& abs_filename = file::JoinPathRespectAbsolute(
        stats_->cwd(), opt_input_filename);
    if (access(abs_filename.c_str(), R_OK) == 0) {
      required_files_.insert(opt_input_filename);
    } else {
      LOG(WARNING) << trace_id_ << " optional file not found:" << abs_filename;
    }
  }
  // If gomacc sets input file, add them as well.
  for (const auto& input : req_->input()) {
    required_files_.insert(input.filename());
  }
  if (VLOG_IS_ON(2)) {
    for (const auto& required_file : required_files_) {
      LOG(INFO) << trace_id_ << " required files:" << required_file;
    }
  }
  req_->clear_input();

  const absl::Duration include_preprocess_time = include_timer_.GetDuration();
  stats_->set_include_preprocess_time(DurationToIntMs(include_preprocess_time));
  stats_->include_preprocess_time = include_preprocess_time;
  stats_->set_depscache_used(depscache_used_);

  LOG_IF(WARNING, stats_->include_processor_run_time > absl::Seconds(1))
      << trace_id_ << " SLOW run IncludeProcessor"
      << " required_files=" << required_files_.size()
      << " depscache=" << depscache_used_
      << " in " << stats_->include_processor_run_time;

  SetupRequestDone(true);
}

void CompileTask::SetupRequestDone(bool ok) {
  CHECK_EQ(SETUP, state_);

  if (abort_) {
    // subproc of local idle was already finished.
    ProcessFinished("aborted in setup");
    return;
  }

  if (canceled_) {
    ProcessFinished("canceled in setup");
    return;
  }

  if (!ok) {
    if (should_fallback_) {
      VLOG(1) << trace_id_ << " should fallback by setup failure";
      // should_fallback_ expects INIT state when subprocess finishes
      // in CompileTask::FinishSubProcess().
      state_ = INIT;
      if (subproc_ == nullptr)
        SetupSubProcess();
      RunSubProcess("fallback by setup failure");
      return;
    }
    // no fallback.
    AddErrorToResponse(TO_USER, "Failed to setup request", true);
    ProcessFinished("fail in setup");
    return;
  }
  TryProcessFileRequest();
}

#ifndef _WIN32
bool CompileTask::MakeWeakRelativeInArgv() {
  CHECK_EQ(SETUP, state_);
  DCHECK(compiler_info_state_.get() != nullptr);

  // Support only C/C++.
  if (compiler_info_state_.get()->info().type() != CompilerInfoType::Cxx) {
    return false;
  }

  orig_flag_dump_ = flag_dump_;
  // If cwd is in tmp directory, we can't know output path is
  // whether ./path/to/output or $TMP/path/to/output.
  // If latter, make the path relative would produce wrong output file.
  if (HasPrefixDir(req_->cwd(), "/tmp") || HasPrefixDir(req_->cwd(), "/var")) {
    LOG(WARNING) << "GOMA_USE_RELATIVE_PATHS_IN_ARGV=true, but cwd may be "
                 << "under temp directory: " << req_->cwd() << ". "
                 << "Use original args.";
    orig_flag_dump_ = "";
    return false;
  }
  bool changed = false;
  std::ostringstream ss;
  const std::vector<string>& parsed_args = CompilerFlagsUtil::MakeWeakRelative(
      flags_->args(), req_->cwd(),
      ToCxxCompilerInfo(compiler_info_state_.get()->info()));
  for (size_t i = 0; i < parsed_args.size(); ++i) {
    if (req_->arg(i) != parsed_args[i]) {
      VLOG(1) << "Arg[" << i << "]: " << req_->arg(i) << " => "
              << parsed_args[i];
      req_->set_arg(i, parsed_args[i]);
      changed = true;
    }
    ss << req_->arg(i) << " ";
  }
  flag_dump_ = ss.str();
  if (!changed) {
    VLOG(1) << "GOMA_USE_RELATIVE_PATHS_IN_ARGV=true, "
            << "but no argv changed";
    orig_flag_dump_ = "";
  }
  return changed;
}
#endif

static void FixCommandSpec(const CompilerInfo& compiler_info,
                           const CompilerFlags& flags,
                           CommandSpec* command_spec) {
  // Overwrites name in command_spec if possible.
  // The name is used for selecting a compiler in goma backend.
  // The name set by gomacc could be wrong if a given compiler, especially it is
  // cc or c++, is a symlink to non-gcc compiler. Since compiler_info knows
  // more details on the compiler, we overwrite the name with the value comes
  // from it.
  //
  // You may think we can use realpath(3) in gomacc. We do not do that because
  // of two reasons:
  // 1. compiler_info is cached.
  // 2. we can know more detailed info there.
  if (compiler_info.HasName())
    command_spec->set_name(compiler_info.name());

  if (!command_spec->has_version())
    command_spec->set_version(compiler_info.version());
  if (!command_spec->has_target())
    command_spec->set_target(compiler_info.target());
  command_spec->set_binary_hash(compiler_info.request_compiler_hash());
  command_spec->set_size(compiler_info.local_compiler_stat().size);

  command_spec->clear_system_include_path();
  command_spec->clear_cxx_system_include_path();
  command_spec->clear_system_framework_path();
  command_spec->clear_system_library_path();

  // C++ program should only send C++ include paths, otherwise, include order
  // might be wrong. For C program, cxx_system_include_paths would be empty.
  // c.f. b/25675250
  if (compiler_info.type() == CompilerInfoType::Cxx) {
    DCHECK(flags.type() == CompilerFlagType::Gcc ||
           flags.type() == CompilerFlagType::Clexe ||
           flags.type() == CompilerFlagType::ClangTidy)
        << flags.type();

    bool is_cplusplus = static_cast<const CxxFlags&>(flags).is_cplusplus();
    const CxxCompilerInfo& cxxci = ToCxxCompilerInfo(compiler_info);
    if (!is_cplusplus) {
      for (const auto& path : cxxci.system_include_paths())
        command_spec->add_system_include_path(path);
    }
    for (const auto& path : cxxci.cxx_system_include_paths())
      command_spec->add_cxx_system_include_path(path);
    for (const auto& path : cxxci.system_framework_paths())
      command_spec->add_system_framework_path(path);
  }
}

static void FixSystemLibraryPath(const std::vector<string>& library_paths,
                                 CommandSpec* command_spec) {
  for (const auto& path : library_paths)
    command_spec->add_system_library_path(path);
}

void CompileTask::UpdateExpandedArgs() {
  for (const auto& expanded_arg : flags_->expanded_args()) {
    req_->add_expanded_arg(expanded_arg);
    stats_->add_expanded_arg(expanded_arg);
  }
}

void CompileTask::SetExpectedOutputs() {
  for (const auto& file : flags_->output_files()) {
    req_->add_expected_output_files(file);
  }
  for (const auto& dir : flags_->output_dirs()) {
    req_->add_expected_output_dirs(dir);
  }
}

void CompileTask::ModifyRequestArgs() {
  DCHECK(compiler_info_state_.get() != nullptr);
  const CompilerInfo& compiler_info = compiler_info_state_.get()->info();
  bool found_executable_binary = false;
  for (const auto& r : compiler_info.resource()) {
    if (r.type == CompilerInfoData::EXECUTABLE_BINARY) {
      found_executable_binary = true;
      continue;
    }
    const string& path = r.name;
    req_->add_input()->set_filename(path);
    LOG(INFO) << trace_id_ << " input automatically added: " << path;
  }

  // Set toolchain information (if enabled and found)
  if (service_->send_compiler_binary_as_input() && found_executable_binary) {
    LOG(INFO) << trace_id_ << " input contains toolchain";
    req_->set_toolchain_included(true);

    for (const auto& r : compiler_info.resource()) {
      if (r.type != CompilerInfoData::EXECUTABLE_BINARY) {
        continue;
      }
      const string& path = r.name;
      req_->add_input()->set_filename(path);
      LOG(INFO) << trace_id_ << " input automatically added: " << path;
    }
  }

  bool modified_args = false;
  bool use_expanded_args = (req_->expanded_arg_size() > 0);
  for (const auto& flag : compiler_info.additional_flags()) {
    req_->add_arg(flag);
    if (use_expanded_args) {
      req_->add_expanded_arg(flag);
    }
    modified_args = true;
  }
  if (flags_->type() == CompilerFlagType::Clexe) {
    // If /Yu is specified, we add /Y- to tell the backend compiler not
    // to try using PCH. We add this here because we don't want to show
    // this flag in compiler_proxy's console.
    const string& using_pch = static_cast<const VCFlags&>(*flags_).using_pch();
    if (!using_pch.empty()) {
      req_->add_arg("/Y-");
      if (use_expanded_args) {
        req_->add_expanded_arg("/Y-");
      }
      modified_args = true;
    }
  }

  LOG_IF(INFO, modified_args) << trace_id_ << " modified args: "
                              << absl::StrJoin(req_->arg(), " ");
}

void CompileTask::ModifyRequestEnvs() {
  std::vector<string> envs;
  for (const auto& env : req_->env()) {
    if (flags_->IsServerImportantEnv(env.c_str())) {
      envs.push_back(env);
    }
  }
  if (envs.size() == (size_t)req_->env_size()) {
    return;
  }

  req_->clear_env();
  for (const auto& env : envs) {
    req_->add_env(env);
  }
  LOG(INFO) << trace_id_ << " modified env: " << envs;
}

void CompileTask::UpdateCommandSpec() {
  CHECK_EQ(SETUP, state_);
  command_spec_ = req_->command_spec();
  CommandSpec* command_spec = req_->mutable_command_spec();
  if (compiler_info_state_.get() == nullptr)
    return;
  const CompilerInfo& compiler_info = compiler_info_state_.get()->info();
  FixCommandSpec(compiler_info, *flags_, command_spec);
}

void CompileTask::MayFixSubprogramSpec(
    google::protobuf::RepeatedPtrField<SubprogramSpec>* subprogram_specs)
        const {
  std::set<string> used_subprogram_name;
  subprogram_specs->Clear();
  if (compiler_info_state_.get() == nullptr) {
    return;
  }
  for (const auto& info : compiler_info_state_.get()->info().subprograms()) {
    DCHECK(file::IsAbsolutePath(info.name))
        << "filename of subprogram is expected to be absolute path."
        << " info.name=" << info.name
        << " info.hash=" << info.hash;
    if (!used_subprogram_name.insert(info.name).second) {
      LOG(ERROR) << "The same subprogram is added twice.  Ignoring."
                 << " info.name=" << info.name
                 << " info.hash=" << info.hash;
      continue;
    }
    SubprogramSpec* subprog_spec = subprogram_specs->Add();
    subprog_spec->set_path(info.name);
    subprog_spec->set_binary_hash(info.hash);
    subprog_spec->set_size(info.file_stat.size);
  }
}

void CompileTask::MayUpdateSubprogramSpec() {
  CHECK_EQ(SETUP, state_);
  MayFixSubprogramSpec(req_->mutable_subprogram());
  if (VLOG_IS_ON(3)) {
    for (const auto& subprog_spec : req_->subprogram()) {
      LOG(INFO) << trace_id_ << " update subprogram spec:"
                << " path=" << subprog_spec.path()
                << " hash=" << subprog_spec.binary_hash();
    }
  }
}

struct CompileTask::IncludeProcessorRequestParam {
  // input file_stat_cache will be moved to temporarily.
  std::unique_ptr<FileStatCache> file_stat_cache;
};

struct CompileTask::IncludeProcessorResponseParam {
  // result of IncludeProcessor.
  CompilerTypeSpecific::IncludeProcessorResult result;
  // return borrowed file_stat_cache to CompileTask.
  std::unique_ptr<FileStatCache> file_stat_cache;
  // true if include processor was canceled.
  bool canceled = false;
};

void CompileTask::StartIncludeProcessor() {
  VLOG(1) << "StartIncludeProcessor";
  CHECK_EQ(SETUP, state_);

  // TODO: DepsCache should be able to support multiple input files,
  // however currently we have to pass |abs_input_filename|, so DeosCache
  // supports a compile task that has one input file.
  if (DepsCache::IsEnabled() && compiler_type_specific_->SupportsDepsCache() &&
      flags_->input_filenames().size() == 1U) {
    const string& input_filename = flags_->input_filenames()[0];
    const string& abs_input_filename =
        file::JoinPathRespectAbsolute(flags_->cwd(), input_filename);

    DepsCache* dc = DepsCache::instance();
    deps_identifier_ = DepsCache::MakeDepsIdentifier(
        compiler_info_state_.get()->info(), *flags_);
    if (deps_identifier_.has_value() &&
        dc->GetDependencies(deps_identifier_, flags_->cwd(), abs_input_filename,
                            &required_files_, input_file_stat_cache_.get())) {
      LOG(INFO) << trace_id_ << " use deps cache. required_files="
                << required_files_.size();
      depscache_used_ = true;
      UpdateRequiredFilesDone(true);
      return;
    }
  }

  auto request_param = absl::make_unique<IncludeProcessorRequestParam>();

  input_file_stat_cache_->ReleaseOwner();
  request_param->file_stat_cache = std::move(input_file_stat_cache_);

  OneshotClosure* closure = NewCallback(this, &CompileTask::RunIncludeProcessor,
                                        std::move(request_param));
  service_->wm()->RunClosureInPool(
      FROM_HERE, service_->include_processor_pool(),
      closure,
      WorkerThread::PRIORITY_LOW);
}

void CompileTask::RunIncludeProcessor(
    std::unique_ptr<IncludeProcessorRequestParam> request_param) {
  VLOG(1) << "RunIncludeProcessor";
  DCHECK(compiler_info_state_.get() != nullptr);

  // Pass ownership temporary to IncludeProcessor thread.
  request_param->file_stat_cache->AcquireOwner();

  const absl::Duration include_processor_wait_time =
      include_wait_timer_.GetDuration();
  stats_->set_include_processor_wait_time(
      DurationToIntMs(include_processor_wait_time));
  stats_->include_processor_wait_time = include_processor_wait_time;

  if (canceled_ || abort_) {
    LOG(INFO) << trace_id_
              << " won't run include processor because result won't be used."
              << " canceled=" << canceled_
              << " abort=" << abort_;
    auto response_param = absl::make_unique<IncludeProcessorResponseParam>();
    response_param->canceled = true;
    response_param->file_stat_cache = std::move(request_param->file_stat_cache);
    response_param->file_stat_cache->ReleaseOwner();
    service_->wm()->RunClosureInThread(
        FROM_HERE, thread_id_,
        NewCallback(this, &CompileTask::RunIncludeProcessorDone,
                    std::move(response_param)),
        WorkerThread::PRIORITY_LOW);
    return;
  }

  LOG_IF(WARNING, stats_->include_processor_wait_time > absl::Seconds(1))
      << trace_id_ << " SLOW start IncludeProcessor"
      << " in " << stats_->include_processor_wait_time;

  SimpleTimer include_timer(SimpleTimer::START);
  CompilerTypeSpecific::IncludeProcessorResult result =
      compiler_type_specific_->RunIncludeProcessor(
          trace_id_, *flags_, compiler_info_state_.get()->info(),
          req_->command_spec(), request_param->file_stat_cache.get());
  const absl::Duration include_processor_run_time = include_timer.GetDuration();
  stats_->set_include_processor_run_time(
      DurationToIntMs(include_processor_run_time));
  stats_->include_processor_run_time = include_processor_run_time;

  auto response_param = absl::make_unique<IncludeProcessorResponseParam>();
  response_param->result = std::move(result);
  response_param->file_stat_cache = std::move(request_param->file_stat_cache);
  response_param->file_stat_cache->ReleaseOwner();

  service_->wm()->RunClosureInThread(
      FROM_HERE, thread_id_,
      NewCallback(this, &CompileTask::RunIncludeProcessorDone,
                  std::move(response_param)),
      WorkerThread::PRIORITY_LOW);
}

void CompileTask::RunIncludeProcessorDone(
    std::unique_ptr<IncludeProcessorResponseParam> response_param) {
  VLOG(1) << "RunIncludeProcessorDone";
  DCHECK(BelongsToCurrentThread());
  DCHECK(response_param->file_stat_cache.get() != nullptr);

  input_file_stat_cache_ = std::move(response_param->file_stat_cache);
  input_file_stat_cache_->AcquireOwner();
  if (response_param->canceled) {
    UpdateRequiredFilesDone(false);
    return;
  }
  required_files_ = std::move(response_param->result.required_files);

  if (!response_param->result.system_library_paths.empty()) {
    system_library_paths_ =
        std::move(response_param->result.system_library_paths);
    FixSystemLibraryPath(system_library_paths_, req_->mutable_command_spec());
  }

  if (response_param->result.total_files) {
    stats_->set_include_preprocess_total_files(
        *response_param->result.total_files);
  }
  if (response_param->result.skipped_files) {
    stats_->set_include_preprocess_skipped_files(
        *response_param->result.skipped_files);
  }

  if (!response_param->result.ok) {
    LOG(WARNING) << trace_id_ << "include processor failed"
                 << " error=" << response_param->result.error_reason
                 << " flags=" << flags_->DebugString();
    if (response_param->result.error_to_user) {
      AddErrorToResponse(TO_USER, response_param->result.error_reason, true);
    }
  }

  // When deps_identifier_.has_value() is true, the condition to use DepsCache
  // should be satisfied. However, several checks are done for the safe.
  if (DepsCache::IsEnabled() && compiler_type_specific_->SupportsDepsCache() &&
      response_param->result.ok && deps_identifier_.has_value() &&
      flags_->input_filenames().size() == 1U) {
    const string& input_filename = flags_->input_filenames()[0];
    const string& abs_input_filename =
        file::JoinPathRespectAbsolute(flags_->cwd(), input_filename);

    DepsCache* dc = DepsCache::instance();
    if (!dc->SetDependencies(deps_identifier_, flags_->cwd(),
                             abs_input_filename, required_files_,
                             input_file_stat_cache_.get())) {
      LOG(INFO) << trace_id_ << " failed to save dependencies.";
    }
  }

  UpdateRequiredFilesDone(response_param->result.ok);
}

// ----------------------------------------------------------------
// state_: FILE_REQ.
void CompileTask::SetInputFileCallback() {
  CHECK(BelongsToCurrentThread());
  CHECK_EQ(FILE_REQ, state_);
  CHECK(!input_file_callback_);
  input_file_callback_ = NewCallback(
      this, &CompileTask::ProcessFileRequestDone);
  num_input_file_task_ = 0;
  input_file_success_ = true;
}

void CompileTask::StartInputFileTask() {
  CHECK(BelongsToCurrentThread());
  CHECK_EQ(FILE_REQ, state_);
  ++num_input_file_task_;
}

void CompileTask::InputFileTaskFinished(InputFileTask* input_file_task) {
  CHECK(BelongsToCurrentThread());
  CHECK_EQ(FILE_REQ, state_);

  if (abort_) {
    VLOG(1) << trace_id_ << "aborted ";
    input_file_success_ = false;
    input_file_task->Done(this);
    return;
  }

  const string& filename = input_file_task->filename();
  const string& hash_key = input_file_task->hash_key();
  const ssize_t file_size = input_file_task->file_size();
  const absl::optional<absl::Time>& mtime = input_file_task->mtime();
  VLOG(1) << trace_id_ << " input done:" << filename;
  if (mtime.has_value() &&
      *mtime > absl::FromTimeT(stats_->latest_input_mtime())) {
    stats_->set_latest_input_filename(filename);
    stats_->set_latest_input_mtime(absl::ToTimeT(*mtime));
  }
  if (!input_file_task->success()) {
    AddErrorToResponse(TO_LOG, "Create file blob failed for:" + filename, true);
    input_file_success_ = false;
    input_file_task->Done(this);
    return;
  }
  DCHECK(!hash_key.empty()) << filename;
  stats_->add_input_file_time(
      DurationToIntMs(input_file_task->timer().GetDuration()));
  stats_->add_input_file_size(file_size);
  if (!input_file_task->UpdateInputInTask(this)) {
    LOG(ERROR) << trace_id_ << " bad input data "
               << filename;
    input_file_success_ = false;
  }
  const HttpClient::Status& http_status =
      input_file_task->http_status();
  stats_->input_file_rpc_size += http_status.req_size;
  stats_->input_file_rpc_raw_size += http_status.raw_req_size;
  input_file_task->Done(this);
}

void CompileTask::MaybeRunInputFileCallback(bool task_finished) {
  CHECK(BelongsToCurrentThread());
  CHECK_EQ(FILE_REQ, state_);
  OneshotClosure* closure = nullptr;
  if (task_finished) {
    --num_input_file_task_;
    VLOG(1) << trace_id_ << " input remain=" << num_input_file_task_;
    if (num_input_file_task_ > 0)
      return;
  }
  CHECK_EQ(0, num_input_file_task_);
  if (input_file_callback_) {
    closure = input_file_callback_;
    input_file_callback_ = nullptr;
  }
  if (closure)
    closure->Run();
}

// ----------------------------------------------------------------
// state_: CALL_EXEC.

void CompileTask::CheckCommandSpec() {
  CHECK_EQ(CALL_EXEC, state_);
  if (!resp_->result().has_command_spec()) {
    return;
  }

  // Checks all mismatches first, then decide behavior later.
  bool is_name_mismatch = false;
  bool is_target_mismatch = false;
  bool is_binary_hash_mismatch = false;
  bool is_version_mismatch = false;
  bool is_subprograms_mismatch = false;
  const CommandSpec& req_command_spec = req_->command_spec();
  const CommandSpec& resp_command_spec = resp_->result().command_spec();
  const string message_on_mismatch(
      "local:" + CreateCommandVersionString(req_command_spec) +
      " but remote:" +
      CreateCommandVersionString(resp_command_spec));
  if (req_command_spec.name() != resp_command_spec.name()) {
    is_name_mismatch = true;
    std::ostringstream ss;
    ss << trace_id_ << " compiler name mismatch:"
       << " local:" << req_command_spec.name()
       << " remote:" << resp_command_spec.name();
    AddErrorToResponse(TO_LOG, ss.str(), false);
    stats_->set_exec_command_name_mismatch(message_on_mismatch);
  }
  if (req_command_spec.target() != resp_command_spec.target()) {
    is_target_mismatch = true;
    std::ostringstream ss;
    ss << trace_id_ << " compiler target mismatch:"
       << " local:" << req_command_spec.target()
       << " remote:" << resp_command_spec.target();
    AddErrorToResponse(TO_LOG, ss.str(), false);
    stats_->set_exec_command_target_mismatch(message_on_mismatch);
  }
  if (req_command_spec.binary_hash() != resp_command_spec.binary_hash()) {
    is_binary_hash_mismatch = true;
    LOG(WARNING) << trace_id_ << " compiler binary hash mismatch:"
                 << " local:" << req_command_spec.binary_hash()
                 << " remote:" << resp_command_spec.binary_hash();
    stats_->set_exec_command_binary_hash_mismatch(message_on_mismatch);
  }
  if (req_command_spec.version() != resp_command_spec.version()) {
    is_version_mismatch = true;
    LOG(WARNING) << trace_id_ << " compiler version mismatch:"
                 << " local:" << req_command_spec.version()
                 << " remote:" << resp_command_spec.version();
    stats_->set_exec_command_version_mismatch(message_on_mismatch);
  }
  if (!IsSameSubprograms(*req_, *resp_)) {
    is_subprograms_mismatch = true;
    std::ostringstream local_subprograms;
    DumpSubprograms(req_->subprogram(), &local_subprograms);
    std::ostringstream remote_subprograms;
    DumpSubprograms(resp_->result().subprogram(), &remote_subprograms);
    LOG(WARNING) << trace_id_ << " compiler subprograms mismatch:"
                 << " local:" << local_subprograms.str()
                 << " remote:" << remote_subprograms.str();
    std::ostringstream ss;
    ss << "local:" << CreateCommandVersionString(req_command_spec)
       << " subprogram:" << local_subprograms.str()
       << " but remote:" << CreateCommandVersionString(resp_command_spec)
       << " subprogram:" << remote_subprograms.str();
    stats_->set_exec_command_subprograms_mismatch(ss.str());
  }

  if (service_->hermetic()) {
    bool mismatch = false;
    // Check if remote used the same command spec.
    if (is_name_mismatch) {
      mismatch = true;
      AddErrorToResponse(TO_USER, "compiler name mismatch", true);
    }
    if (is_target_mismatch) {
      mismatch = true;
      AddErrorToResponse(TO_USER, "compiler target mismatch", true);
    }
    if (is_binary_hash_mismatch) {
      mismatch = true;
      AddErrorToResponse(TO_USER, "compiler binary hash mismatch", true);
    }
    if (is_version_mismatch) {
      AddErrorToResponse(TO_USER, "compiler version mismatch", true);
      mismatch = true;
    }
    if (is_subprograms_mismatch) {
      AddErrorToResponse(TO_USER, "subprograms mismatch", true);
      mismatch = true;
    }
    if (mismatch) {
      if (service_->DisableCompilerInfo(compiler_info_state_.get(),
                                        "hermetic mismatch")) {
        AddErrorToResponse(
            TO_USER,
            req_->command_spec().local_compiler_path() + " is disabled.",
            true);
      }
      want_fallback_ = service_->hermetic_fallback();
      if (want_fallback_ != requester_env_.fallback()) {
        LOG(INFO) << trace_id_ << " hermetic mismatch: fallback changed from "
                  << requester_env_.fallback()
                  << " to " << want_fallback_;
      }
    }
    return;
  }

  if (is_name_mismatch || is_target_mismatch) {
    AddErrorToResponse(TO_USER, "compiler name or target mismatch", true);
    if (service_->DisableCompilerInfo(compiler_info_state_.get(),
                                      "compiler name or target mismatch")) {
      AddErrorToResponse(
          TO_USER,
          req_->command_spec().local_compiler_path() + " is disabled.",
          true);
    }
    return;
  }
  // TODO: drop command_check_level support in the future.
  //                    GOMA_HERMETIC should be recommended.
  if (is_binary_hash_mismatch) {
    string error_message;
    bool set_error = false;
    if (service_->RecordCommandSpecBinaryHashMismatch(
            stats_->exec_command_binary_hash_mismatch())) {
      error_message = "compiler binary hash mismatch: " +
          stats_->exec_command_binary_hash_mismatch();
    }
    if (service_->command_check_level() == "checksum") {
      set_error = true;
    }
    if (!requester_env_.verify_command().empty()) {
      if (requester_env_.verify_command() == "checksum" ||
          requester_env_.verify_command() == "all") {
        AddErrorToResponse(TO_LOG, "", true);
        resp_->mutable_result()->set_stderr_buffer(
            "compiler binary hash mismatch: " +
            stats_->exec_command_binary_hash_mismatch() + "\n" +
            resp_->mutable_result()->stderr_buffer());
      }
      // ignore when other verify command mode.
    } else if (!error_message.empty()) {
      error_message =
          (set_error ? "Error: " : "Warning: ") + error_message;
      AddErrorToResponse(TO_USER, error_message, set_error);
    }
  }
  if (is_version_mismatch) {
    string error_message;
    bool set_error = false;
    if (service_->RecordCommandSpecVersionMismatch(
            stats_->exec_command_version_mismatch())) {
      error_message = "compiler version mismatch: " +
                      stats_->exec_command_version_mismatch();
    }
    if (service_->command_check_level() == "version") {
      set_error = true;
    }
    if (!requester_env_.verify_command().empty()) {
      if (requester_env_.verify_command() == "version" ||
          requester_env_.verify_command() == "all") {
        AddErrorToResponse(TO_LOG, "", true);
        resp_->mutable_result()->set_stderr_buffer(
            "compiler version mismatch: " +
            stats_->exec_command_version_mismatch() + "\n" +
            resp_->mutable_result()->stderr_buffer());
      }
      // ignore when other verify command mode.
    } else if (!error_message.empty()) {
      error_message =
          (set_error ? "Error: " : "Warning: ") + error_message;
      AddErrorToResponse(TO_USER, error_message, set_error);
    }
  }
  if (is_subprograms_mismatch) {
    std::ostringstream error_message;
    bool set_error = false;

    std::set<string> remote_hashes;
    for (const auto& subprog : resp_->result().subprogram()) {
      remote_hashes.insert(subprog.binary_hash());
    }
    for (const auto& subprog : req_->subprogram()) {
      if (remote_hashes.find(subprog.binary_hash()) != remote_hashes.end()) {
        continue;
      }
      std::ostringstream ss;
      ss << subprog.path() << " " << subprog.binary_hash();
      if (service_->RecordSubprogramMismatch(ss.str())) {
        if (!error_message.str().empty()) {
          error_message << std::endl;
        }
        error_message << "subprogram mismatch: "
                      << ss.str();
      }
    }

    if (service_->command_check_level() == "checksum") {
      set_error = true;
    }
    if (!requester_env_.verify_command().empty()) {
      if (requester_env_.verify_command() == "checksum" ||
          requester_env_.verify_command() == "all") {
        AddErrorToResponse(TO_LOG, "", true);
        resp_->mutable_result()->set_stderr_buffer(
            error_message.str() + "\n" +
            resp_->mutable_result()->stderr_buffer());
      }
      // ignore when other verify command mode.
    } else if (!error_message.str().empty()) {
      AddErrorToResponse(
          TO_USER,
          (set_error ? "Error: " : "Warning: ") + error_message.str(),
          set_error);
    }
  }
}

void CompileTask::CheckNoMatchingCommandSpec(const string& retry_reason) {
  CHECK_EQ(CALL_EXEC, state_);

  // If ExecResult does not have CommandSpec, goma backend did not try
  // to find the compiler. No need to check mismatches.
  if (!resp_->result().has_command_spec()) {
    return;
  }

  bool is_compiler_missing = false;
  bool is_subprogram_missing = false;
  // If ExecResult has incomplete CommandSpec, it means that goma backend
  // tried to select a matching compiler but failed.
  if (!resp_->result().command_spec().has_binary_hash()) {
    is_compiler_missing = true;
  }
  if (!IsSameSubprograms(*req_, *resp_)) {
    is_subprogram_missing = true;
  }
  // Nothing is missing.
  if (!is_compiler_missing && !is_subprogram_missing) {
    return;
  }

  std::ostringstream local_subprograms;
  std::ostringstream remote_subprograms;
  DumpSubprograms(req_->subprogram(), &local_subprograms);
  DumpSubprograms(resp_->result().subprogram(), &remote_subprograms);

  std::ostringstream what_missing;
  if (is_compiler_missing) {
    LOG(WARNING) << trace_id_
                 << " compiler not found:"
                 << " local: "
                 << CreateCommandVersionString(req_->command_spec())
                 << " remote: none";
    what_missing << "compiler("
                 << CreateCommandVersionString(req_->command_spec())
                 << ")";
  }
  if (is_subprogram_missing) {
    LOG(WARNING) << trace_id_
                 << " subprogram not found:"
                 << " local: " << local_subprograms.str()
                 << " remote: " << remote_subprograms.str();
    if (!what_missing.str().empty())
      what_missing << "/";
    what_missing << "subprograms("
                 << local_subprograms.str()
                 << ")";
  }

  std::ostringstream ss;
  ss << "local: " << CreateCommandVersionString(req_->command_spec())
     << " subprogram: " << local_subprograms.str()
     << " but remote: ";
  if (is_compiler_missing) {
    ss << "none";
  } else {
    ss << CreateCommandVersionString(resp_->result().command_spec());
  }
  ss << " subprogram: " << remote_subprograms.str();
  stats_->set_exec_command_not_found(ss.str());

  if (service_->hermetic() && !what_missing.str().empty()) {
    std::ostringstream msg;
    msg << "No matching " << what_missing.str() << " found in server";
    AddErrorToResponse(TO_USER, msg.str(), true);
    if (is_compiler_missing &&
        service_->DisableCompilerInfo(compiler_info_state_.get(),
                                      "no matching compiler found in server")) {
        AddErrorToResponse(
            TO_USER, req_->command_spec().local_compiler_path() +
            " is disabled.",
            true);
    }

    want_fallback_ = service_->hermetic_fallback();
    if (want_fallback_ != requester_env_.fallback()) {
      LOG(INFO) << trace_id_
                << " hermetic miss "
                << what_missing.str()
                << ": fallback changed from "
                << requester_env_.fallback()
                << " to " << want_fallback_;
    }
  }
}

void CompileTask::StoreEmbeddedUploadInformationIfNeeded() {
  // We save embedded upload information only if missing input size is 0.
  // Let's consider the situation we're using cluster A and cluster B.
  // When we send a compile request to cluster A, cluster A might report
  // there are missing inputs. Then we retry to send a compile request.
  // However, we might send it to another cluster B. Then cluster B might
  // report missing input error again.
  // So, we would like to save the embedded upload information only if
  // missing input error did not happen.
  // TODO: This can reduce the number of input file missing, it would
  // still exist. After uploading a file to cluster B was succeeded, we might
  // send another compile request to cluster A. When cluster A does not have
  // the file cache, missing inputs error will occur.

  if (resp_->missing_input_size() > 0)
    return;

  // TODO: What time should we use here?
  const absl::Time upload_timestamp_ms = absl::Now();

  for (const auto& input : req_->input()) {
    // If content does not exist, it's not embedded upload.
    if (!input.has_content())
      continue;
    const std::string& abs_filename = file::JoinPathRespectAbsolute(
        flags_->cwd(), input.filename());
    bool new_cache_key = service_->file_hash_cache()->StoreFileCacheKey(
        abs_filename, input.hash_key(), upload_timestamp_ms,
        input_file_stat_cache_->Get(abs_filename));
    VLOG(1) << trace_id_
            << " store file cache key for embedded upload: "
            << abs_filename
            << " : is new cache key? = " << new_cache_key;
  }
}

// ----------------------------------------------------------------
// state_: FILE_RESP.
void CompileTask::SetOutputFileCallback() {
  CHECK(BelongsToCurrentThread());
  CHECK_EQ(FILE_RESP, state_);
  CHECK(!output_file_callback_);
  output_file_callback_ = NewCallback(
      this, &CompileTask::ProcessFileResponseDone);
  num_output_file_task_ = 0;
  output_file_success_ = true;
}

void CompileTask::CheckOutputFilename(const string& filename) {
  CHECK_EQ(FILE_RESP, state_);
  if (filename[0] == '/') {
    if (HasPrefixDir(filename, service_->tmp_dir()) ||
        HasPrefixDir(filename, "/var")) {
      VLOG(1) << "Output to temp directory:" << filename;
    } else if (service_->use_relative_paths_in_argv()) {
      // If FLAGS_USE_RELATIVE_PATHS_IN_ARGV is false, output path may be
      // absolute path specified by -o or so.

      Json::Value json;
      DumpToJson(true, &json);
      LOG(ERROR) << trace_id_ << " " << json;
      LOG(FATAL) << "Absolute output filename:"
                 << filename;
    }
  }
}

void CompileTask::StartOutputFileTask() {
  CHECK(BelongsToCurrentThread());
  CHECK_EQ(FILE_RESP, state_);
  ++num_output_file_task_;
}

void CompileTask::OutputFileTaskFinished(
    std::unique_ptr<OutputFileTask> output_file_task) {
  CHECK(BelongsToCurrentThread());
  CHECK_EQ(FILE_RESP, state_);

  DCHECK_EQ(this, output_file_task->task());
  const ExecResult_Output& output = output_file_task->output();
  const string& filename = output.filename();

  if (abort_) {
    output_file_success_ = false;
    return;
  }
  if (!output_file_task->success()) {
    AddErrorToResponse(TO_LOG,
                       "Failed to write file blob:" + filename + " (" +
                       (cache_hit() ? "cached" : "no-cached") + ")",
                       true);
    output_file_success_ = false;

    // If it fails to write file, goma has ExecResult in cache but might
    // lost output file.  It would be better to retry with STORE_ONLY
    // to recreate output file and store it in cache.
    ExecReq::CachePolicy cache_policy = req_->cache_policy();
    if (cache_policy == ExecReq::LOOKUP_AND_STORE ||
        cache_policy == ExecReq::LOOKUP_AND_STORE_SUCCESS) {
      LOG(WARNING) << trace_id_
                   << " will retry with STORE_ONLY";
      req_->set_cache_policy(ExecReq::STORE_ONLY);
    }
    return;
  }
  absl::Duration output_file_time = output_file_task->timer().GetDuration();
  LOG_IF(WARNING, output_file_time > absl::Minutes(1))
      << trace_id_ << " SLOW output file:"
      << " filename=" << filename
      << " http_rpc=" << output_file_task->http_status().DebugString()
      << " num_rpc=" << output_file_task->num_rpc()
      << " in_memory=" << output_file_task->IsInMemory() << " in "
      << output_file_time;
  stats_->add_output_file_time(DurationToIntMs(output_file_time));
  LOG_IF(WARNING,
         output.blob().blob_type() != FileBlob::FILE &&
         output.blob().blob_type() != FileBlob::FILE_META)
      << "Invalid blob type: " << output.blob().blob_type();
  stats_->add_output_file_size(output.blob().file_size());
  stats_->output_file_rpc += output_file_task->num_rpc();
  const HttpClient::Status& http_status = output_file_task->http_status();
  stats_->add_chunk_resp_size(http_status.resp_size);
  stats_->output_file_rpc_req_build_time += http_status.req_build_time;
  stats_->output_file_rpc_req_send_time += http_status.req_send_time;
  stats_->output_file_rpc_wait_time += http_status.wait_time;
  stats_->output_file_rpc_resp_recv_time += http_status.resp_recv_time;
  stats_->output_file_rpc_resp_parse_time += http_status.resp_parse_time;
  stats_->output_file_rpc_size += http_status.resp_size;
  stats_->output_file_rpc_raw_size += http_status.raw_resp_size;
}

void CompileTask::MaybeRunOutputFileCallback(int index, bool task_finished) {
  CHECK(BelongsToCurrentThread());
  CHECK_EQ(FILE_RESP, state_);
  OneshotClosure* closure = nullptr;
  if (task_finished) {
    DCHECK_NE(-1, index);
    // Once output.blob has been written on disk, we don't need it
    // any more.
    resp_->mutable_result()->mutable_output(index)->clear_blob();
    --num_output_file_task_;
    if (num_output_file_task_ > 0)
      return;
  } else {
    CHECK_EQ(-1, index);
  }
  CHECK_EQ(0, num_output_file_task_);
  if (output_file_callback_) {
    closure = output_file_callback_;
    output_file_callback_ = nullptr;
  }
  if (closure)
    closure->Run();
}

bool CompileTask::VerifyOutput(
    const string& local_output_path,
    const string& goma_output_path) {
  CHECK_EQ(FILE_RESP, state_);
  LOG(INFO) << "Verify Output: "
            << " local:" << local_output_path
            << " goma:" << goma_output_path;
  std::ostringstream error_message;
  static const int kSize = 1024;
  char local_buf[kSize];
  char goma_buf[kSize];
  ScopedFd local_fd(ScopedFd::OpenForRead(local_output_path));
  if (!local_fd.valid()) {
    error_message << "Not found: local file:" << local_output_path;
    AddErrorToResponse(TO_USER, error_message.str(), true);
    return false;
  }
  ScopedFd goma_fd(ScopedFd::OpenForRead(goma_output_path));
  if (!goma_fd.valid()) {
    error_message << "Not found: goma file:" << goma_output_path;
    AddErrorToResponse(TO_USER, error_message.str(), true);
    return false;
  }
  int local_len;
  int goma_len;
  for (size_t len = 0; ; len += local_len) {
    local_len = local_fd.Read(local_buf, kSize);
    if (local_len < 0) {
      error_message << "read error local:" << local_output_path
                    << " @" << len << " " << GetLastErrorMessage();
      AddErrorToResponse(TO_USER, error_message.str(), true);
      return false;
    }
    goma_len = goma_fd.Read(goma_buf, kSize);
    if (goma_len < 0) {
      error_message << "read error goma:" << goma_output_path
                    << " @" << len << " " << GetLastErrorMessage();
      AddErrorToResponse(TO_USER, error_message.str(), true);
      return false;
    }
    if (local_len != goma_len) {
      error_message << "read len: " << local_len << "!=" << goma_len
                    << " " << local_output_path << " @" << len;
      AddErrorToResponse(TO_USER, error_message.str(), true);
      return false;
    }
    if (local_len == 0) {
      LOG(INFO) << trace_id_
                << " Verify OK: " << local_output_path
                << " size=" << len;
      return true;
    }
    if (memcmp(local_buf, goma_buf, local_len) != 0) {
      error_message << "output mismatch: "
                    << " local:" << local_output_path
                    << " goma:" << goma_output_path
                    << " @[" << len << "," <<  local_len << ")";
      AddErrorToResponse(TO_USER, error_message.str(), true);
      return false;
    }
    VLOG(2) << "len:" << len << "+" << local_len;
  }
  return true;
}

void CompileTask::ClearOutputFile() {
  for (auto& iter : output_file_) {
    if (!iter.content.empty()) {
      LOG(INFO) << trace_id_ << " clear output, but content is not empty";
      service_->ReleaseOutputBuffer(iter.size, &iter.content);
      continue;
    }
    // Remove if we wrote tmp file for the output.
    // Don't remove filename, which is the actual output filename,
    // and local run might have output to the file.
    const string& filename = iter.filename;
    const string& tmp_filename = iter.tmp_filename;
    if (!tmp_filename.empty() && tmp_filename != filename) {
      remove(tmp_filename.c_str());
    }
  }
  output_file_.clear();
}

// ----------------------------------------------------------------
// local run finished.
void CompileTask::SetLocalOutputFileCallback() {
  CHECK(BelongsToCurrentThread());
  CHECK(!local_output_file_callback_);
  local_output_file_callback_ = NewCallback(
      this, &CompileTask::ProcessLocalFileOutputDone);
  num_local_output_file_task_ = 0;
}

void CompileTask::StartLocalOutputFileTask() {
  CHECK(BelongsToCurrentThread());
  ++num_local_output_file_task_;
}

void CompileTask::LocalOutputFileTaskFinished(
    std::unique_ptr<LocalOutputFileTask> local_output_file_task) {
  CHECK(BelongsToCurrentThread());

  DCHECK_EQ(this, local_output_file_task->task());
  const string& filename = local_output_file_task->filename();
  if (!local_output_file_task->success()) {
    LOG(WARNING) << trace_id_
                 << " Create file blob failed for local output:" << filename;
    return;
  }
  const absl::Duration local_output_file_task_duration =
      local_output_file_task->timer().GetDuration();
  stats_->add_local_output_file_time(
      DurationToIntMs(local_output_file_task_duration));
  stats_->total_local_output_file_time += local_output_file_task_duration;

  const FileStat& file_stat = local_output_file_task->file_stat();
  stats_->add_local_output_file_size(file_stat.size);
}

void CompileTask::MaybeRunLocalOutputFileCallback(bool task_finished) {
  CHECK(BelongsToCurrentThread());
  OneshotClosure* closure = nullptr;
  if (task_finished) {
    --num_local_output_file_task_;
    if (num_local_output_file_task_ > 0)
      return;
  }
  CHECK_EQ(0, num_local_output_file_task_);
  if (local_output_file_callback_) {
    closure = local_output_file_callback_;
    local_output_file_callback_ = nullptr;
  }
  if (closure)
    closure->Run();
}

// ----------------------------------------------------------------
// state_: FINISHED/LOCAL_FINISHED or abort_
void CompileTask::UpdateStats() {
  CHECK(state_ >= FINISHED || abort_);

  resp_->set_compiler_proxy_time(DurationToIntMs(handler_timer_.GetDuration()));

  stats_->StoreStatsInExecResp(resp_.get());

  // TODO: similar logic found in CompileService::CompileTaskDone, so
  // it would be better to be merged.  Note that ExecResp are not available
  // in CompileService::CompileTaskDone.
  switch (state_) {
    case FINISHED:
      resp_->set_compiler_proxy_goma_finished(true);
      if (cache_hit())
        resp_->set_compiler_proxy_goma_cache_hit(true);
      break;
    case LOCAL_FINISHED:
      resp_->set_compiler_proxy_local_finished(true);
      break;
    default:
      resp_->set_compiler_proxy_goma_aborted(true);
      break;
  }
  if (local_run_)
    resp_->set_compiler_proxy_local_run(true);
  if (local_killed_)
    resp_->set_compiler_proxy_local_killed(true);
}

void CompileTask::SaveInfoFromInputOutput() {
  DCHECK(BelongsToCurrentThread());
  CHECK(state_ >= FINISHED || abort_);
  CHECK(req_.get());
  CHECK(resp_.get());
  CHECK(!exec_resp_.get());

  if (failed() || fail_fallback_) {
    if (!fail_fallback_) {
      // if fail fallback, we already stored remote outputs in stdout_ and
      // stderr_, and resp_ becomes local process output.
      stdout_ = resp_->result().stdout_buffer();
      stderr_ = resp_->result().stderr_buffer();
    }
  }
  // arg, env and expanded_arg are used for dumping ExecReq.
  // We should keep what we actually used instead of what came from gomacc.
  *stats_->mutable_arg() = std::move(*req_->mutable_arg());
  *stats_->mutable_env() = std::move(*req_->mutable_env());
  *stats_->mutable_expanded_arg() = std::move(*req_->mutable_expanded_arg());
  req_.reset();
  resp_.reset();
  flags_.reset();
  input_file_stat_cache_.reset();
  output_file_stat_cache_.reset();
}

// ----------------------------------------------------------------
// subprocess handling.
void CompileTask::SetupSubProcess() {
  VLOG(1) << trace_id_ << " SetupSubProcess "
          << SubProcessReq::Weight_Name(subproc_weight_);
  CHECK(BelongsToCurrentThread());
  CHECK(subproc_ == nullptr) << trace_id_ << " " << StateName(state_)
                          << " pid=" << subproc_->started().pid()
                          << stats_->local_run_reason();
  CHECK(!req_->command_spec().local_compiler_path().empty())
      << req_->DebugString();
  if (delayed_setup_subproc_ != nullptr) {
    delayed_setup_subproc_->Cancel();
    delayed_setup_subproc_ = nullptr;
  }

  std::vector<const char*> argv;
  argv.push_back(req_->command_spec().local_compiler_path().c_str());
  for (int i = 1; i < stats_->arg_size(); ++i) {
    argv.push_back(stats_->arg(i).c_str());
  }
  argv.push_back(nullptr);

  subproc_ = new SubProcessTask(
      trace_id_,
      req_->command_spec().local_compiler_path().c_str(),
      const_cast<char**>(&argv[0]));
  SubProcessReq* req = subproc_->mutable_req();
  req->set_cwd(req_->cwd());
  if (requester_env_.has_umask()) {
    req->set_umask(requester_env_.umask());
  }
  if (flags_->type() == CompilerFlagType::Gcc) {
    const GCCFlags& gcc_flag = static_cast<const GCCFlags&>(*flags_);
    if (gcc_flag.is_stdin_input()) {
      CHECK_GE(req_->input_size(), 1) << req_->DebugString();
      req->set_stdin_filename(req_->input(0).filename());
    }
  } else if (flags_->type() == CompilerFlagType::Clexe) {
    // TODO: handle input is stdin case for VC++?
  }
  {
    std::ostringstream filenamebuf;
    filenamebuf << "gomacc." << id_ << ".out";
    subproc_stdout_ = file::JoinPath(service_->tmp_dir(), filenamebuf.str());
    req->set_stdout_filename(subproc_stdout_);
  }
  {
    std::ostringstream filenamebuf;
    filenamebuf << "gomacc." << id_ << ".err";
    subproc_stderr_ = file::JoinPath(service_->tmp_dir(), filenamebuf.str());
    req->set_stderr_filename(subproc_stderr_);
  }
  for (const auto& env : stats_->env()) {
    req->add_env(env);
  }
  if (local_path_.empty()) {
    LOG(WARNING) << "Empty PATH: " << req_->DebugString();
  } else {
    req->add_env("PATH=" + local_path_);
  }
#ifdef _WIN32
  req->add_env("TMP=" + service_->tmp_dir());
  req->add_env("TEMP=" + service_->tmp_dir());
  if (pathext_.empty()) {
    LOG(WARNING) << "Empty PATHEXT: " << req_->DebugString();
  } else {
    req->add_env("PATHEXT=" + pathext_);
  }
#endif

  req->set_weight(subproc_weight_);
  subproc_->Start(
      NewCallback(
          this,
          &CompileTask::FinishSubProcess));
}

void CompileTask::RunSubProcess(const string& reason) {
  VLOG(1) << trace_id_ << " RunSubProcess " << reason;
  CHECK(!abort_);
  if (subproc_ == nullptr) {
    LOG(WARNING) << trace_id_ << " subproc already finished.";
    return;
  }
  stats_->set_local_run_reason(reason);
  subproc_->RequestRun();
  VLOG(1) << "Run " << reason << " " << subproc_->req().DebugString();
}

void CompileTask::KillSubProcess() {
  // TODO: support the case subprocess is killed by FAIL_FAST.
  VLOG(1) << trace_id_ << " KillSubProcess";
  CHECK(subproc_ != nullptr);
  SubProcessState::State state = subproc_->state();
  local_killed_ = subproc_->Kill();  // Will call FinishSubProcess().
  VLOG(1) << trace_id_ << " kill pid=" << subproc_->started().pid()
          << " " << local_killed_
          << " " << SubProcessState::State_Name(state)
          << "->" << SubProcessState::State_Name(subproc_->state());
  if (local_killed_) {
    if (service_->dont_kill_subprocess()) {
      stats_->set_local_run_reason("fast goma, but wait for local.");
    } else {
      stats_->set_local_run_reason("killed by fast goma");
    }
  } else if (subproc_->started().pid() != SubProcessState::kInvalidPid) {
    // subproc was signaled but not waited yet.
    stats_->set_local_run_reason("fast goma, local signaled");
  } else {
    // subproc was initialized, but not yet started.
    stats_->set_local_run_reason("fast goma, local not started");
  }
}

void CompileTask::FinishSubProcess() {
  VLOG(1) << trace_id_ << " FinishSubProcess";
  CHECK(BelongsToCurrentThread());
  CHECK(!abort_);
  SubProcessTask* subproc = nullptr;
  {
    AUTOLOCK(lock, &mu_);
    subproc = subproc_;
    subproc_ = nullptr;
  }
  CHECK(subproc);

  LOG(INFO) << trace_id_ << " finished subprocess."
            << " pid=" << subproc->started().pid()
            << " status=" << subproc->terminated().status()
            << " pending_ms=" << subproc->started().pending_ms()
            << " run_ms=" << subproc->terminated().run_ms()
            << " mem_kb=" << subproc->terminated().mem_kb()
            << " local_killed=" << local_killed_;

  bool local_run_failed = false;
  bool local_run_goma_failure = false;
  if (subproc->started().pid() != SubProcessState::kInvalidPid) {
    local_run_ = true;
    if (!local_killed_) {
      subproc_exit_status_ = subproc->terminated().status();
      // something failed after start of subproc. e.g. kill failed.
      if (subproc_exit_status_ < 0) {
        stats_->set_compiler_proxy_error(true);
        LOG(ERROR) << trace_id_ << " subproc exec failure by goma"
                   << " pid=" << subproc->started().pid()
                   << " status=" << subproc_exit_status_
                   << " error=" << SubProcessTerminated_ErrorTerminate_Name(
                       subproc->terminated().error());
        local_run_goma_failure = true;
      }
      if (subproc_exit_status_ != 0) {
        local_run_failed = true;
      }
    }
    stats_->set_local_pending_time(subproc->started().pending_ms());
    stats_->local_pending_time =
        absl::Milliseconds(subproc->started().pending_ms());

    stats_->set_local_run_time(subproc->terminated().run_ms());
    stats_->local_run_time = absl::Milliseconds(subproc->terminated().run_ms());

    stats_->set_local_mem_kb(subproc->terminated().mem_kb());
    VLOG(1) << trace_id_ << " subproc finished"
            << " pid=" << subproc->started().pid();
  } else {
    // pid is kInvalidPid
    if (subproc->terminated().status() ==
        SubProcessTerminated::kInternalError) {
      std::ostringstream ss;
      ss << "failed to run compiler locally."
         << " pid=" << subproc->started().pid()
         << " error=" << SubProcessTerminated_ErrorTerminate_Name(
             subproc->terminated().error())
         << " status=" << subproc->terminated().status();
      AddErrorToResponse(TO_USER, ss.str(), true);
      local_run_failed = true;
      local_run_goma_failure = true;
    }
  }

  if (state_ == FINISHED && !fail_fallback_) {
    ProcessReply();
    return;
  }

  // This subprocess would be
  // - gch hack (state_ < FINISHED, goma service was slower than local).
  // - verify output. (state_ == INIT) -> SETUP
  // - should fallback. (state_ == INIT) -> LOCAL_FINISHED.
  // - fail fallback. (state_ = FINISHED, fail_fallback_ == true)
  // - fallback only (state_ == LOCAL_RUN)
  // - idle fallback (state_ < FINISHED, goma service was slower than local).
  //   - might be killed because gomacc closed the ipc.
  string orig_stdout = resp_->result().stdout_buffer();
  string orig_stderr = resp_->result().stderr_buffer();

  CHECK(resp_.get() != nullptr) << trace_id_ << " state=" << state_;
  ExecResult* result = resp_->mutable_result();
  CHECK(result != nullptr) << trace_id_ << " state=" << state_;
  if (fail_fallback_ && local_run_ &&
      result->exit_status() != subproc->terminated().status())
    stats_->set_goma_error(true);
  result->set_exit_status(subproc->terminated().status());
  if (result->exit_status() == 0) {
    resp_->clear_error_message();
  }
  if (subproc->terminated().has_term_signal()) {
    std::ostringstream ss;
    ss << "child process exited unexpectedly with signal."
       << " signal=" << subproc->terminated().term_signal();
    exec_error_message_.push_back(ss.str());
    CHECK(result->exit_status() != 0)
        << trace_id_ << " if term signal is not 0, exit status must not be 0."
        << ss.str();
  }

  string stdout_buffer;
  CHECK(!subproc_stdout_.empty()) << trace_id_ << " state=" << state_;
  ReadFileToString(subproc_stdout_.c_str(), &stdout_buffer);
  remove(subproc_stdout_.c_str());
  if (fail_fallback_ && local_run_ && orig_stdout != stdout_buffer)
    stats_->set_goma_error(true);
  result->set_stdout_buffer(stdout_buffer);

  string stderr_buffer;
  CHECK(!subproc_stderr_.empty()) << trace_id_ << " state=" << state_;
  ReadFileToString(subproc_stderr_.c_str(), &stderr_buffer);
  remove(subproc_stderr_.c_str());
  if (fail_fallback_ && local_run_ && orig_stderr != stderr_buffer)
    stats_->set_goma_error(true);
  result->set_stderr_buffer(stderr_buffer);

  if (verify_output_) {
    CHECK_EQ(INIT, state_);
    // local runs done, start remote.
    ProcessSetup();
    return;
  }

  if (precompiling_ && service_->enable_gch_hack()) {
    CHECK_LT(state_, FINISHED) << trace_id_ << " finish subproc";
    CHECK(subproc_ == nullptr) << trace_id_ << " finish subproc";
    // local runs done, not yet goma.
    return;
  }

  // Upload output files asynchronously, so that these files could be
  // used in link phrase.
  if (!local_run_failed) {
    ProcessLocalFileOutput();
    // The callback must be called asynchronously.
    if (service_->store_local_run_output())
      CHECK(local_output_file_callback_ != nullptr);
  }
  if (should_fallback_) {
    CHECK_EQ(INIT, state_);
    state_ = LOCAL_FINISHED;
    finished_ = true;
    // reply fallback response.
    VLOG(2) << trace_id_ << " should fallback:" << resp_->DebugString();
    if (!local_run_failed) {
      ReplyResponse("should fallback");
    } else {
      ReplyResponse("should fallback but local run failed");
    }
    return;
  }
  if (fail_fallback_) {
    CHECK_EQ(FINISHED, state_);
    VLOG(2) << trace_id_ << " fail fallback:" << resp_->DebugString();
    if (!local_run_failed) {
      ReplyResponse("fail fallback");
    } else {
      // If both remote and local failed, it is a real compile failure.
      // We must not preserve goma's error message then. (b/27889459)
      resp_->clear_error_message();
      ReplyResponse("fail fallback and local run also failed");
    }
    return;
  }
  if (state_ == LOCAL_RUN) {
    VLOG(2) << trace_id_ << " local run finished:" << resp_->DebugString();
    state_ = LOCAL_FINISHED;
    finished_ = true;
    if (!local_run_goma_failure) {
      resp_->clear_error_message();
    }
    ReplyResponse("local finish, no goma");
    // TODO: restart from the beginning.
    // Since no remote compile is running here, it is nice to start remote
    // compile in this case.  However, let me postpone the implementation
    // until I understand procedure of CompileTask well.
    return;
  }
  // otherwise, local finishes earlier than remote, or setup.
  if (!local_run_goma_failure) {
    abort_ = true;
    VLOG(2) << trace_id_ << " idle fallback:" << resp_->DebugString();
    resp_->clear_error_message();
    ReplyResponse("local finish, abort goma");
    return;
  }
  // In this case, remote should be running and we expect that success.
  LOG(INFO) << trace_id_ << " local compile failed because of goma."
            << " waiting for remote result.";
}

// ----------------------------------------------------------------

bool CompileTask::failed() const {
  return stats_->exec_exit_status() != 0;
}

bool CompileTask::canceled() const {
  return canceled_;
}

bool CompileTask::cache_hit() const {
  return stats_->cache_hit();
}

bool CompileTask::local_cache_hit() const {
  return stats_->LocalCacheHit();
}

void CompileTask::AddErrorToResponse(
    ErrDest dest, const string& error_message, bool set_error) {
  if (!error_message.empty()) {
    if (set_error)
      LOG(ERROR) << trace_id_ << " " << error_message;
    else
      LOG(WARNING) << trace_id_ << " " << error_message;
    std::ostringstream msg;
    msg << "compiler_proxy ";
    msg << "[" << handler_timer_.GetDuration() << "]: ";
    msg << error_message;
    if (dest == TO_USER) {
      DCHECK(set_error) << trace_id_
                        << " user error should always set error."
                        << " msg=" << error_message;
      resp_->add_error_message(msg.str());
    } else {
      service_->RecordErrorToLog(error_message, set_error);
    }
    exec_error_message_.push_back(msg.str());
  }
  if (set_error &&
      (!resp_->has_result() || resp_->result().exit_status() == 0)) {
    resp_->mutable_result()->set_exit_status(1);
  }
}

void CompileTask::DumpRequest() const {
  if (!frozen_timestamp_.has_value()) {
    LOG(ERROR) << trace_id_ << " DumpRequest called on active task";
    return;
  }
  LOG(INFO) << trace_id_ << " DumpRequest";
  string filename = "exec_req.data";
  ExecReq req;
  CommandSpec* command_spec = req.mutable_command_spec();
  *command_spec = command_spec_;
  command_spec->set_local_compiler_path(local_compiler_path_);
  if (compiler_info_state_.get() != nullptr) {
    const CompilerInfo& compiler_info = compiler_info_state_.get()->info();
    std::vector<string> args(stats_->arg().begin(), stats_->arg().end());
    std::unique_ptr<CompilerFlags> flags(
        CompilerFlagsParser::New(args, stats_->cwd()));
    FixCommandSpec(compiler_info, *flags, command_spec);
    FixSystemLibraryPath(system_library_paths_, command_spec);
    MayFixSubprogramSpec(req.mutable_subprogram());
  } else {
    // If compiler_info_state_ is nullptr, it would be should_fallback_.
    LOG_IF(ERROR, !should_fallback_)
        << trace_id_ << " DumpRequest compiler_info_state_ is nullptr.";
    filename = "local_exec_req.data";
  }

  for (const auto& arg : stats_->arg())
    req.add_arg(arg);
  for (const auto& env : stats_->env())
    req.add_env(env);
  for (const auto& expanded_arg : stats_->expanded_arg())
    req.add_expanded_arg(expanded_arg);
  req.set_cwd(stats_->cwd());
  *req.mutable_requester_info() = requester_info_;

  std::ostringstream ss;
  ss << "task_request_" << id_;
  const string task_request_dir = file::JoinPath(service_->tmp_dir(), ss.str());
  file::RecursivelyDelete(task_request_dir, file::Defaults());
#ifndef _WIN32
  PCHECK(mkdir(task_request_dir.c_str(), 0755) == 0);
#else
  if (!CreateDirectoryA(task_request_dir.c_str(), nullptr)) {
    DWORD err = GetLastError();
    LOG_SYSRESULT(err);
    LOG_IF(FATAL, FAILED(err)) << "CreateDirectoryA " << task_request_dir;
  }
#endif

  for (const auto& input_filename : required_files_) {
    ExecReq_Input* input = req.add_input();
    input->set_filename(input_filename);
    FileServiceDumpClient fs;
    const string abs_input_filename = file::JoinPathRespectAbsolute(
        req.cwd(), input_filename);
    if (!fs.CreateFileBlob(abs_input_filename, true,
                           input->mutable_content())) {
      LOG(ERROR) << trace_id_ << " DumpRequest failed to create fileblob:"
                 << input_filename;
    } else {
      input->set_hash_key(FileServiceClient::ComputeHashKey(input->content()));
      if (!fs.Dump(file::JoinPath(task_request_dir, input->hash_key()))) {
        LOG(ERROR) << trace_id_ << " DumpRequest failed to store fileblob:"
                   << input_filename
                   << " hash:" << input->hash_key();
      }
    }
  }
  string r;
  req.SerializeToString(&r);
  filename = file::JoinPath(task_request_dir, filename);
  if (!WriteStringToFile(r, filename)) {
    LOG(ERROR) << trace_id_ << " DumpRequest failed to write: " << filename;
  } else {
    LOG(INFO) << trace_id_ << " DumpRequest wrote serialized proto: "
              << filename;
  }

  // Only show file hash for text_format.
  for (auto& input : *req.mutable_input()) {
    input.clear_content();
  }

  string text_req;
  google::protobuf::TextFormat::PrintToString(req, &text_req);
  filename += ".txt";
  if (!WriteStringToFile(text_req, filename)) {
    LOG(ERROR) << trace_id_ << " DumpRequest failed to write: " << filename;
  } else {
    LOG(INFO) << trace_id_ << " DumpRequest wrote text proto: " << filename;
  }

  LOG(INFO) << trace_id_ << " DumpRequest done";
}

}  // namespace devtools_goma
