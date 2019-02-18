// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "mypath.h"

#include <limits.h>
#ifndef _WIN32
#include <pwd.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#ifdef __MACH__
# include <mach-o/dyld.h>
#endif
#ifdef __FreeBSD__
# include <sys/types.h>
# include <sys/sysctl.h>
#endif
#endif

#include <vector>

#include "glog/logging.h"
#include "basictypes.h"
#ifdef _WIN32
# include "config_win.h"
# include <psapi.h>
# pragma comment(lib, "psapi.lib")
# include <lmcons.h>  // for UNLEN
#endif

#include "absl/base/macros.h"
#include "env_flags.h"
#include "file_dir.h"
#include "file_stat.h"
#include "filesystem.h"
#include "mypath_helper.h"
#include "path.h"
#include "path_resolver.h"
#include "path_util.h"
#include "util.h"

GOMA_DECLARE_string(CACHE_DIR);
GOMA_DECLARE_string(TMP_DIR);

namespace {

#ifndef _WIN32
const char kGomaTmpDirPrefix[] = "goma_";
#else
const char kGomaTmpDir[] = "goma";
#endif
const char kGomaCrashDumpDir[] = "goma_crash";
const char kGomaCacheDir[] = "goma_cache";

template<typename UnaryFunction>
static string GetEnvMatchedCondition(
    const std::vector<const char*>& candidates,
    UnaryFunction condition,
    const char* default_value) {
  for (const auto* candidate : candidates) {
    const string value = devtools_goma::GetEnv(candidate);
    if (!value.empty() && condition(value)) {
      return value;
    }
  }
  return default_value;
}

}  // anonymous namespace

namespace devtools_goma {

string GetUsernameEnv() {
  static const char* kRoot = "root";
  static const char* kUserEnvs[] = {
    "SUDO_USER",
    "USERNAME",
    "USER",
    "LOGNAME",
  };

  return GetEnvMatchedCondition(
      std::vector<const char*>(&kUserEnvs[0],
                               &kUserEnvs[ABSL_ARRAYSIZE(kUserEnvs)]),
      [](const string& user) { return user != kRoot; }, "");
}

string GetUsernameNoEnv() {
#ifndef _WIN32
  uid_t uid = getuid();
  struct passwd* pw = getpwuid(uid);
  if (uid == 0 || pw == nullptr || pw->pw_name == nullptr ||
      *pw->pw_name == '\0') {
    return "";
  }
  return pw->pw_name;
#else
  char buf[UNLEN + 1] = {0};
  DWORD len = UNLEN;
  ::GetUserNameA(buf, &len);
  return buf;
#endif
}

string GetUsername() {
  string username = GetUsernameEnv();
  if (!username.empty()) {
    return username;
  }
  username = GetUsernameNoEnv();
  if (!username.empty()) {
    SetEnv("USER", username);
    return username;
  }
  return "unknown";
}

string GetNodename() {
#ifndef _WIN32
  // Gets nodename, which is a good enough approximation to a
  // hostname, for debugging purposes, for now.
  struct utsname u;
  if (uname(&u) == 0) {
    return u.nodename;
  }
  PLOG(ERROR) << "uname failed";
#else
  // Get NetBIOS name for now to avoid network queries.
  char buffer[MAX_COMPUTERNAME_LENGTH + 1] = {0};
  DWORD len = MAX_COMPUTERNAME_LENGTH + 1;
  if (GetComputerNameA(buffer, &len) && len) {
    string nodename(buffer, len);
    return nodename;
  }
  LOG(ERROR) << "GetComputerName " << GetLastError();
#endif
  return "localhost";
}

string GetMyPathname() {
  string myself_fullpath;
#ifdef _WIN32
  char path[PATH_MAX] = {0};
  HANDLE process = GetCurrentProcess();
  PCHECK(GetModuleFileNameExA(process, nullptr, path, PATH_MAX));
  myself_fullpath = path;
#elif defined(__MACH__)
  myself_fullpath = _dyld_get_image_name(0);
#elif defined(__FreeBSD__)
  char buf[PATH_MAX + 1];
  const int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1};
  size_t length = sizeof(buf);
  PCHECK(sysctl(mib, 4, buf, &length, nullptr, 0) >= 0);
  CHECK_GT(length, 1U);
  myself_fullpath.assign(buf, length - 1);
#else
  char buf[PATH_MAX + 1];
  ssize_t len;
  PCHECK((len = readlink("/proc/self/exe", buf, PATH_MAX)) >= 0);
  CHECK_LT(static_cast<size_t>(len), sizeof buf);
  buf[len] = '\0';
  myself_fullpath = buf;
#endif
  // Mac's _dyld_get_image_name(0) sometimes returns path containing ".".
  // Not to confuse caller, normalize path here.
  return PathResolver::ResolvePath(myself_fullpath);
}

