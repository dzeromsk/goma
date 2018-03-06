// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include <memory>

#include "worker_thread_manager.h"

#ifndef _WIN32
#include <pthread.h>
#include <sys/socket.h>
#include <sys/types.h>
#else
#include "socket_helper_win.h"
#endif

#include "callback.h"
#include "compiler_specific.h"
#include "socket_descriptor.h"
#include "lockhelper.h"
#include "mock_socket_factory.h"
#include "platform_thread.h"
#include "scoped_fd.h"
#include "simple_timer.h"

#include <glog/logging.h>
#include <gtest/gtest.h>

namespace devtools_goma {

class WorkerThreadManagerTest : public ::testing::Test {
 public:
  WorkerThreadManagerTest()
      : test_threadid_(0),
        num_test_threadid_(0),
        periodic_counter_(0) {
  }
  ~WorkerThreadManagerTest() override {
  }

 protected:
  class TestReadContext {
   public:
    TestReadContext(int fd, double timeout)
        : fd_(fd), timeout_(timeout), num_read_(-1), d_(nullptr),
          timeout_called_(false) {
    }
    ~TestReadContext() {
    }
    const int fd_;
    const double timeout_;
    int num_read_;
    SocketDescriptor* d_;
    bool timeout_called_;

   private:
    DISALLOW_COPY_AND_ASSIGN(TestReadContext);
  };

  class TestWriteContext {
   public:
    TestWriteContext(int fd, int total_write)
        : fd_(fd), total_write_(total_write), num_write_(-1), d_(nullptr) {
    }
    ~TestWriteContext() {
    }
    const int fd_;
    const int total_write_;
    int num_write_;
    SocketDescriptor* d_;

   private:
    DISALLOW_COPY_AND_ASSIGN(TestWriteContext);
  };

  void SetUp() override {
    wm_.reset(new WorkerThreadManager);
    test_threadid_ = 0;
    num_test_threadid_ = 0;
    periodic_counter_ = 0;
  }
  void TearDown() override {
    wm_.reset(nullptr);
  }

  void Reset() {
    AutoLock lock(&mu_);
    test_threadid_ = 0;
    num_test_threadid_ = 0;
  }

  OneshotClosure* NewTestRun() {
    {
      AutoLock lock(&mu_);
      EXPECT_TRUE(!test_threadid_);
    }
    return NewCallback(
        this, &WorkerThreadManagerTest::TestRun);
  }

  void TestRun() {
    AutoLock lock(&mu_);
    test_threadid_ = wm_->GetCurrentThreadId();
    cond_.Signal();
  }

  void WaitTestRun() {
    AutoLock lock(&mu_);
    while (test_threadid_ == 0) {
      cond_.Wait(&mu_);
    }
  }

  OneshotClosure* NewTestDispatch() {
    {
      AutoLock lock(&mu_);
      EXPECT_TRUE(!test_threadid_);
    }
    return NewCallback(
        this, &WorkerThreadManagerTest::TestDispatch);
  }

  void TestDispatch() {
    while (wm_->Dispatch()) {
      AutoLock lock(&mu_);
      if (test_threadid_ == 0)
        continue;
      EXPECT_EQ(test_threadid_, wm_->GetCurrentThreadId());
      cond_.Signal();
      return;
    }
    LOG(FATAL) << "Dispatch unexpectedly finished";
  }

  OneshotClosure* NewTestThreadId(
      WorkerThreadManager::ThreadId id) {
    return NewCallback(
        this, &WorkerThreadManagerTest::TestThreadId, id);
  }

  void TestThreadId(WorkerThreadManager::ThreadId id) {
    EXPECT_EQ(id, wm_->GetCurrentThreadId());
    AutoLock lock(&mu_);
    ++num_test_threadid_;
    cond_.Signal();
  }

  void WaitTestThreadHandle(int num) {
    AutoLock lock(&mu_);
    while (num_test_threadid_ < num) {
      cond_.Wait(&mu_);
    }
  }

  std::unique_ptr<PermanentClosure> NewPeriodicRun() {
    {
      AutoLock lock(&mu_);
      periodic_counter_ = 0;
    }
    return NewPermanentCallback(
        this, &WorkerThreadManagerTest::TestPeriodicRun);
  }

  void TestPeriodicRun() {
    AutoLock lock(&mu_);
    ++periodic_counter_;
    cond_.Signal();
  }

