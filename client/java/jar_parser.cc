// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "jar_parser.h"

#include <limits.h>
#include <string.h>

#include <memory>

#include "absl/strings/match.h"
#include "absl/strings/str_split.h"
#include "basictypes.h"
#include "glog/logging.h"
#include "minizip/unzip.h"
#include "path.h"

#ifdef _WIN32
#include "config_win.h"
#endif

namespace devtools_goma {

JarParser::JarParser() {}

static void AddJarFile(absl::string_view jar_file,
                       absl::string_view cwd,
                       std::set<string>* jar_files);

static void ReadManifest(char* content,
                         absl::string_view cwd,
                         std::set<string>* jar_files) {
  // The format of manifest files is similar to HTTP header
  // (i.e., "key1: value1<CRLF>key2: value2<CRLF>")
  // We need only the value of Class-Path.
  char* p = content;
  static const char kClassPathHeader[] = "Class-Path: ";
  const size_t kClassPathHeaderSize = strlen(kClassPathHeader);
  for (;;) {
    if (!strncmp(p, kClassPathHeader, kClassPathHeaderSize)) {
      p += kClassPathHeaderSize;
      break;
    }

    p = strchr(p, '\n');
    if (!p) {
      return;
    }
    p++;
  }

  char* end = strchr(p, '\r');
  if (end) {
    *end = '\0';
  }

  for (auto&& path : absl::StrSplit(p, ' ', absl::SkipEmpty())) {
    if (absl::EndsWith(path, ".jar")) {
      AddJarFile(path, cwd, jar_files);
    }
  }
}

class ScopedUnzFile {
 public:
  explicit ScopedUnzFile(const char* path)
      : path_(path), unz_file_(unzOpen64(path)), open_current_(false) {}
  ~ScopedUnzFile() {
    if (open_current_) {
      unzCloseCurrentFile(unz_file_);
      open_current_ = false;
    }
    if (IsValid()) {
      int err = unzClose(unz_file_);
      LOG_IF(WARNING, err != UNZ_OK)
          << "unzClose path=" << path_ << "err=" << err;
    }
  }

  bool IsValid() const { return unz_file_ != 0; }

  int GetGlobalInfo64(unz_global_info64* info) {
    return unzGetGlobalInfo64(unz_file_, info);
  }

  int GetCurrentFileInfo64(unz_file_info64* fileinfo,
                           char* filename,
                           unsigned long filename_bufsize,
                           void* extra,
                           unsigned long extra_bufsize,
                           char* comment,
                           unsigned long comment_bufsize) {
    return unzGetCurrentFileInfo64(unz_file_, fileinfo, filename,
                                   filename_bufsize, extra, extra_bufsize,
                                   comment, comment_bufsize);
  }

  int OpenCurrentFile() {
    DCHECK(!open_current_) << path_;
    int err = unzOpenCurrentFile(unz_file_);
    open_current_ = (err == UNZ_OK);
    return err;
  }

  int ReadCurrentFile(void* buf, size_t len) {
    // TODO: If len >= 2**32, this can fail?
    DCHECK(open_current_) << path_;
    return unzReadCurrentFile(unz_file_, buf, static_cast<unsigned>(len));
  }

  int CloseCurrentFile() {
    open_current_ = false;
    return unzCloseCurrentFile(unz_file_);
  }

  int GoToNextFile() {
    DCHECK(!open_current_) << path_;
    return unzGoToNextFile(unz_file_);
  }

 private:
  const string path_;
  unzFile unz_file_;
  bool open_current_;
  DISALLOW_COPY_AND_ASSIGN(ScopedUnzFile);
};

static void AddJarFile(absl::string_view jar_file,
                       absl::string_view cwd,
                       std::set<string>* jar_files) {
  const string& jar_path = file::JoinPathRespectAbsolute(cwd, jar_file);
  if (!jar_files->insert(jar_path).second) {
    return;
  }

  LOG(INFO) << "Reading jar file: " << jar_path;

  const absl::string_view basedir(file::Dirname(jar_path));

  ScopedUnzFile scoped_jar(jar_path.c_str());
  if (!scoped_jar.IsValid()) {
    LOG(WARNING) << "Not jar archive? (unzOpen64):" << jar_path;
    return;
  }

  int err;
  unz_global_info64 jar_info;
  err = scoped_jar.GetGlobalInfo64(&jar_info);
  if (err) {
    LOG(WARNING) << "Broken jar archive? (unzGetGlobalInfo64): " << jar_path
                 << " err=" << err;
    return;
  }

  for (ZPOS64_T i = 0; i < jar_info.number_entry; i++) {
    unz_file_info64 fileinfo;
    char filename[PATH_MAX];
    err = scoped_jar.GetCurrentFileInfo64(&fileinfo, filename, sizeof(filename),
                                          nullptr, 0, nullptr, 0);
    if (err) {
      LOG(WARNING) << "Broken jar archive? (unzGetCurrentFileInfo64): "
                   << jar_path << " err=" << err;
      return;
    }

    static const char kManifestFileName[] = "META-INF/MANIFEST.MF";
    if (!strcmp(filename, kManifestFileName)) {
      err = scoped_jar.OpenCurrentFile();
      if (err) {
        LOG(WARNING) << "Broken jar archive? (unzOpenCurrentFile): " << jar_path
                     << " err=" << err;
        return;
      }

      size_t sz = static_cast<size_t>(fileinfo.uncompressed_size);
      std::unique_ptr<char[]> buf(new char[sz + 1]);
      err = scoped_jar.ReadCurrentFile(buf.get(), sz);
      if (err < 0) {
        LOG(WARNING) << "Broken jar archive? (unzReadCurrentFile): " << jar_path
                     << " err=" << err;
        return;
      }
      buf.get()[fileinfo.uncompressed_size] = '\0';
      ReadManifest(buf.get(), basedir, jar_files);
      err = scoped_jar.CloseCurrentFile();
      LOG_IF(WARNING, err != UNZ_OK)
          << "CloseCurrentFile: " << jar_path << " err=" << err;
      return;
    }

    err = scoped_jar.GoToNextFile();
    if (err == UNZ_END_OF_LIST_OF_FILE) {
      break;
    }
    if (err) {
      LOG(WARNING) << "Broken jar archive? (unzGoToNextFile): " << jar_path
                   << " err=" << err;
      return;
    }
  }

  if (!absl::EndsWith(jar_file, ".zip")) {
    LOG(WARNING) << jar_file << " doesn't contain manifest";
  }
}

void JarParser::GetJarFiles(const std::vector<string>& input_jar_files,
                            const string& cwd,
                            std::set<string>* jar_files) {
  for (const auto& input_jar_file : input_jar_files) {
    AddJarFile(input_jar_file, cwd, jar_files);
  }
}

}  //  namespace devtools_goma
