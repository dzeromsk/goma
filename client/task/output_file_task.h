// Copyright 2019 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_TASK_OUTPUT_FILE_TASK_H_
#define DEVTOOLS_GOMA_CLIENT_TASK_OUTPUT_FILE_TASK_H_

#include <memory>
#include <string>

#include "base/basictypes.h"
#include "goma_blob.h"
#include "simple_timer.h"

using std::string;

namespace devtools_goma {

class CompileTask;
class ExecResult_Output;
class OneshotClosure;
class WorkerThreadManager;

class OutputFileTask {
 public:
  using OutputFileInfo = BlobClient::Downloader::OutputFileInfo;

  // Doesn't take ownership of |info|.
  OutputFileTask(WorkerThreadManager* wm,
                 std::unique_ptr<BlobClient::Downloader> blob_downloader,
                 CompileTask* task,
                 int output_index,
                 const ExecResult_Output& output,
                 OutputFileInfo* info);
  ~OutputFileTask();

  void Run(OneshotClosure* closure);

  CompileTask* task() const { return task_; }
  const ExecResult_Output& output() const { return output_; }
  const SimpleTimer& timer() const { return timer_; }
  bool success() const { return success_; }
  bool IsInMemory() const;

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

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_TASK_OUTPUT_FILE_TASK_H_
