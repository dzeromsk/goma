// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.




#include "file_helper.h"

#include <errno.h>

#ifdef _WIN32
# include "config_win.h"
#endif  // _WIN32

#include "absl/strings/string_view.h"
#include "glog/logging.h"
#include "path.h"
#include "scoped_fd.h"

namespace devtools_goma {

bool ReadFileToString(absl::string_view filename, string* OUTPUT) {
  const string& name = string(filename);
  DCHECK(OUTPUT != nullptr) << filename;
  OUTPUT->clear();

  devtools_goma::ScopedFd fd(devtools_goma::ScopedFd::OpenForRead(name));
  if (!fd.valid()) {
#ifndef _WIN32
    if (errno == ENOENT)
      VLOG(1) << "GOMA: file not found:" << name;
    else
      PLOG(ERROR) << "GOMA: failed to open " << name;
#else
    DWORD err = GetLastError();
    if ((err == ERROR_FILE_NOT_FOUND) || (err == ERROR_PATH_NOT_FOUND)) {
      VLOG(1) << "GOMA: file not found:" << name;
    } else {
      LOG_SYSRESULT(err);
      // PLOG checks std errno, which will always be 0, so use LOG(ERROR) here.
      LOG(ERROR) << "GOMA: failed to open " << name;
    }
#endif
    return false;
  }
  size_t file_size = 0;
  if (!fd.GetFileSize(&file_size)) {
    LOG(ERROR) << "filename: [" << name << "] stat failed";
    return false;
  }
  VLOG(1) << "filename: [" << name << "] " << " size=" << file_size;
  if (file_size == 0) {
    return true;
  }
  OUTPUT->resize(file_size);
  for (int r, len = 0; static_cast<size_t>(len) < file_size;) {
    r = fd.Read(const_cast<char*>(OUTPUT->data() + len), file_size - len);
    if (r < 0) {
      LOG(ERROR) << "read " << name;
      return false;
    }
    if (r == 0) {
      LOG(ERROR) << "read unexpected EOF at " << len
                 << " name " << name << " size=" << file_size;
      return false;
    }
    len += r;
  }
  return true;
}

bool WriteStringToFile(absl::string_view data, absl::string_view file_name) {
  devtools_goma::ScopedFd fd(
      devtools_goma::ScopedFd::Create(string(file_name), 0600));
  if (!fd.valid()) {
    LOG(ERROR) << "GOMA: failed to open " << file_name;
    return false;
  }
  if (fd.Write(data.data(), data.size()) == -1) {
    LOG(ERROR) << "write " << file_name;
    return false;
  }
  return true;
}

}  // namespace devtools_goma
