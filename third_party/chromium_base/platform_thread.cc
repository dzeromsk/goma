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

#include "platform_thread.h"

#include "glog/logging.h"

#ifdef _WIN32
#include <process.h>
#endif

#if HAVE_CPU_PROFILER
#include <gperftools/profiler.h>
#endif

namespace devtools_goma {

#if defined (_WIN32)

unsigned __stdcall ThreadFunc(void* params) {
  PlatformThread::Delegate* delegate =
      static_cast<PlatformThread::Delegate*>(params);
  delegate->ThreadMain();
  return 0;
}

// static
bool PlatformThread::Create(Delegate* delegate,
                            PlatformThreadHandle* thread_handle) {
  CHECK(thread_handle);
  uintptr_t r = _beginthreadex(nullptr, 0, ThreadFunc, delegate, 0, nullptr);
  if (r == 0) {
    return false;
  }

  *thread_handle = reinterpret_cast<HANDLE>(r);
  return true;
}

// static
void PlatformThread::Join(PlatformThreadHandle thread_handle) {
  CHECK(thread_handle);
  DWORD result = WaitForSingleObject(thread_handle, INFINITE);
  CHECK(result == WAIT_OBJECT_0);
  CloseHandle(thread_handle);
}

#else

void* ThreadFunc(void* params) {
#if HAVE_CPU_PROFILER
  ProfilerRegisterThread();
#endif
  PlatformThread::Delegate* delegate =
      static_cast<PlatformThread::Delegate*>(params);
  delegate->ThreadMain();
  return nullptr;
}

// static
bool PlatformThread::Create(Delegate* delegate,
                            PlatformThreadHandle* thread_handle) {
  CHECK(thread_handle);

  bool success = false;
  pthread_attr_t attributes;
  pthread_attr_init(&attributes);
  success = !pthread_create(thread_handle, &attributes, ThreadFunc, delegate);
  PLOG_IF(ERROR, !success) << "pthread_create";
  pthread_attr_destroy(&attributes);

  return success;
}

// static
void PlatformThread::Join(PlatformThreadHandle thread_handle) {
  CHECK(thread_handle);
  pthread_join(thread_handle, nullptr);
}

#endif  // _WIN32

}  // namespace devtools_goma
