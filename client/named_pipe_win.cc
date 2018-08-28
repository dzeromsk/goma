// Copyright 2016 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "named_pipe_win.h"

#include <glog/logging.h>

#include "simple_timer.h"

namespace devtools_goma {

ScopedNamedPipe::~ScopedNamedPipe() {
  Close();
}

void ScopedNamedPipe::StreamWrite(std::ostream& os) const {
  os << handle_;
}

ssize_t ScopedNamedPipe::Read(void* ptr, size_t len) const {
  DWORD bytes_read = 0;
  if (!ReadFile(handle_, ptr, len, &bytes_read, nullptr)) {
    LOG_SYSRESULT(GetLastError());
    return -1;
  }
  return bytes_read;
}

ssize_t ScopedNamedPipe::Write(const void* ptr, size_t len) const {
  DWORD bytes_written = 0;
  if (!WriteFile(handle_, ptr, len, &bytes_written, nullptr)) {
    LOG_SYSRESULT(GetLastError());
    return -1;
  }
  return bytes_written;
}

namespace {

void IOCompletionRoutine(
    DWORD error_code, DWORD num_bytes, LPOVERLAPPED overlapped) {
}

ssize_t WaitAsyncOp(HANDLE handle, ssize_t bufsize,
                    LPOVERLAPPED op, absl::Duration timeout) {
  SimpleTimer timer;
  DWORD w = ERROR_TIMEOUT;
  while (timeout >= absl::ZeroDuration()) {
    timer.Start();
    w = WaitForSingleObjectEx(handle, absl::ToInt64Milliseconds(timeout), TRUE);
    switch (w) {
      case WAIT_OBJECT_0:
        timeout -= timer.GetDuration();
        {
          DWORD num_bytes = 0;
          if (GetOverlappedResult(handle, op, &num_bytes, FALSE)) {
            return num_bytes;
          }
          DWORD err = GetLastError();
          if (err == ERROR_IO_INCOMPLETE) {
            continue;
          }
          if (err == ERROR_MORE_DATA) {
            return bufsize;
          }
          LOG_SYSRESULT(err);  // async op's error.
          return FAIL;
        }

      case WAIT_IO_COMPLETION:
        timeout -= timer.GetDuration();
        continue;

      case WAIT_TIMEOUT:
        break;

      default:
        LOG_SYSRESULT(GetLastError());
        LOG(ERROR) << "wait AsyncOp w=" << w;
        break;

    }
    break;
  }
  if (CancelIo(handle) == 0) {
    LOG_SYSRESULT(GetLastError());
    LOG(ERROR) << "cancel by timeout";
  }
  DWORD num_bytes = 0;
  if (GetOverlappedResult(handle, op, &num_bytes, TRUE) != 0) {
    // partially completed?
    return num_bytes;
  }
  DWORD err = GetLastError();
  CHECK_NE(err, static_cast<DWORD>(ERROR_IO_INCOMPLETE))
      << "GetOverlappedResult with bWait=TRUE should not result "
      << "in ERROR_IO_INCOMPLETE";
  switch (err) {
    case ERROR_MORE_DATA:
      // io completed before CancelIo?
      return bufsize;
    case ERROR_OPERATION_ABORTED:
      // io cancelled by CancelIo.
      break;
    default:
      // unexpected error?
      LOG_SYSRESULT(err);
      LOG(ERROR) << "cancel result error=" << err;
  }
  if (w == WAIT_TIMEOUT) {
    return ERR_TIMEOUT;
  }
  return FAIL;
}

}  // anonymous namespace

ssize_t ScopedNamedPipe::ReadWithTimeout(
    char* buf, size_t bufsize, absl::Duration timeout) const {
  OVERLAPPED op;
  memset(&op, 0, sizeof op);
  BOOL ret = ReadFileEx(handle_, buf, bufsize, &op,
                        &IOCompletionRoutine);
  if (!ret) {
    LOG_SYSRESULT(GetLastError());
    return FAIL;
  }
  return WaitAsyncOp(handle_, bufsize, &op, timeout);
}

ssize_t ScopedNamedPipe::WriteWithTimeout(
    const char* buf, size_t bufsize, absl::Duration timeout) const {
  OVERLAPPED op;
  memset(&op, 0, sizeof op);
  BOOL ret = WriteFileEx(handle_, buf, bufsize, &op,
                         &IOCompletionRoutine);
  if (!ret) {
    LOG_SYSRESULT(GetLastError());
    return FAIL;
  }
  return WaitAsyncOp(handle_, bufsize, &op, timeout);
}

int ScopedNamedPipe::WriteString(absl::string_view message,
                                 absl::Duration timeout) const {
  const char* p = message.data();
  int size = message.size();
  while (size > 0) {
    int ret = WriteWithTimeout(p, size, timeout);
    if (ret < 0) {
      LOG(ERROR) << "write failure: " << ret
                 << " writen=" << (message.size() - size)
                 << " size=" << size
                 << " out of " << message.size();
      return ret;
    }
    p += ret;
    size -= ret;
  }
  return OK;
}

std::string ScopedNamedPipe::GetLastErrorMessage() const {
  char message[1024];
  FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, nullptr,
                 GetLastError(), 0,
                 message, sizeof(message), nullptr);
  return message;
}

void ScopedNamedPipe::reset(HANDLE handle) {
  Close();
  handle_ = handle;
}

bool ScopedNamedPipe::Close() {
  if (valid()) {
    return CloseHandle(release()) == TRUE;
  }
  return true;
}

}  // namespace devtools_goma
