// Copyright 2013 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "machine_info.h"

#include <stdio.h>
#include <sys/types.h>

#include "basictypes.h"
#include "glog/logging.h"
#include "scoped_fd.h"
#include "util.h"

#if defined(_WIN32)
#include <psapi.h>
#include <windows.h>
#endif

#if defined(__linux__)
#include <unistd.h>
#endif

#if defined(__MACH__)
#include <libproc.h>
#include <sys/sysctl.h>
#include <sys/proc_info.h>
#endif

namespace devtools_goma {

#if defined(_WIN32)

int GetNumCPUs() {
  SYSTEM_INFO sysinfo;
  GetSystemInfo(&sysinfo);
  return sysinfo.dwNumberOfProcessors;
}

int64_t GetSystemTotalMemory() {
  MEMORYSTATUSEX status;
  status.dwLength = sizeof(status);
  if (!GlobalMemoryStatusEx(&status)) {
    fprintf(stderr, "GlobalMemoryStatusEx failed: %lu\n", GetLastError());
    return 0;
  }

  return status.ullTotalPhys;
}

int64_t GetConsumingMemoryOfCurrentProcess() {
  DWORD process_id = GetCurrentProcessId();

  ScopedFd process(OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, process_id));
  if (!process.valid()) {
    PLOG(ERROR) << "OpenProcess failed";
    return 0;
  }

  PROCESS_MEMORY_COUNTERS pmc;
  if (!GetProcessMemoryInfo(process.handle(), &pmc, sizeof(pmc))) {
    PLOG(ERROR) << "GetProcessMemoryInfo failed";
    return 0;
  }

  return pmc.WorkingSetSize;
}

#elif defined(__linux__)

int GetNumCPUs() {
  int cpus = sysconf(_SC_NPROCESSORS_ONLN);
  if (cpus < 0) {
    PLOG(ERROR) << "sysconf(_SC_NPROCESSORS_ONLN) failed";
    return 0;
  }

  return cpus;
}

int64_t GetSystemTotalMemory() {
  const int64_t page_size = sysconf(_SC_PAGESIZE);
  if (page_size < 0) {
    PLOG(ERROR) << "sysconf(_SC_PAGESIZE) failed";
    return 0;
  }

  const int64_t num_pages = sysconf(_SC_PHYS_PAGES);
  if (num_pages < 0) {
    PLOG(ERROR) << "sysconf(_SC_PHYS_PAGES) failed";
    return 0;
  }

  return page_size * num_pages;
}

int64_t GetConsumingMemoryOfCurrentProcess() {
  // Reads /proc/self/statm
  // The second column is the number of pages for resident.

  const int64_t page_size = sysconf(_SC_PAGESIZE);
  if (page_size < 0) {
    PLOG(ERROR) << "sysconf(_SC_PAGESIZE) failed";
    return 0;
  }

  ScopedFd fd(ScopedFd::OpenForRead("/proc/self/statm"));
  if (!fd.valid()) {
    PLOG(ERROR) << "Opening /proc/self/statm failed";
    return 0;
  }

  char buf[1024];
  if (fd.Read(buf, 1024) < 0) {
    PLOG(ERROR) << "Reading /proc/self/statm failed";
    return 0;
  }

  int num_pages;
  if (sscanf(buf, "%*d %d", &num_pages) != 1) {
    LOG(ERROR) << "Data from /proc/self/statm is not in expected form";
    return 0;
  }

  return num_pages * page_size;
}

#elif defined(__MACH__)
int GetNumCPUs() {
  static const char* kCandidates[] = {
    "hw.logicalcpu_max", "hw.ncpu"
  };

  int size = 0;
  size_t len = sizeof(size);
  for (const auto& candidate : kCandidates) {
    if (sysctlbyname(candidate, &size, &len, nullptr, 0) == 0) {
      return size;
    }
  }

  // Failed for all candidates.
  LOG(ERROR) << "sysctlbyname for GetNumCPUs failed";
  return 0;
}

int64_t GetSystemTotalMemory() {
  int64_t size;
  size_t len = sizeof(size);

  if (sysctlbyname("hw.memsize", &size, &len, nullptr, 0) < 0) {
    PLOG(ERROR) << "sysctlbyname(hw.memsize) failed";
    return 0;
  }

  return size;
}

int64_t GetConsumingMemoryOfCurrentProcess() {
  const pid_t pid = Getpid();

  struct proc_taskinfo taskinfo;
  int infosize = proc_pidinfo(pid, PROC_PIDTASKINFO, 0,
                              &taskinfo, sizeof(taskinfo));
  if (infosize < 0) {
    PLOG(ERROR) << "proc_pidinfo failed";
    return 0;
  }

  // According to this blog,
  // http://vinceyuan.blogspot.jp/2011/12/wrong-info-from-procpidinfo.html
  // we have to check proc_pidinfo returning value. Sometimes proc_pidinfo
  // returns too few bytes.
  if (infosize < sizeof(taskinfo)) {
    LOG(ERROR) << "proc_pidinfo returned too few bytes " << infosize
               << " (expected " << sizeof(taskinfo) << ")";
    return 0;
  }

  return taskinfo.pti_resident_size;
}

#else
#  error "Unknown architecture"
#endif

}  // namespace devtools_goma
