// Copyright 2013 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_MACHINE_INFO_H_
#define DEVTOOLS_GOMA_CLIENT_MACHINE_INFO_H_

#include <stdint.h>

namespace devtools_goma {

// Gets the number of CPUs. If failed obtaining, 0 will be returned.
int GetNumCPUs();

// Gets the total size of memory in bytes.
// If failed obtaining, 0 will be returned.
int64_t GetSystemTotalMemory();

// Gets consumed memory of the current process in bytes.
//   On Linux, this is equal to "RES" in top.
//   On Windows, this is equal to "Working Set" in Task Manager.
//   On Mac, this is equal to "Real Memory" in Activity Monitor.
// If failed obtaining, 0 will be returned.
int64_t GetConsumingMemoryOfCurrentProcess();

// Gets virtual memory of the current process in bytes.
// If failed obtaining, 0 will be returned.
int64_t GetVirtualMemoryOfCurrentProcess();

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_MACHINE_INFO_H_
