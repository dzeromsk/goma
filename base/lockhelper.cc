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

bool Lock::Try() const {
  if (::TryEnterCriticalSection(&os_lock_) != FALSE) {
    return true;
  }
  return false;
}

void Lock::Acquire() const {
  ::EnterCriticalSection(&os_lock_);
}

void Lock::Release() const {
  ::LeaveCriticalSection(&os_lock_);
}

ReadWriteLock::ReadWriteLock() {
  ::InitializeSRWLock(&srw_lock_);
}

ReadWriteLock::~ReadWriteLock() {
}

void ReadWriteLock::AcquireShared() const {
  ::AcquireSRWLockShared(&srw_lock_);
}

void ReadWriteLock::ReleaseShared() const {
  ::ReleaseSRWLockShared(&srw_lock_);
}

void ReadWriteLock::AcquireExclusive() const {
  ::AcquireSRWLockExclusive(&srw_lock_);
}

void ReadWriteLock::ReleaseExclusive() const {
  ::ReleaseSRWLockExclusive(&srw_lock_);
}

ConditionVariable::ConditionVariable(Lock* user_lock)
    : user_lock_(user_lock) {
  ::InitializeConditionVariable(&cv_);
  DCHECK(user_lock);
}

ConditionVariable::~ConditionVariable() {
}

void ConditionVariable::Wait() {
  CRITICAL_SECTION* cs = &user_lock_->os_lock_;

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

bool Lock::Try() const {
  return (pthread_mutex_trylock(&os_lock_) == 0);
}

void Lock::Acquire() const {
  pthread_mutex_lock(&os_lock_);
}

void Lock::Release() const {
  pthread_mutex_unlock(&os_lock_);
}

ReadWriteLock::ReadWriteLock() {
  pthread_rwlock_init(&os_rwlock_, nullptr);
}

ReadWriteLock::~ReadWriteLock() {
  pthread_rwlock_destroy(&os_rwlock_);
}

void ReadWriteLock::AcquireShared() const {
  pthread_rwlock_rdlock(&os_rwlock_);
}

void ReadWriteLock::ReleaseShared() const {
  pthread_rwlock_unlock(&os_rwlock_);
}

void ReadWriteLock::AcquireExclusive() const {
  pthread_rwlock_wrlock(&os_rwlock_);
}

void ReadWriteLock::ReleaseExclusive() const {
  pthread_rwlock_unlock(&os_rwlock_);
}

ConditionVariable::ConditionVariable(Lock* user_lock)
    : user_mutex_(&user_lock->os_lock_) {
  pthread_cond_init(&condition_, nullptr);
}

ConditionVariable::~ConditionVariable() {
  pthread_cond_destroy(&condition_);
}

void ConditionVariable::Wait() {
  pthread_cond_wait(&condition_, user_mutex_);
}

void ConditionVariable::Signal() {
  pthread_cond_signal(&condition_);
}

void ConditionVariable::Broadcast() {
  pthread_cond_broadcast(&condition_);
}
#endif

}  // namespace devtools_goma