string GetMyDirectory() {
#ifndef _WIN32
  const char SEP = '/';
#else
  const char SEP = '\\';
#endif
  string myself_fullpath = GetMyPathname();
  size_t last_slash = myself_fullpath.rfind(SEP);
  CHECK(last_slash != string::npos);
  return myself_fullpath.substr(0, last_slash);
}

// NOTE: When updating this, you also need to update get_temp_directory() in
// client/goma-wrapper and GetGomaTmpDir in goma_ctl.py.
string GetGomaTmpDir() {
  if (FLAGS_TMP_DIR != "") {
    return FLAGS_TMP_DIR;
  }

  string tmpdir = GetPlatformSpecificTempDirectory();
#ifndef _WIN32
  if (tmpdir.empty()) {
    tmpdir = "/tmp";
  }
#endif
  CHECK(!tmpdir.empty()) << "Could not determine temp directory.";

  // Assume goma_ctl.py creates /tmp/goma_<user> or %TEMP%\goma.
#ifndef _WIN32
  string private_name(kGomaTmpDirPrefix);
  const string username = GetUsername();
  if (username == "" || username == "unknown") {
    LOG(ERROR) << "bad username:" << username;
  }
  private_name.append(username);
#else
  string private_name(kGomaTmpDir);
#endif
  string private_tmpdir = file::JoinPath(tmpdir, private_name);
  return private_tmpdir;
}

void CheckTempDirectory(const string& tmpdir) {
  if (!EnsureDirectory(tmpdir, 0700)) {
    LOG(FATAL) << "failed to create goma tmp dir or "
               << "private goma tmp dir is not dir: " << tmpdir;
  }

#ifndef _WIN32
  struct stat st;
  // We must use lstat instead of stat to avoid symlink attack (b/69717657).
  PCHECK(lstat(tmpdir.c_str(), &st) == 0) << "lstat " << tmpdir;
  if ((st.st_mode & 077) != 0) {
    LOG(FATAL) << "private goma tmp dir is not owned only by you. "
               << "please check owner/permission of " << tmpdir
               << ".  It must not be readable/writable by group/other. "
               << "e.g.  $ chmod go-rwx " << tmpdir;
  }
#endif
}

string GetCrashDumpDirectory() {
  return file::JoinPath(GetGomaTmpDir(), kGomaCrashDumpDir);
}

string GetCacheDirectory() {
  if (FLAGS_CACHE_DIR != "") {
    return FLAGS_CACHE_DIR;
  }

  return file::JoinPath(GetGomaTmpDir(), kGomaCacheDir);
}

#ifndef _WIN32
// check we can believe PWD environment variable.
// Align with llvm current_path().
// llvm checking PWD id and "." id are the same.
// see also http://b/122976726
static bool checkPWD(const char* pwd) {
  struct stat pwd_stat;
  memset(&pwd_stat, 0, sizeof pwd_stat);
  if (stat(pwd, &pwd_stat) != 0) {
    PLOG(WARNING) << "stat: pwd=" << pwd;
    return false;
  }
  struct stat dot_stat;
  memset(&dot_stat, 0, sizeof dot_stat);
  if (stat(".", &dot_stat) != 0) {
    PLOG(WARNING) << "stat: .";
    return false;
  }
  return memcmp(&pwd_stat, &dot_stat, sizeof(struct stat)) == 0;
}
#endif

string GetCurrentDirNameOrDie(void) {
#ifndef _WIN32
  // get_cwd() returns the current resolved directory. However, a compiler is
  // taking PWD as current working directory. PWD might contain unresolved
  // directory.
  // We don't return /proc/self/cwd if it is set in PWD, since the corresponding
  // directory is different among gomacc and compiler_proxy.
  // See also: b/37259278

  const char* pwd = getenv("PWD");
  if (pwd != nullptr && IsPosixAbsolutePath(pwd) &&
      !HasPrefixDir(pwd, "/proc/self/cwd")) {
    if (checkPWD(pwd)) {
      return pwd;
    }
  }

  char *dir = getcwd(nullptr, 0);
  CHECK(dir) << "GOMA: Cannot find current directory ";
  string dir_str(dir);
  free(dir);
  return dir_str;
#else
  char dir[PATH_MAX];
  CHECK_NE(GetCurrentDirectoryA(PATH_MAX, dir), (DWORD)0) <<
      "GOMA: Cannot find current directory: " << GetLastError();
  string dir_str(dir);
  return dir_str;
#endif
}

}  // namespace devtools_goma
