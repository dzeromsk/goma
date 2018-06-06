// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_BASE_CONFIG_WIN_H_
#define DEVTOOLS_GOMA_BASE_CONFIG_WIN_H_

#ifdef _WIN32
#pragma once

// This must be defined before the windows.h is included.
#ifndef WINVER
#define WINVER 0x0600
#endif

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN     // Avoid a bunch of conflicts.
#endif

#define PATH_MAX MAX_PATH

#if defined(_MSC_VER) || defined(_MSC_EXTENSIONS)
# define PRECISION_DIVIDER         10000000Ui64
# define DELTA_EPOCH_IN_MICROSECS  11644473600000000Ui64
#else
# define PRECISION_DIVIDER         10000000ULL
# define DELTA_EPOCH_IN_MICROSECS  11644473600000000ULL
#endif

// Following definitions are only valid for win32.
typedef unsigned int uid_t;
typedef unsigned int gid_t;
typedef unsigned long pid_t;
typedef unsigned int mode_t;
typedef int ssize_t;

#if defined (_MSC_VER) && (_MSC_VER < 1600)
  typedef unsigned char     uint8_t;
  typedef signed char       int8_t;
  typedef unsigned __int16  uint16_t;
  typedef signed __int16    int16_t;
  typedef unsigned __int32  uint32_t;
  typedef signed __int32    int32_t;
  typedef unsigned __int64  uint64_t;
  typedef signed __int64    int64_t;
#else
  #include <stdint.h>
#endif

#include <windows.h>

#endif  // _WIN32
#endif  // DEVTOOLS_GOMA_BASE_CONFIG_WIN_H_
