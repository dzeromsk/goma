// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>

#include <vector>
#include <memory>

#include <gtest/gtest.h>

#include "lockhelper.h"
#include "platform_thread.h"

namespace devtools_goma {

// Basic test to make sure that Acquire()/Release()/Try() don't crash

class BasicLockTestThread : public PlatformThread::Delegate {
 public:
  explicit BasicLockTestThread(Lock* lock) : lock_(lock), acquired_(0) {
  }

  void ThreadMain() override {
    for (int i = 0; i < 10; i++) {
      lock_->Acquire();
      acquired_++;
      lock_->Release();
    }
    for (int i = 0; i < 10; i++) {
      lock_->Acquire();
      acquired_++;
      PlatformThread::Sleep(rand() % 20);
      lock_->Release();
    }
    for (int i = 0; i < 10; i++) {
      if (lock_->Try()) {
        acquired_++;
        PlatformThread::Sleep(rand() % 20);
        lock_->Release();
      }
    }
  }

  int acquired() const { return acquired_; }

 private:
  Lock* lock_;
  int acquired_;

  DISALLOW_COPY_AND_ASSIGN(BasicLockTestThread);
};

bool BasicLockTest() {
  Lock lock;
  BasicLockTestThread thread(&lock);
  PlatformThreadHandle handle = kNullThreadHandle;

  EXPECT_TRUE(PlatformThread::Create(&thread, &handle));

  int acquired = 0;
  for (int i = 0; i < 5; i++) {
    lock.Acquire();
    acquired++;
    lock.Release();
  }
  for (int i = 0; i < 10; i++) {
    lock.Acquire();
    acquired++;
    PlatformThread::Sleep(rand() % 20);
    lock.Release();
  }
  for (int i = 0; i < 10; i++) {
    if (lock.Try()) {
      acquired++;
      PlatformThread::Sleep(rand() % 20);
      lock.Release();
    }
  }
  for (int i = 0; i < 5; i++) {
    lock.Acquire();
    acquired++;
    PlatformThread::Sleep(rand() % 20);
    lock.Release();
  }

  PlatformThread::Join(handle);

  EXPECT_GE(acquired, 20);
  EXPECT_GE(thread.acquired(), 20);

  return true;
}

// Test that Try() works as expected -------------------------------------------

class TryLockTestThread : public PlatformThread::Delegate {
 public:
  explicit TryLockTestThread(Lock* lock) : lock_(lock), got_lock_(false) {
  }

  void ThreadMain() override {
    if (lock_->Try()) {
      got_lock_ = true;
      lock_->Release();
    } else {
      got_lock_ = false;
    }
  }

  bool got_lock() const { return got_lock_; }

 private:
  Lock* lock_;
  bool got_lock_;

  DISALLOW_COPY_AND_ASSIGN(TryLockTestThread);
};

bool TryLockTest() {
  Lock lock;

  if (lock.Try()) {
    // We now have the lock....
    // This thread will not be able to get the lock.
    TryLockTestThread thread(&lock);
    PlatformThreadHandle handle = kNullThreadHandle;

    EXPECT_TRUE(PlatformThread::Create(&thread, &handle));

    PlatformThread::Join(handle);

    EXPECT_FALSE(thread.got_lock());

    lock.Release();
  } else {
    EXPECT_TRUE(false) << "taking lock failed";
  }

  // This thread will....
  {
    TryLockTestThread thread(&lock);
    PlatformThreadHandle handle = kNullThreadHandle;

    EXPECT_TRUE(PlatformThread::Create(&thread, &handle));

    PlatformThread::Join(handle);

    EXPECT_TRUE(thread.got_lock());
    // But it released it....
    if (lock.Try()) {
      lock.Release();
    } else {
      EXPECT_TRUE(false) << "taking lock failed";
    }
  }

  return true;
}

// Tests that locks actually exclude -------------------------------------------

class MutexLockTestThread : public PlatformThread::Delegate {
 public:
  MutexLockTestThread(Lock* lock, int* value) : lock_(lock), value_(value) {}

  // Static helper which can also be called from the main thread.
  static void DoStuff(Lock* lock, int* value) {
    for (int i = 0; i < 40; i++) {
      lock->Acquire();
      int v = *value;
      PlatformThread::Sleep(rand() % 10);
      *value = v + 1;
      lock->Release();
    }
  }

  void ThreadMain() override {
    DoStuff(lock_, value_);
  }

 private:
  Lock* lock_;
  int* value_;

