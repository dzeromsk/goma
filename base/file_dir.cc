// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "file_dir.h"

#include <utility>

#ifndef _WIN32
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#else
#include "config_win.h"
#endif

#include "filesystem.h"
#include "options.h"
#include "path.h"
#include "status.h"

namespace devtools_goma {

#ifndef _WIN32
bool ListDirectory(const string& dirname, std::vector<DirEntry>* entries) {
  DIR* dir = opendir(dirname.c_str());
  if (dir == nullptr) {
    struct stat st;
    if (stat(dirname.c_str(), &st) != 0) {
      return false;
    }
    return !S_ISDIR(st.st_mode);
  }

  struct dirent* ent;
  while ((ent = readdir(dir)) != nullptr) {
    DirEntry dent;
    dent.name = ent->d_name;
    dent.is_dir = ent->d_type == DT_DIR;
    if (ent->d_type == DT_LNK) {
      struct stat st;
      if (stat(file::JoinPath(dirname, dent.name).c_str(), &st) == 0) {
        dent.is_dir = S_ISDIR(st.st_mode);
      }
    }
    entries->push_back(std::move(dent));
  }
  closedir(dir);
  return true;
}

bool DeleteDirectory(const string& dirname) {
  return rmdir(dirname.c_str()) == 0;
}

#else
bool ListDirectory(const string& dirname, std::vector<DirEntry>* entries) {
  DWORD attr = GetFileAttributesA(dirname.c_str());
  if (attr == INVALID_FILE_ATTRIBUTES)
    return false;
  if (!(attr & FILE_ATTRIBUTE_DIRECTORY))
    return true;

  const string pattern = dirname + "\\*";
  WIN32_FIND_DATAA find_data = {};
  HANDLE find_handle = FindFirstFileA(pattern.c_str(), &find_data);
  if (find_handle == INVALID_HANDLE_VALUE)
    return false;

  BOOL reading = TRUE;
  for (; reading == TRUE; reading = FindNextFileA(find_handle, &find_data)) {
    DirEntry dent;
    dent.name = find_data.cFileName;
    dent.is_dir = (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    entries->push_back(std::move(dent));
  }
  FindClose(find_handle);
  return true;
}

bool DeleteDirectory(const string& dirname) {
  return RemoveDirectoryA(dirname.c_str()) != 0;
}
#endif

bool EnsureDirectory(const string& dirname, int mode) {
  if (file::IsDirectory(dirname, file::Defaults()).ok()) {
    return true;
  }
  if (file::CreateDir(dirname.c_str(), file::CreationMode(mode)).ok()) {
    return true;
  }

  // When multiple processes call EnsureDirectory simultaneously, race might
  // happen. So, we need to check IsDirectory again for the safe here.
  return file::IsDirectory(dirname, file::Defaults()).ok();
}

}  // namespace devtools_goma
