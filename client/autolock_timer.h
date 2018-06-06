// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_AUTOLOCK_TIMER_H_
#define DEVTOOLS_GOMA_CLIENT_AUTOLOCK_TIMER_H_

#include <memory>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include "basictypes.h"
#include "lockhelper.h"
#include "simple_timer.h"

namespace devtools_goma {

class AutoLockStat {
 public:
  explicit AutoLockStat(const char* auto_lock_name)
      : name(auto_lock_name),
        count_(0),
        total_wait_time_ns_(0), max_wait_time_ns_(0),
        total_hold_time_ns_(0), max_hold_time_ns_(0) {}
  const char* name;

  void GetStats(int* count,
                int64_t* total_wait_time_ns,
                int64_t* max_wait_time_ns,
                int64_t* total_hold_time_ns,
                int64_t* max_hold_time_ns) {
    AutoFastLock lock(&lock_);
    *count = count_;
    *total_wait_time_ns = total_wait_time_ns_;
    *max_wait_time_ns = max_wait_time_ns_;
    *total_hold_time_ns = total_hold_time_ns_;
    *max_hold_time_ns = max_hold_time_ns_;
  }

  void UpdateWaitTime(int64_t wait_time_ns) {
    AutoFastLock lock(&lock_);
    ++count_;
    total_wait_time_ns_ += wait_time_ns;
    if (wait_time_ns > max_wait_time_ns_)
      max_wait_time_ns_ = wait_time_ns;
  }

  void UpdateHoldTime(int64_t hold_time_ns) {
    AutoFastLock lock(&lock_);
    total_hold_time_ns_ += hold_time_ns;
    if (hold_time_ns > max_hold_time_ns_)
      max_hold_time_ns_ = hold_time_ns;
  }

 private:
  FastLock lock_;
  int count_ GUARDED_BY(lock_);
  int64_t total_wait_time_ns_ GUARDED_BY(lock_);
  int64_t max_wait_time_ns_ GUARDED_BY(lock_);
  int64_t total_hold_time_ns_ GUARDED_BY(lock_);
  int64_t max_hold_time_ns_ GUARDED_BY(lock_);

  DISALLOW_COPY_AND_ASSIGN(AutoLockStat);
};

class AutoLockStats {
 public:
  AutoLockStats() {}

  // Return initialized AutoLockStat for |name|.
  // |name| should be string literal (it must not be released).
  // This should be called once in a location.
  // e.g.
  //   static AutoLockStat* stat = g_auto_lock_stats_->NewStat(name);
  //
  AutoLockStat* NewStat(const char* name);

  void Report(std::ostringstream* ss,
              const std::unordered_set<std::string>& skip_names);
  void TextReport(std::ostringstream* ss);

 private:
  mutable Lock mu_;
  std::vector<std::unique_ptr<AutoLockStat>> stats_ GUARDED_BY(mu_);
  DISALLOW_COPY_AND_ASSIGN(AutoLockStats);
};

extern AutoLockStats* g_auto_lock_stats;

class MutexAcquireStrategy {
 public:
  static void Acquire(Lock* lock) EXCLUSIVE_LOCK_FUNCTION(lock) {
    lock->Acquire();
  }

  static void Release(Lock* lock) UNLOCK_FUNCTION(lock) {
    lock->Release();
  }

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(MutexAcquireStrategy);
};

class ReadWriteLockAcquireSharedStrategy {
 public:
  static void Acquire(ReadWriteLock* lock) SHARED_LOCK_FUNCTION(lock) {
    lock->AcquireShared();
  }

  static void Release(ReadWriteLock* lock) UNLOCK_FUNCTION(lock) {
    lock->ReleaseShared();
  }

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(ReadWriteLockAcquireSharedStrategy);
};

class ReadWriteLockAcquireExclusiveStrategy {
 public:
  static void Acquire(ReadWriteLock* lock) EXCLUSIVE_LOCK_FUNCTION(lock) {
    lock->AcquireExclusive();
  }

  static void Release(ReadWriteLock* lock) UNLOCK_FUNCTION(lock) {
    lock->ReleaseExclusive();
  }

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(ReadWriteLockAcquireExclusiveStrategy);
};

template<typename LockType, typename LockAcquireStrategy>
class AutoLockTimerBase {
 public:
  // Auto lock on |lock| with stats of |name|.
  // |name| must be string literal. It must not be deleted.
  // If |statp| is NULL, it doesn't collect stats (i.e. it works as
  // almost same as AutoLock).
  // If |statp| is not NULL, it holds stats for lock wait/hold time.
  AutoLockTimerBase(LockType* lock, AutoLockStat* statp)
      : lock_(lock), stat_(nullptr), timer_(SimpleTimer::NO_START) {
    if (statp)
      timer_.Start();
    LockAcquireStrategy::Acquire(lock_);
    if (statp) {
      stat_ = statp;
      stat_->UpdateWaitTime(timer_.GetInNanoSeconds());
      timer_.Start();
    }
  }