  DISALLOW_COPY_AND_ASSIGN(MutexLockTestThread);
};

bool MutexTwoThreads() {
  Lock lock;
  int value = 0;

  MutexLockTestThread thread(&lock, &value);
  PlatformThreadHandle handle = kNullThreadHandle;

  EXPECT_TRUE(PlatformThread::Create(&thread, &handle));

  MutexLockTestThread::DoStuff(&lock, &value);

  PlatformThread::Join(handle);

  EXPECT_EQ(2 * 40, value);
  return true;
}

bool MutexFourThreads() {
  Lock lock;
  int value = 0;

  MutexLockTestThread thread1(&lock, &value);
  MutexLockTestThread thread2(&lock, &value);
  MutexLockTestThread thread3(&lock, &value);
  PlatformThreadHandle handle1 = kNullThreadHandle;
  PlatformThreadHandle handle2 = kNullThreadHandle;
  PlatformThreadHandle handle3 = kNullThreadHandle;

  EXPECT_TRUE(PlatformThread::Create(&thread1, &handle1));
  EXPECT_TRUE(PlatformThread::Create(&thread2, &handle2));
  EXPECT_TRUE(PlatformThread::Create(&thread3, &handle3));

  MutexLockTestThread::DoStuff(&lock, &value);

  PlatformThread::Join(handle1);
  PlatformThread::Join(handle2);
  PlatformThread::Join(handle3);

  EXPECT_EQ(4 * 40, value);
  return true;
}

class ConditionVariableTestThread : public PlatformThread::Delegate {
 public:
  struct Data {
    Data() : result{}, index(0), count(0) {}

    char result[10];
    int index;
    int count;
  };

  ConditionVariableTestThread(int id,
                              Lock* lock,
                              ConditionVariable* cond,
                              Data* data)
      : id_(id), lock_(lock), cond_(cond), data_(data) {
  }

  void ThreadMain() override {
    if (id_ == 1) {
      Count1();
    } else {
      Count2();
    }
  }

 private:
  // Write numbers 1-3 and 7-9 as permitted by Count2()
  void Count1() {
    for (;;) {
      lock_->Acquire();
      cond_->Wait();
      data_->count++;
      // Use EXPECT_TRUE instead of ASSERT_TRUE, since when the condition
      // does not hold, ASSERT_TRUE will cause function exit, it means
      // lock is not released.
      EXPECT_TRUE((0 <= data_->index && data_->index < 3) ||
                  (6 <= data_->index && data_->index < 9))
          << data_->index;
      data_->result[data_->index++] = static_cast<char>('0' + data_->count);
      int c = data_->count;
      lock_->Release();
      if (c >= 9) {
        return;
      }
    }
  }

  // Write numbers 4-6 in Count2 thread.
  void Count2() {
    for (;;) {
      lock_->Acquire();
      if (data_->count < 3 || 6 <= data_->count) {
        cond_->Signal();
      } else {
        data_->count++;
        // Use EXPECT_TRUE instead of ASSERT_TRUE, since when the condition
        // does not hold, ASSERT_TRUE will cause function exit, it means
        // lock is not released.
        EXPECT_TRUE(3 <= data_->index && data_->index < 6) << data_->index;
        data_->result[data_->index++] = static_cast<char>('0' + data_->count);
      }
      int c = data_->count;
      lock_->Release();
      if (c >= 9) {
        return;
      }
    }
  }

  const int id_;

  Lock* lock_;
  ConditionVariable* cond_;
  Data* data_;

  DISALLOW_COPY_AND_ASSIGN(ConditionVariableTestThread);
};

bool ConditionVar() {
  Lock lock;
  ConditionVariable cond(&lock);
  ConditionVariableTestThread::Data data;

  std::unique_ptr<ConditionVariableTestThread> threads[2];
  PlatformThreadHandle handles[2];
  for (int i = 0; i < 2; ++i) {
    threads[i].reset(new ConditionVariableTestThread(i, &lock, &cond, &data));
    handles[i] = kNullThreadHandle;
  }

  EXPECT_TRUE(PlatformThread::Create(threads[0].get(), &handles[0]));
  EXPECT_TRUE(PlatformThread::Create(threads[1].get(), &handles[1]));

  PlatformThread::Join(handles[0]);
  PlatformThread::Join(handles[1]);

  EXPECT_STREQ("123456789", data.result);
  return true;
}

// ReadwriteLock BasicTest  ---------------------------------

class ReadWriteLockBasicTestThread : public PlatformThread::Delegate {
 public:
  ReadWriteLockBasicTestThread(ReadWriteLock* lock, int* num)
      : lock_(lock),
        num_(num) {
  }

  void ThreadMain() override {
    for (int i = 0; i < 10; i++) {
      lock_->AcquireExclusive();
      *num_ += 1;
      lock_->ReleaseExclusive();
    }
    for (int i = 0; i < 10; i++) {
      AutoSharedLock shared_autolock(lock_);
      int num1 = *num_;
      PlatformThread::Sleep(rand() % 20);
      int num2 = *num_;
      EXPECT_EQ(num1, num2);
    }
    for (int i = 0; i < 10; i++) {
      AutoExclusiveLock exclusive_autolock(lock_);
      *num_ += 1;
      PlatformThread::Sleep(rand() % 20);
    }
  }

