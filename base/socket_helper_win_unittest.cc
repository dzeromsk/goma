// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


// This is a Windows-only unit test
#ifdef _WIN32
#include "socket_helper_win.h"

#include <glog/logging.h>
#include <gtest/gtest.h>

#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "lockhelper.h"
#include "platform_thread.h"
using std::string;

namespace devtools_goma {

const char* TEST_STRING = "Hola! Amigo!";

class SocketPairTestThread : public PlatformThread::Delegate {
 public:
  typedef enum {
    kInitial,
    kAsync,
    kBlockRecv,
    kBlockSend,
    kTerminate
  } State;

  explicit SocketPairTestThread(int fd)
      : state_(kInitial), signal_(::CreateEvent(nullptr, TRUE, FALSE, nullptr)),
        socket_(fd) {
    CHECK_NE(signal_, INVALID_HANDLE_VALUE);
  }

  ~SocketPairTestThread() override {
    closesocket(socket_);
    CloseHandle(signal_);
  }

  void ThreadMain() override {
    bool terminate_signaled = false;
    for (; !terminate_signaled; ) {
      WaitForSingleObject(signal_, INFINITE);
      AutoLock lock(&lock_);
      switch (state_) {
        case kInitial:
          break;

        case kAsync:
          {
            timeval tv;
            tv.tv_sec = 2;
            tv.tv_usec = 0;
            bool r_done = false;
            fd_set r_set;
            send(socket_, TEST_STRING,
                 static_cast<int>(strlen(TEST_STRING)), 0);
            FD_ZERO(&r_set);
            MSVC_PUSH_DISABLE_WARNING_FOR_FD_SET();
            FD_SET(static_cast<SOCKET>(socket_), &r_set);
            MSVC_POP_WARNING();
            while (!r_done) {
              int result = select(socket_ + 1, &r_set, 0, 0, &tv);
              if (FD_ISSET(socket_, &r_set)) {
                char buf[256] = {0};
                recv(socket_, buf, 256, 0);
                message_ = buf;
                r_done = true;
              }
              if (result == 0 || result == SOCKET_ERROR) {
                break;  // recv timeout or select failed
              }
            }
          }
          break;

        case kBlockRecv:
          {
            char buf[256] = {0};
            recv(socket_, buf, 256, 0);
            message_ = buf;
          }
          break;

        case kBlockSend:
          send(socket_, TEST_STRING, static_cast<int>(strlen(TEST_STRING)), 0);
          break;

        default:  // kTerminate
          terminate_signaled = true;
          break;
      }
      ResetEvent(signal_);
    }
  }

  void set_state(State state) {
    // Block set_state until |signal_| is not set.
    while (::WaitForSingleObjectEx(signal_, 0, TRUE) != WAIT_TIMEOUT) {
      absl::SleepFor(absl::Milliseconds(100));
    }
    AutoLock lock(&lock_);
    state_ = state;
    SetEvent(signal_);
  }

  string message() {
    AutoLock lock(&lock_);
    return message_;
  }

  void Reset() {
    AutoLock lock(&lock_);
    message_.clear();
  }

 private:
  Lock lock_;
  State state_;
  string message_;
  HANDLE signal_;
  int socket_;
  DISALLOW_COPY_AND_ASSIGN(SocketPairTestThread);
};

TEST(SocketHelperWin, BlockingSocketPair) {
  int fd[2] = {0};
  EXPECT_NE(SOCKET_ERROR, socketpair(AF_INET, SOCK_STREAM, 0, fd));

  SocketPairTestThread thread0(fd[0]);
  SocketPairTestThread thread1(fd[1]);

  PlatformThreadHandle handle0 = kNullThreadHandle;
  PlatformThreadHandle handle1 = kNullThreadHandle;

  EXPECT_TRUE(PlatformThread::Create(&thread0, &handle0));
  EXPECT_TRUE(PlatformThread::Create(&thread1, &handle1));

  thread0.set_state(SocketPairTestThread::kBlockRecv);
  thread1.set_state(SocketPairTestThread::kBlockSend);

  thread0.set_state(SocketPairTestThread::kBlockSend);
  thread1.set_state(SocketPairTestThread::kBlockRecv);

  thread0.set_state(SocketPairTestThread::kTerminate);
  thread1.set_state(SocketPairTestThread::kTerminate);

  WaitForSingleObject(handle0, 2000);
  WaitForSingleObject(handle1, 2000);

  EXPECT_STREQ(TEST_STRING, thread0.message().c_str());
  EXPECT_STREQ(TEST_STRING, thread1.message().c_str());
}

TEST(SocketHelperWin, AsyncSocketPair) {
  int fd[2] = {0};
  EXPECT_EQ(0, async_socketpair(fd));

  SocketPairTestThread thread0(fd[0]);
  SocketPairTestThread thread1(fd[1]);

  PlatformThreadHandle handle0 = kNullThreadHandle;
  PlatformThreadHandle handle1 = kNullThreadHandle;

  EXPECT_TRUE(PlatformThread::Create(&thread0, &handle0));
  EXPECT_TRUE(PlatformThread::Create(&thread1, &handle1));

  thread0.set_state(SocketPairTestThread::kAsync);
  thread1.set_state(SocketPairTestThread::kAsync);

  thread0.set_state(SocketPairTestThread::kTerminate);
  thread1.set_state(SocketPairTestThread::kTerminate);

  WaitForSingleObject(handle0, 2000);
  WaitForSingleObject(handle1, 2000);

  EXPECT_STREQ(TEST_STRING, thread0.message().c_str());
  EXPECT_STREQ(TEST_STRING, thread1.message().c_str());
}

}  // namespace devtools_goma

#endif  // _WIN32
