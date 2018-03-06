// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "lockhelper.h"

#ifdef _WIN32
# include <stack>
# include <glog/logging.h>
#endif

namespace devtools_goma {
#if defined (_WIN32)

Lock::Lock() {
  // The second parameter is the spin count, for short-held locks it avoid the
  // contending thread from going to sleep which helps performance greatly.
  ::InitializeCriticalSectionAndSpinCount(&os_lock_, 2000);
}

Lock::~Lock() {
  ::DeleteCriticalSection(&os_lock_);
}

bool Lock::Try() {
  if (::TryEnterCriticalSection(&os_lock_) != FALSE) {
    return true;
  }
  return false;
}

void Lock::Acquire() {
  ::EnterCriticalSection(&os_lock_);
}

void Lock::Release() {
  ::LeaveCriticalSection(&os_lock_);
}

ReadWriteLock::ReadWriteLock() {
  ::InitializeSRWLock(&srw_lock_);
}

ReadWriteLock::~ReadWriteLock() {
}

void ReadWriteLock::AcquireShared() {
  ::AcquireSRWLockShared(&srw_lock_);
}

void ReadWriteLock::ReleaseShared() {
  ::ReleaseSRWLockShared(&srw_lock_);
}

void ReadWriteLock::AcquireExclusive() {
  ::AcquireSRWLockExclusive(&srw_lock_);
}

void ReadWriteLock::ReleaseExclusive() {
  ::ReleaseSRWLockExclusive(&srw_lock_);
}

ConditionVariable::ConditionVariable() {
  ::InitializeConditionVariable(&cv_);
}

ConditionVariable::~ConditionVariable() {
}

void ConditionVariable::Wait(Lock* lock) {
  CRITICAL_SECTION* cs = &lock->os_lock_;

  if (FALSE == SleepConditionVariableCS(&cv_, cs, INFINITE)) {
    DCHECK(GetLastError() != WAIT_TIMEOUT);
  }
}

void ConditionVariable::Broadcast() {
  WakeAllConditionVariable(&cv_);
}

void ConditionVariable::Signal() {
  WakeConditionVariable(&cv_);
}

#else

Lock::Lock() {
  pthread_mutex_init(&os_lock_, nullptr);
}

Lock::~Lock() {
  pthread_mutex_destroy(&os_lock_);
}

bool Lock::Try() {
  return (pthread_mutex_trylock(&os_lock_) == 0);
}

void Lock::Acquire() {
  pthread_mutex_lock(&os_lock_);
}

void Lock::Release() {
  pthread_mutex_unlock(&os_lock_);
}

ReadWriteLock::ReadWriteLock() {
  pthread_rwlock_init(&os_rwlock_, nullptr);
}

ReadWriteLock::~ReadWriteLock() {
  pthread_rwlock_destroy(&os_rwlock_);
}

void ReadWriteLock::AcquireShared() {
  pthread_rwlock_rdlock(&os_rwlock_);
}

void ReadWriteLock::ReleaseShared() {
  pthread_rwlock_unlock(&os_rwlock_);
}

void ReadWriteLock::AcquireExclusive() {
  pthread_rwlock_wrlock(&os_rwlock_);
}

void ReadWriteLock::ReleaseExclusive() {
  pthread_rwlock_unlock(&os_rwlock_);
}

ConditionVariable::ConditionVariable() {
  pthread_cond_init(&condition_, nullptr);
}

ConditionVariable::~ConditionVariable() {
  pthread_cond_destroy(&condition_);
}

void ConditionVariable::Wait(Lock* user_lock) {
  pthread_cond_wait(&condition_, &user_lock->os_lock_);
}

void ConditionVariable::Signal() {
  pthread_cond_signal(&condition_);
}

void ConditionVariable::Broadcast() {
  pthread_cond_broadcast(&condition_);
}
#endif

}  // namespace devtools_goma