  void WaitTestPeriodicRun(int n) {
    AutoLock lock(&mu_);
    while (periodic_counter_ < n) {
      cond_.Wait(&mu_);
    }
  }

  OneshotClosure* NewTestDescriptorRead(TestReadContext* tc) {
    {
      AutoLock lock(&mu_);
      EXPECT_GT(tc->fd_, 0);
      EXPECT_LT(tc->num_read_, 0);
      EXPECT_TRUE(tc->d_ == nullptr);
    }
    return NewCallback(
        this, &WorkerThreadManagerTest::TestDescriptorRead, tc);
  }

  void TestDescriptorRead(TestReadContext* tc) {
    ScopedSocket sock;
    double timeout = 0;
    {
      AutoLock lock(&mu_);
      EXPECT_LT(tc->num_read_, 0);
      EXPECT_TRUE(tc->d_ == nullptr);
      timeout = tc->timeout_;
      EXPECT_FALSE(tc->timeout_called_);
      sock.reset(tc->fd_);
    }
    SocketDescriptor* d =
        wm_->RegisterSocketDescriptor(
            std::move(sock), WorkerThreadManager::PRIORITY_HIGH);
    d->NotifyWhenReadable(
        NewPermanentCallback(this, &WorkerThreadManagerTest::DoRead, tc));
    if (timeout > 0) {
      d->NotifyWhenTimedout(
          timeout,
          NewCallback(
              this, &WorkerThreadManagerTest::DoTimeout, tc));
    }
    AutoLock lock(&mu_);
    tc->num_read_ = 0;
    tc->d_ = d;
    cond_.Signal();
  }

  void DoRead(TestReadContext* tc) {
    SocketDescriptor* d = nullptr;
    {
      AutoLock lock(&mu_);
      EXPECT_GE(tc->num_read_, 0);
      EXPECT_EQ(tc->fd_, tc->d_->fd());
      EXPECT_EQ(WorkerThreadManager::PRIORITY_HIGH, tc->d_->priority());
      d = tc->d_;
    }
    char buf[1] = { 42 };
    int n = d->Read(buf, 1);
    if (n > 0) {
      EXPECT_EQ(1, n);
    } else {
      d->StopRead();
      wm_->RunClosureInThread(
          FROM_HERE,
          wm_->GetCurrentThreadId(),
          NewCallback(
              this, &WorkerThreadManagerTest::DoStopRead, tc),
          WorkerThreadManager::PRIORITY_IMMEDIATE);
    }
    AutoLock lock(&mu_);
    ++tc->num_read_;
    cond_.Signal();
  }

  void WaitTestRead(TestReadContext* tc, int n) {
    AutoLock lock(&mu_);
    while (tc->num_read_ != n) {
      cond_.Wait(&mu_);
    }
  }

  void DoTimeout(TestReadContext* tc) {
    SocketDescriptor* d = nullptr;
    {
      AutoLock lock(&mu_);
      EXPECT_EQ(tc->fd_, tc->d_->fd());
      EXPECT_EQ(WorkerThreadManager::PRIORITY_HIGH, tc->d_->priority());
      EXPECT_GT(tc->timeout_, 0.0);
      EXPECT_FALSE(tc->timeout_called_);
      d = tc->d_;
    }
    d->StopRead();
    wm_->RunClosureInThread(
        FROM_HERE,
        wm_->GetCurrentThreadId(),
        NewCallback(
            this, &WorkerThreadManagerTest::DoStopRead, tc),
        WorkerThreadManager::PRIORITY_IMMEDIATE);
    AutoLock lock(&mu_);
    tc->timeout_called_ = true;
    cond_.Signal();
  }

  void DoStopRead(TestReadContext* tc) {
    int fd;
    SocketDescriptor* d = nullptr;
    {
      AutoLock lock(&mu_);
      EXPECT_EQ(tc->fd_, tc->d_->fd());
      EXPECT_EQ(WorkerThreadManager::PRIORITY_HIGH, tc->d_->priority());
      fd = tc->fd_;
      d = tc->d_;
    }
    d->ClearReadable();
    d->ClearTimeout();
    ScopedSocket sock(wm_->DeleteSocketDescriptor(d));
    EXPECT_EQ(fd, sock.get());
    sock.Close();
    AutoLock lock(&mu_);
    tc->d_ = nullptr;
    cond_.Signal();
  }

