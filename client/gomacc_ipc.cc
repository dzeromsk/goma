// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "gomacc_ipc.h"

#include <sys/errno.h>
#include <unistd.h>

#include <string>

#include "compiler_specific.h"
MSVC_PUSH_DISABLE_WARNING_FOR_PROTO()
#include "prototmp/gomacc_message.pb.h"
MSVC_POP_WARNING()

using std::string;

namespace {

#define HANDLE_EINTR(x) ({ \
  typeof(x) __eintr_result__; \
  do { \
    __eintr_result__ = x; \
  } while (__eintr_result__ == -1 && errno == EINTR); \
  __eintr_result__;\
})

int ReadAll(int fd, char* buf, int bufsize) {
  int nread = 0;
  while (bufsize > 0) {
    int n = HANDLE_EINTR(read(fd, buf, bufsize));
    if (n < 0)
      return -1;
    if (n == 0)
      return nread;
    bufsize -= n;
    buf += n;
    nread += n;
  }
  return nread;
}

int WriteAll(int fd, const char* buf, int bufsize) {
  int nwritten = 0;
  while (bufsize > 0)  {
    int n = HANDLE_EINTR(write(fd, buf, bufsize));
    if (n < 0)
      return -1;
    if (n == 0)
      return nwritten;
    bufsize -= n;
    buf += n;
    nwritten += n;
  }
  return nwritten;
}

}  // namespace

bool SendCommand(int fd, GomaCCCommand cmd) {
  int value = static_cast<int>(cmd);
  if (WriteAll(fd, (char*)&value, sizeof(int)) != sizeof(int))
    return false;
  return true;
}

bool ReceiveCommand(int fd, GomaCCCommand* cmd) {
  int value;
  if (ReadAll(fd, (char*)&value, sizeof(int)) != sizeof(int))
    return false;
  *cmd = static_cast<GomaCCCommand>(value);
  return true;
}

bool SendMessage(int sock, const google::protobuf::Message& message) {
  string msg;
  message.SerializeToString(&msg);
  int size = static_cast<int>(msg.size());
  if (!WriteAll(sock, (char*)(&size), sizeof(size)))
    return false;
  if (!WriteAll(sock, msg.c_str(), msg.size()))
    return false;
  return true;
}

bool ReceiveMessage(int sock, google::protobuf::Message* message) {
  int length;
  if (!ReadAll(sock, (char*)(&length), sizeof(length)))
    return false;
  std::unique_ptr<char[]> deleter;
  // No multi-thread safe.
  static char scratch[2048];
  char* buf = scratch;
  if (static_cast<size_t>(length) > sizeof(scratch)) {
    buf = new char[length];
    deleter.reset(buf);
  }
  if (!ReadAll(sock, buf, length))
    return false;
  message->ParseFromArray(buf, length);
  return true;
}
