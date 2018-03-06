// Copyright 2010 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_BASE_LOCKHELPER_H_
#define DEVTOOLS_GOMA_BASE_LOCKHELPER_H_

#include "absl/base/thread_annotations.h"
#include "basictypes.h"

#ifdef __MACH__
# include <libkern/OSAtomic.h>
#endif

#ifdef _WIN32
# include "config_win.h"
typedef CRITICAL_SECTION OSLockType;
typedef SRWLOCK OSRWLockType;
#else
# include <pthread.h>
# include <errno.h>
typedef pthread_mutex_t OSLockType;
typedef pthread_rwlock_t OSRWLockType;
#endif

namespace devtools_goma {

// NOTE: capability based thread safety analysis is not working well
// for shared lock. So, let me keep using older style thread safety analysis.

class LOCKABLE Lock {
 public:
  Lock();
  ~Lock();

  // If the lock is not held, take it and return true.  If the lock is already
  // held by something else, immediately return false.
  bool Try() EXCLUSIVE_TRYLOCK_FUNCTION(true);

  // Take the lock, blocking until it is available if necessary.
  void Acquire() EXCLUSIVE_LOCK_FUNCTION();

  // Release the lock.  This must only be called by the lock's holder: after
  // a successful call to Try, or a call to Lock.
  void Release() UNLOCK_FUNCTION();

 private:
  friend class ConditionVariable;
#ifdef _WIN32
  friend class WinVistaCondVar;
#endif
  OSLockType os_lock_;
  DISALLOW_COPY_AND_ASSIGN(Lock);
};

#ifdef __MACH__

// In Mac, pthread becomes very slow when contention happens.
// Using OSSpinLock improves performance for short lock holding.
class LOCKABLE FastLock {
 public:
  FastLock(const FastLock&) = delete;
  FastLock& operator=(const FastLock&) = delete;

  FastLock() : lock_(OS_SPINLOCK_INIT) {}

  void Acquire() EXCLUSIVE_LOCK_FUNCTION() {
    OSSpinLockLock(&lock_);
  }

  void Release() UNLOCK_FUNCTION() {
    OSSpinLockUnlock(&lock_);
  }
 private:
  // TODO: Use os_unfair_lock if available.
  // OSSpinLock is deprecated in 10.12.
  OSSpinLock lock_;
};

#else

using FastLock = Lock;

#endif

// ReadWriteLock provides readers-writer lock.
class LOCKABLE ReadWriteLock {
 public:
  ReadWriteLock();
  ~ReadWriteLock();

  void AcquireShared() SHARED_LOCK_FUNCTION();
  void ReleaseShared() UNLOCK_FUNCTION();

  void AcquireExclusive() EXCLUSIVE_LOCK_FUNCTION();
  void ReleaseExclusive() UNLOCK_FUNCTION();

 private:
#ifdef _WIN32
  SRWLOCK srw_lock_;
#else
  OSRWLockType os_rwlock_;
#endif
  DISALLOW_COPY_AND_ASSIGN(ReadWriteLock);
};

class SCOPED_LOCKABLE AutoLock {
 public:
  // Does not take ownership of |lock|, which must refer to a valid Lock
  // that outlives this object.
  explicit AutoLock(Lock* lock) EXCLUSIVE_LOCK_FUNCTION(lock)
      : lock_(lock) {
    lock_->Acquire();
  }

  ~AutoLock() UNLOCK_FUNCTION() {
    lock_->Release();
  }

 private:
  Lock* lock_;
  DISALLOW_COPY_AND_ASSIGN(AutoLock);
};

class SCOPED_LOCKABLE AutoFastLock {
 public:
  AutoFastLock(const AutoFastLock&) = delete;
  AutoFastLock& operator=(const AutoFastLock&) = delete;

  // Does not take ownership of |lock|, which must refer to a valid FastLock
  // that outlives this object.
  explicit AutoFastLock(FastLock* lock) EXCLUSIVE_LOCK_FUNCTION(lock)
      : lock_(lock) {
    lock_->Acquire();
  }

  ~AutoFastLock() UNLOCK_FUNCTION() {
    lock_->Release();
  }

 private:
  FastLock* lock_;
};

class SCOPED_LOCKABLE AutoExclusiveLock {
 public:
  // Does not take ownership of |lock|, which must refer to a valid
  // ReadWriteLock that outlives this object.
  explicit AutoExclusiveLock(ReadWriteLock* lock)
      EXCLUSIVE_LOCK_FUNCTION(lock) : lock_(lock) {
    lock_->AcquireExclusive();
  }

  ~AutoExclusiveLock() UNLOCK_FUNCTION() {
    lock_->ReleaseExclusive();
  }

 private:
  ReadWriteLock* lock_;
  DISALLOW_COPY_AND_ASSIGN(AutoExclusiveLock);
};

class SCOPED_LOCKABLE AutoSharedLock {
 public:
  // Does not take ownership of |lock|, which must refer to a valid
  // ReadWriteLock that outlives this object.
  explicit AutoSharedLock(ReadWriteLock* lock) SHARED_LOCK_FUNCTION(lock)
      : lock_(lock) {
    lock_->AcquireShared();
  }

  ~AutoSharedLock() UNLOCK_FUNCTION() {
    lock_->ReleaseShared();
  }

 private:
  ReadWriteLock* lock_;
  DISALLOW_COPY_AND_ASSIGN(AutoSharedLock);
};

// POSIX conditional variable
class ConditionVariable {
 public:
  ConditionVariable();
  ~ConditionVariable();

  void Wait(Lock* lock);
  void Signal();
  void Broadcast();

 private:
#ifdef _WIN32
  CONDITION_VARIABLE cv_;
#else  // Assume POSIX
  pthread_cond_t condition_;
#endif
  DISALLOW_COPY_AND_ASSIGN(ConditionVariable);
};

}  // namespace devtools_goma
#endif  // DEVTOOLS_GOMA_BASE_LOCKHELPER_H_