 private:
  ReadWriteLock* lock_;
  int* num_;

  DISALLOW_COPY_AND_ASSIGN(ReadWriteLockBasicTestThread);
};

bool ReadWriteLockBasicTest() {
  ReadWriteLock lock;
  int num = 0;

  ReadWriteLockBasicTestThread thread1(&lock, &num);
  ReadWriteLockBasicTestThread thread2(&lock, &num);
  PlatformThreadHandle handle1 = kNullThreadHandle;
  PlatformThreadHandle handle2 = kNullThreadHandle;

  EXPECT_TRUE(PlatformThread::Create(&thread1, &handle1));
  EXPECT_TRUE(PlatformThread::Create(&thread2, &handle2));

  PlatformThread::Join(handle1);
  PlatformThread::Join(handle2);

  EXPECT_EQ(40, num);
  return true;
}

// AcquireExclusive  -------------------------------------------

class ReadWriteLockAcquireExclusiveThread : public PlatformThread::Delegate {
 public:
  ReadWriteLockAcquireExclusiveThread(ReadWriteLock* lock, int* num) :
      lock_(lock),
      num_(num),
      started_(false) {
  }

  void ThreadMain() override {
    SetStarted();
    AutoExclusiveLock autolock(lock_);
    *num_ += 1;
  }

  void SetStarted() {
    AutoLock lock(&mu_);
    started_ = true;
  }

  bool started() const {
    AutoLock lock(&mu_);
    return started_;
  }

 private:
  ReadWriteLock* lock_;
  int* num_;

  mutable Lock mu_;
  bool started_;

  DISALLOW_COPY_AND_ASSIGN(ReadWriteLockAcquireExclusiveThread);
};

bool ReadWriteLockAcquireExclusiveTest1() {
  ReadWriteLock lock;
  int num = 0;

  lock.AcquireExclusive();

  // This thread will be blocked by |lock|.
  ReadWriteLockAcquireExclusiveThread thread(&lock, &num);
  PlatformThreadHandle handle = kNullThreadHandle;
  EXPECT_TRUE(PlatformThread::Create(&thread, &handle));

  // Wait until |thread| is really started.
  while (!thread.started()) {
    PlatformThread::Sleep(1);
  }

  // Try to run the thread.
  EXPECT_EQ(num, 0);
  num += 1;
  EXPECT_EQ(num, 1);

  lock.ReleaseExclusive();

  // Now the thread can go on.

  PlatformThread::Join(handle);
  EXPECT_EQ(num, 2);

  return true;
}

bool ReadWriteLockAcquireExclusiveTest2() {
  ReadWriteLock lock;
  int num = 0;

  lock.AcquireShared();

  // This thread will be blocked by |lock|.
  ReadWriteLockAcquireExclusiveThread thread(&lock, &num);
  PlatformThreadHandle handle = kNullThreadHandle;
  EXPECT_TRUE(PlatformThread::Create(&thread, &handle));

  // Wait until |thread| is really started.
  while (!thread.started()) {
    PlatformThread::Sleep(1);
  }

  EXPECT_EQ(num, 0);
  lock.ReleaseShared();

  // Now the thread can go on.

  PlatformThread::Join(handle);
  EXPECT_EQ(num, 1);

  return true;
}

// AcquireShared  -------------------------------------------

class ReadWriteLockAcquireSharedThread : public PlatformThread::Delegate {
 public:
  ReadWriteLockAcquireSharedThread(ReadWriteLock* lock, int* num) :
      lock_(lock),
      num_(num),
      gotten_num_(0),
      started_(false) {
  }

  void ThreadMain() override {
    SetStarted();
    AutoSharedLock shared_lock(lock_);
    gotten_num_ = *num_;
  }

  int gotten_num() const {
    return gotten_num_;
  }

  void SetStarted() {
    AutoLock lock(&mu_);
    started_ = true;
  }

  bool started() const {
    AutoLock lock(&mu_);
    return started_;
  }

 private:
  ReadWriteLock* lock_;
  int* num_;
  int gotten_num_;

  mutable Lock mu_;
  bool started_;