  void WaitTestReadFinish(TestReadContext* tc) {
    AutoLock lock(&mu_);
    while (tc->d_ != nullptr) {
      cond_.Wait(&mu_);
    }
  }

  OneshotClosure* NewTestDescriptorWrite(TestWriteContext* tc) {
    {
      AutoLock lock(&mu_);
      EXPECT_GT(tc->fd_, 0);
      EXPECT_LT(tc->num_write_, 0);
      EXPECT_TRUE(tc->d_ == nullptr);
    }
    return NewCallback(
        this, &WorkerThreadManagerTest::TestDescriptorWrite, tc);
  }

  void TestDescriptorWrite(TestWriteContext* tc) {
    ScopedSocket sock;
    {
      AutoLock lock(&mu_);
      EXPECT_LT(tc->num_write_, 0);
      EXPECT_TRUE(tc->d_ == nullptr);
      sock.reset(tc->fd_);
    }
    SocketDescriptor* d =
        wm_->RegisterSocketDescriptor(
            std::move(sock), WorkerThreadManager::PRIORITY_HIGH);
    d->NotifyWhenWritable(
        NewPermanentCallback(this, &WorkerThreadManagerTest::DoWrite, tc));
    AutoLock lock(&mu_);
    tc->num_write_ = 0;
    tc->d_ = d;
    cond_.Signal();
  }

  void DoWrite(TestWriteContext* tc) {
    int num_write = 0;
    int total_write = 0;
    SocketDescriptor* d = nullptr;
    {
      AutoLock lock(&mu_);
      EXPECT_GE(tc->num_write_, 0);
      EXPECT_EQ(tc->fd_, tc->d_->fd());
      EXPECT_EQ(WorkerThreadManager::PRIORITY_HIGH, tc->d_->priority());
      num_write = tc->num_write_;
      total_write = tc->total_write_;
      d = tc->d_;
    }
    char buf[1] = { 42 };
    int n = 0;
    if (num_write < total_write) {
      n = d->Write(buf, 1);
    }
    if (n > 0) {
      EXPECT_EQ(1, n);
    } else {
      d->StopWrite();
      wm_->RunClosureInThread(
          FROM_HERE,
          wm_->GetCurrentThreadId(),
          NewCallback(
              this, &WorkerThreadManagerTest::DoStopWrite, tc),
          WorkerThreadManager::PRIORITY_IMMEDIATE);
      return;
    }
    AutoLock lock(&mu_);
    ++tc->num_write_;
    cond_.Signal();
  }

  void WaitTestWrite(TestWriteContext* tc, int n) {
    AutoLock lock(&mu_);
    while (tc->num_write_ < n) {
      cond_.Wait(&mu_);
    }
  }

  void DoStopWrite(TestWriteContext* tc) {
    int fd;
    SocketDescriptor* d = nullptr;
    {
      AutoLock lock(&mu_);
      EXPECT_EQ(tc->fd_, tc->d_->fd());
      EXPECT_EQ(WorkerThreadManager::PRIORITY_HIGH, tc->d_->priority());
      fd = tc->fd_;
      d = tc->d_;
    }
    d->ClearWritable();
    ScopedSocket sock(wm_->DeleteSocketDescriptor(d));
    EXPECT_EQ(fd, sock.get());
    sock.Close();
    AutoLock lock(&mu_);
    tc->d_ = nullptr;
    cond_.Signal();
  }

  void WaitTestWriteFinish(TestWriteContext* tc) {
    AutoLock lock(&mu_);
    while (tc->d_ != nullptr) {
      cond_.Wait(&mu_);
    }
  }

  WorkerThreadManager::ThreadId test_threadid() const {
    AutoLock lock(&mu_);
    return test_threadid_;
  }

  int num_test_threadid() const {
    AutoLock lock(&mu_);
    return num_test_threadid_;
  }

  int periodic_counter() const {
    AutoLock lock(&mu_);
    return periodic_counter_;
  }

  std::unique_ptr<WorkerThreadManager> wm_;
  mutable Lock mu_;

