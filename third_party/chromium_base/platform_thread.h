/*
 * Copyright 2011 Google Inc. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Simple platform thread implementation used to test our cross-platform locks.
// This is a trimmed down version of Chromium base/threading/platform_thread.h.
// Originated from sfntly.googlecode.com
#ifndef DEVTOOLS_GOMA_BASE_PLATFORM_THREAD_H_
#define DEVTOOLS_GOMA_BASE_PLATFORM_THREAD_H_

#if defined (_WIN32)
#include <windows.h>
#else  // Assume pthread
#include <errno.h>
#include <pthread.h>
#include <time.h>
#endif  // if defined (_WIN32)

#include <stdint.h>

namespace devtools_goma {

#if defined (_WIN32)
typedef HANDLE PlatformThreadHandle;
typedef DWORD PlatformThreadId;
const PlatformThreadHandle kNullThreadHandle = NULL;

inline PlatformThreadId GetCurrentThreadId() {
  return ::GetCurrentThreadId();
}
inline PlatformThreadId GetThreadId(PlatformThreadHandle th) {
  return ::GetThreadId(th);
}
inline bool THREAD_ID_IS_SELF(PlatformThreadId thread_id) {
  return (thread_id == ::GetCurrentThreadId());
}
#else  // Assume pthread
typedef pthread_t PlatformThreadHandle;
typedef pthread_t PlatformThreadId;
const PlatformThreadHandle kNullThreadHandle = 0;

inline PlatformThreadId GetCurrentThreadId() {
  return pthread_self();
}
inline PlatformThreadId GetThreadId(PlatformThreadHandle th) {
  return th;
}
inline bool THREAD_ID_IS_SELF(PlatformThreadId thread_id) {
  return pthread_equal(thread_id, pthread_self());
}
#endif

class PlatformThread {
 public:
  class Delegate {
   public:
     virtual ~Delegate() {}
     virtual void ThreadMain() = 0;
  };

  PlatformThread(const PlatformThread&) = delete;
  void operator=(const PlatformThread&) = delete;

  // Creates a new thread using default stack size.  Upon success,
  // |*thread_handle| will be assigned a handle to the newly created thread,
  // and |delegate|'s ThreadMain method will be executed on the newly created
  // thread.
  // NOTE: When you are done with the thread handle, you must call Join to
  // release system resources associated with the thread.  You must ensure that
  // the Delegate object outlives the thread.
  static bool Create(Delegate* delegate, PlatformThreadHandle* thread_handle);

  // Joins with a thread created via the Create function.  This function blocks
  // the caller until the designated thread exits.  This will invalidate
  // |thread_handle|.
  static void Join(PlatformThreadHandle thread_handle);

 private:
  PlatformThread();
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_BASE_PLATFORM_THREAD_H_