  DISALLOW_COPY_AND_ASSIGN(ReadWriteLockAcquireSharedThread);
};


bool ReadWriteLockAcquireSharedWithExclusiveLockTest() {
  ReadWriteLock lock;
  int num = 0;

  lock.AcquireExclusive();

  ReadWriteLockAcquireSharedThread thread(&lock, &num);
  PlatformThreadHandle handle = kNullThreadHandle;
  EXPECT_TRUE(PlatformThread::Create(&thread, &handle));

  // Wait until |thread| is really started.
  while (!thread.started()) {
    PlatformThread::Sleep(1);
  }

  EXPECT_EQ(num, 0);
  num += 1;
  EXPECT_EQ(num, 1);

  lock.ReleaseExclusive();

  PlatformThread::Join(handle);
  EXPECT_EQ(1, thread.gotten_num());

  return true;
}

bool ReadWriteLockAcquireSharedWithSharedLockTest() {
  ReadWriteLock lock;
  int num = 1;

  lock.AcquireShared();
  ReadWriteLockAcquireSharedThread thread(&lock, &num);
  PlatformThreadHandle handle = kNullThreadHandle;

  // Before releasing |lock|, the thread can be finished.
  EXPECT_TRUE(PlatformThread::Create(&thread, &handle));
  PlatformThread::Join(handle);
  EXPECT_EQ(1, thread.gotten_num());

  lock.ReleaseShared();

  return true;
}

template<typename LockType, typename AutoLockType>
class IncrementThread : public PlatformThread::Delegate {
 public:
  IncrementThread(LockType* lock, int* x, int loop_num)
    : lock_(lock), x_(x), loop_num_(loop_num) {
  }

  void ThreadMain() override {
    for (int i = 0; i < loop_num_; ++i) {
      AutoLockType lock(lock_);
      ++*x_;
    }
  }

 private:
  LockType* lock_;
  int* x_;
  const int loop_num_;
};

using FastIncrement = IncrementThread<FastLock, AutoFastLock>;
using NormalIncrement = IncrementThread<Lock, AutoLock>;

}  // namespace devtools_goma

TEST(LockTest, Basic) {
  ASSERT_TRUE(devtools_goma::BasicLockTest());
}

TEST(LockTest, TryLock) {
  ASSERT_TRUE(devtools_goma::TryLockTest());
}

TEST(LockTest, Mutex) {
  ASSERT_TRUE(devtools_goma::MutexTwoThreads());
  ASSERT_TRUE(devtools_goma::MutexFourThreads());
}

TEST(LockTest, ConditionVar) {
  ASSERT_TRUE(devtools_goma::ConditionVar());
}

TEST(ReadWriteLockTest, ReadWriteLockBasic) {
  ASSERT_TRUE(devtools_goma::ReadWriteLockBasicTest());
}

TEST(ReadWriteLockTest, ReadWriteLockAcquireExclusive) {
  ASSERT_TRUE(devtools_goma::ReadWriteLockAcquireExclusiveTest1());
  ASSERT_TRUE(devtools_goma::ReadWriteLockAcquireExclusiveTest2());
}

TEST(ReadWriteLockTest, ReadWriteLockAcquireShared) {
  ASSERT_TRUE(devtools_goma::ReadWriteLockAcquireSharedWithExclusiveLockTest());
  ASSERT_TRUE(devtools_goma::ReadWriteLockAcquireSharedWithSharedLockTest());
}

TEST(LockhelperTest, FastLockBenchmark) {
  const int thread_num = 8;
  const int loop_num = 100000;
  std::vector<devtools_goma::PlatformThreadHandle> thread_ids(thread_num);
  std::vector<std::unique_ptr<devtools_goma::FastIncrement>> incrementers;
  int x = 0;
  devtools_goma::FastLock lock;

  for (int i = 0; i < thread_num; ++i) {
    incrementers.emplace_back(
        new devtools_goma::FastIncrement(&lock, &x, loop_num));
  }

  for (int i = 0; i < thread_num; ++i) {
    devtools_goma::PlatformThread::Create(
        incrementers[i].get(), &thread_ids[i]);
  }

  for (int i = 0; i < thread_num; ++i) {
    devtools_goma::PlatformThread::Join(thread_ids[i]);
  }

  EXPECT_EQ(x, loop_num * thread_num);
}

TEST(LockhelperTest, NormalLockBenchmark) {
  const int thread_num = 8;
  const int loop_num = 100000;
  std::vector<devtools_goma::PlatformThreadHandle> thread_ids(thread_num);
  std::vector<std::unique_ptr<devtools_goma::NormalIncrement>> incrementers;
  int x = 0;
  devtools_goma::Lock lock;

  for (int i = 0; i < thread_num; ++i) {
    incrementers.emplace_back(
      new devtools_goma::NormalIncrement(&lock, &x, loop_num));
  }

  for (int i = 0; i < thread_num; ++i) {
    devtools_goma::PlatformThread::Create(
      incrementers[i].get(), &thread_ids[i]);
  }

  for (int i = 0; i < thread_num; ++i) {
    devtools_goma::PlatformThread::Join(thread_ids[i]);
  }

  EXPECT_EQ(x, loop_num * thread_num);
}
