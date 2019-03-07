// Copyright 2019 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "file_service_blob_downloader.h"

#include <utility>

#include "file_data_output.h"
#include "goma_file_http.h"

namespace devtools_goma {

FileServiceBlobDownloader::FileServiceBlobDownloader(
    std::unique_ptr<FileServiceHttpClient> file_service)
    : file_service_(std::move(file_service)) {}

bool FileServiceBlobDownloader::Download(const ExecResult_Output& output,
                                         OutputFileInfo* info) {
  if (info->tmp_filename.empty()) {
    std::unique_ptr<FileDataOutput> str_output(
        FileDataOutput::NewStringOutput(output.filename(), &info->content));
    return file_service_->OutputFileBlob(output.blob(), str_output.get());
  } else {
    // TODO: We might want to restrict paths this program may write?
    const auto& filename = info->tmp_filename;
    remove(filename.c_str());
    std::unique_ptr<FileDataOutput> file_output(
        FileDataOutput::NewFileOutput(filename, info->mode));

    return file_service_->OutputFileBlob(output.blob(), file_output.get());
  }
}

}  // namespace devtools_goma