 private:
  ConditionVariable cond_;
  WorkerThreadManager::ThreadId test_threadid_;
  int num_test_threadid_;
  int periodic_counter_;
  DISALLOW_COPY_AND_ASSIGN(WorkerThreadManagerTest);
};

TEST_F(WorkerThreadManagerTest, NoRun) {
  wm_->Start(2);
  EXPECT_EQ(2U, wm_->num_threads());
  wm_->Finish();
}

TEST_F(WorkerThreadManagerTest, RunClosure) {
  wm_->Start(2);
  wm_->RunClosure(FROM_HERE, NewTestRun(),
                  WorkerThreadManager::PRIORITY_LOW);
  WaitTestRun();
  wm_->Finish();
  EXPECT_NE(test_threadid(), static_cast<WorkerThreadManager::ThreadId>(0));
  EXPECT_NE(test_threadid(), wm_->GetCurrentThreadId());
}

TEST_F(WorkerThreadManagerTest, Dispatch) {
  wm_->Start(1);
  wm_->RunClosure(FROM_HERE, NewTestDispatch(),
                  WorkerThreadManager::PRIORITY_LOW);
  wm_->RunClosure(FROM_HERE, NewTestRun(),
                  WorkerThreadManager::PRIORITY_LOW);
  WaitTestRun();
  wm_->Finish();
  EXPECT_NE(test_threadid(), static_cast<WorkerThreadManager::ThreadId>(0));
  EXPECT_NE(test_threadid(), wm_->GetCurrentThreadId());
}

TEST_F(WorkerThreadManagerTest, RunClosureInThread) {
  wm_->Start(2);
  wm_->RunClosure(FROM_HERE, NewTestRun(),
                  WorkerThreadManager::PRIORITY_LOW);
  WaitTestRun();
  EXPECT_NE(test_threadid(), static_cast<WorkerThreadManager::ThreadId>(0));
  EXPECT_NE(test_threadid(), wm_->GetCurrentThreadId());
  WorkerThreadManager::ThreadId id = test_threadid();
  Reset();
  EXPECT_TRUE(!test_threadid());
  EXPECT_EQ(num_test_threadid(), 0);
  const int kNumTestThreadHandle = 100;
  for (int i = 0; i < kNumTestThreadHandle; ++i) {
    wm_->RunClosureInThread(FROM_HERE, id, NewTestThreadId(id),
                          WorkerThreadManager::PRIORITY_LOW);
  }
  WaitTestThreadHandle(kNumTestThreadHandle);
  wm_->Finish();
}

TEST_F(WorkerThreadManagerTest, RunClosureInPool) {
  wm_->Start(1);
  int pool = wm_->StartPool(1, "test");
  EXPECT_NE(pool, WorkerThreadManager::kAlarmPool);
  EXPECT_NE(pool, WorkerThreadManager::kFreePool);
  EXPECT_EQ(2U, wm_->num_threads());

  wm_->RunClosure(FROM_HERE, NewTestRun(),
                  WorkerThreadManager::PRIORITY_LOW);
  WaitTestRun();
  EXPECT_NE(test_threadid(), static_cast<WorkerThreadManager::ThreadId>(0));
  EXPECT_NE(test_threadid(), wm_->GetCurrentThreadId());
  WorkerThreadManager::ThreadId free_id = test_threadid();
  Reset();
  EXPECT_TRUE(!test_threadid());

  wm_->RunClosureInPool(FROM_HERE, pool, NewTestRun(),
                        WorkerThreadManager::PRIORITY_LOW);
  WaitTestRun();
  EXPECT_NE(test_threadid(), static_cast<WorkerThreadManager::ThreadId>(0));
  EXPECT_NE(test_threadid(), wm_->GetCurrentThreadId());
  EXPECT_NE(test_threadid(), free_id);
  WorkerThreadManager::ThreadId pool_id = test_threadid();
  Reset();
  EXPECT_TRUE(!test_threadid());
  EXPECT_TRUE(!num_test_threadid());
  const int kNumTestThreadHandle = 100;
  for (int i = 0; i < kNumTestThreadHandle; ++i) {
    wm_->RunClosureInPool(FROM_HERE, pool, NewTestThreadId(pool_id),
                          WorkerThreadManager::PRIORITY_LOW);
  }
  WaitTestThreadHandle(kNumTestThreadHandle);
  wm_->Finish();
}

TEST_F(WorkerThreadManagerTest, PeriodicClosure) {
  wm_->Start(1);
  SimpleTimer timer;
  PeriodicClosureId id = wm_->RegisterPeriodicClosure(
      FROM_HERE, 100, NewPeriodicRun());
  WaitTestPeriodicRun(2);
  wm_->UnregisterPeriodicClosure(id);
  wm_->Finish();
  EXPECT_GE(timer.GetInMs(), 200);
}

TEST_F(WorkerThreadManagerTest, DescriptorReadable) {
  wm_->Start(1);
  int socks[2];
  PCHECK(OpenSocketPairForTest(socks) == 0);
  TestReadContext tc(socks[0], 0.0);
  ScopedSocket s(socks[1]);
  wm_->RunClosure(FROM_HERE, NewTestDescriptorRead(&tc),
                  WorkerThreadManager::PRIORITY_LOW);
  WaitTestRead(&tc, 0);
  {
    AutoLock lock(&mu_);
    EXPECT_EQ(0, tc.num_read_);
    EXPECT_TRUE(tc.d_ != nullptr);
  }
  char buf[1] = { 42 };
  EXPECT_EQ(1, s.Write(buf, 1));
  WaitTestRead(&tc, 1);
  {
    AutoLock lock(&mu_);
    EXPECT_EQ(1, tc.num_read_);
    EXPECT_TRUE(tc.d_ != nullptr);
  }
  s.Close();
  WaitTestReadFinish(&tc);
  {
    AutoLock lock(&mu_);
    EXPECT_EQ(2, tc.num_read_);
    EXPECT_TRUE(tc.d_ == nullptr);
  }
  wm_->Finish();
}

TEST_F(WorkerThreadManagerTest, DescriptorWritable) {
  wm_->Start(1);
  int socks[2];
  PCHECK(OpenSocketPairForTest(socks) == 0);
  const int kTotalWrite = 8192;
  TestWriteContext tc(socks[1], kTotalWrite);
  ScopedSocket s0(socks[0]);
  ScopedSocket s1(socks[1]);
  wm_->RunClosure(FROM_HERE, NewTestDescriptorWrite(&tc),
                  WorkerThreadManager::PRIORITY_LOW);
  WaitTestWrite(&tc, 1);
  {
    AutoLock lock(&mu_);
    EXPECT_GE(tc.num_write_, 1);
    EXPECT_TRUE(tc.d_ != nullptr);
  }
  char buf[1] = { 42 };
  int total_read = 0;
  for (;;) {
    int n = s0.Read(buf, 1);
    if (n == 0) {
      break;
    } else if (n < 0) {
      PLOG(ERROR) << "read " << n;
      break;
    }
    EXPECT_EQ(1, n);
    total_read += n;
  }
  WaitTestWriteFinish(&tc);
  {
    AutoLock lock(&mu_);
    EXPECT_TRUE(tc.d_ == nullptr);
    EXPECT_EQ(kTotalWrite, tc.num_write_);
    EXPECT_EQ(kTotalWrite, total_read);
  }
  s1.Close();
  wm_->Finish();
}

TEST_F(WorkerThreadManagerTest, DescriptorTimeout) {
  wm_->Start(1);
  int socks[2];
  PCHECK(OpenSocketPairForTest(socks) == 0);
  TestReadContext tc(socks[0], 0.5);
  ScopedSocket s(socks[1]);
  wm_->RunClosure(FROM_HERE, NewTestDescriptorRead(&tc),
                  WorkerThreadManager::PRIORITY_LOW);
  WaitTestRead(&tc, 0);
  {
    AutoLock lock(&mu_);
    EXPECT_EQ(0, tc.num_read_);
    EXPECT_TRUE(tc.d_ != nullptr);
  }
  char buf[1] = { 42 };
  EXPECT_EQ(1, s.Write(buf, 1));
  WaitTestRead(&tc, 1);
  {
    AutoLock lock(&mu_);
    EXPECT_EQ(1, tc.num_read_);
    EXPECT_FALSE(tc.timeout_called_);
    EXPECT_TRUE(tc.d_ != nullptr);
  }
  WaitTestReadFinish(&tc);
  {
    AutoLock lock(&mu_);
    EXPECT_EQ(1, tc.num_read_);
    EXPECT_TRUE(tc.timeout_called_);
    EXPECT_TRUE(tc.d_ == nullptr);
  }
  wm_->Finish();
}

}  // namespace devtools_goma