  ~AutoLockTimerBase() {
    if (stat_) {
      stat_->UpdateHoldTime(timer_.GetInNanoSeconds());
    }
    LockAcquireStrategy::Release(lock_);
  }

 private:
  LockType* lock_;
  AutoLockStat* stat_;
  SimpleTimer timer_;
  DISALLOW_COPY_AND_ASSIGN(AutoLockTimerBase);
};

class SCOPED_LOCKABLE AutoLockTimer
    : private AutoLockTimerBase<Lock, MutexAcquireStrategy> {
 public:
  AutoLockTimer(Lock* lock,
                AutoLockStat* statp) EXCLUSIVE_LOCK_FUNCTION(lock)
      : AutoLockTimerBase(lock, statp) {
  }

  ~AutoLockTimer() UNLOCK_FUNCTION() {
  }
};

class SCOPED_LOCKABLE AutoReadWriteLockSharedTimer
    : private AutoLockTimerBase<ReadWriteLock,
                                ReadWriteLockAcquireSharedStrategy> {
 public:
  AutoReadWriteLockSharedTimer(ReadWriteLock* lock,
                               AutoLockStat* statp) SHARED_LOCK_FUNCTION(lock)
      : AutoLockTimerBase(lock, statp) {
  }

  ~AutoReadWriteLockSharedTimer() UNLOCK_FUNCTION() {}
};

class SCOPED_LOCKABLE AutoReadWriteLockExclusiveTimer
    : private AutoLockTimerBase<ReadWriteLock,
                                ReadWriteLockAcquireExclusiveStrategy> {
 public:
  AutoReadWriteLockExclusiveTimer(ReadWriteLock* lock,
                                  AutoLockStat* statp)
      EXCLUSIVE_LOCK_FUNCTION(lock)
      : AutoLockTimerBase(lock, statp) {
  }

  ~AutoReadWriteLockExclusiveTimer() UNLOCK_FUNCTION() {}
};

#define GOMA_AUTOLOCK_TIMER_STRINGFY(i) #i
#define GOMA_AUTOLOCK_TIMER_STR(i) GOMA_AUTOLOCK_TIMER_STRINGFY(i)
// #define NO_AUTOLOCK_STAT
#ifdef NO_AUTOLOCK_STAT
#define AUTOLOCK(lock, mu) AutoLock lock(mu)
#define AUTOLOCK_WITH_STAT(lock, mu, statp) AutoLock lock(mu)
#define AUTO_SHARED_LOCK(lock, rwlock) AutoSharedLock lock(rwlock)
#define AUTO_EXCLUSIVE_LOCK(lock, rwlock) AutoExclusiveLock lock(rwlock)
#else
#define AUTOLOCK(lock, mu)                                              \
  static AutoLockStat* auto_lock_stat_for_the_source_location =         \
      g_auto_lock_stats ? g_auto_lock_stats->NewStat(                   \
          __FILE__ ":" GOMA_AUTOLOCK_TIMER_STR(__LINE__) "(" #mu ")") : \
      NULL;                                                             \
  AutoLockTimer lock(mu, auto_lock_stat_for_the_source_location);

#define AUTOLOCK_WITH_STAT(lock, mu, statp)                             \
  AutoLockTimer lock(mu, statp);
#define AUTO_SHARED_LOCK(lock, rwlock)                                  \
  static AutoLockStat* auto_lock_stat_for_the_source_location =         \
      g_auto_lock_stats ? g_auto_lock_stats->NewStat(                   \
          __FILE__ ":" GOMA_AUTOLOCK_TIMER_STR(__LINE__) "(" #rwlock ":r)") : \
      NULL;                                                             \
  AutoReadWriteLockSharedTimer lock(                                    \
      rwlock, auto_lock_stat_for_the_source_location);
#define AUTO_EXCLUSIVE_LOCK(lock, rwlock)                               \
  static AutoLockStat* auto_lock_stat_for_the_source_location =         \
      g_auto_lock_stats ? g_auto_lock_stats->NewStat(                   \
          __FILE__ ":" GOMA_AUTOLOCK_TIMER_STR(__LINE__) "(" #rwlock ":w)") : \
      NULL;                                                             \
  AutoReadWriteLockExclusiveTimer lock(                                 \
      rwlock, auto_lock_stat_for_the_source_location);
#endif  // NO_AUTOLOCK_STAT

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_AUTOLOCK_TIMER_H_
