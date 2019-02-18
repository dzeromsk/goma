// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "arfile.h"

#include <cstdio>
#ifndef _WIN32
// TODO: evaluate replacing following code using stdio, or Chromium
//                  base library.
// TODO: add code to parse Win32 .lib format.
#include <ar.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __MACH__
#include <mach-o/ranlib.h>
#endif
#else
// hack to provide snprintf.
#define snprintf _snprintf_s

// Copied from GNU C ar.h
#define ARMAG   "!<arch>\n"     /* String that begins an archive file.  */
#define SARMAG  8               /* Size of that string.  */

#define ARFMAG  "`\n"           /* String in ar_fmag at end of each header.  */

extern "C" {
  struct ar_hdr {
    char ar_name[16];           /* Member file name, sometimes / terminated. */
    char ar_date[12];           /* File date, decimal seconds since Epoch.  */
    char ar_uid[6], ar_gid[6];  /* User and group IDs, in ASCII decimal.  */
    char ar_mode[8];            /* File mode, in ASCII octal.  */
    char ar_size[10];           /* File size, in ASCII decimal.  */
    char ar_fmag[2];            /* Always contains ARFMAG.  */
  };
}

#endif

#include <sstream>
#include <utility>

#include "absl/base/macros.h"
#include "absl/strings/match.h"
#include "glog/logging.h"

// VS2010 and VS2012 doesn't provide C99's atoll(), but VS2013 does.
#if defined(_WIN32) && defined(_MSC_VER) && (_MSC_VER < 1800)
namespace {

long long atoll(const char* nptr) {
  return _strtoi64(nptr, 0, 10);
}

}  // namespace
#endif

namespace devtools_goma {

static const char* kThinArMagic = "!<thin>\n";
// GNU variant support.
static const char* kSymbolTableName = "/               ";
static const char* kSym64TableName = "/SYM64/         ";
static const char* kLongnameTableName = "//              ";

// BSD variant support? "#1/<length>" and name will come after ar_hdr.
// but BSD variant doesn't support thin archive?

static string DumpArHdr(const struct ar_hdr& ar_hdr) {
  std::stringstream ss;
  ss << "name: " << std::hex;
  for (size_t i = 0; i < sizeof ar_hdr.ar_name; ++i) {
    ss << static_cast<int>(ar_hdr.ar_name[i]) << " ";
  }
  ss << std::endl;
  ss << "date: " << std::hex;
  for (size_t i = 0; i < sizeof ar_hdr.ar_date; ++i) {
    ss << static_cast<int>(ar_hdr.ar_date[i]) << " ";
  }
  ss << std::endl;
  ss << "uid: " << std::hex;
  for (size_t i = 0; i < sizeof ar_hdr.ar_uid; ++i) {
    ss << static_cast<int>(ar_hdr.ar_uid[i]) << " ";
  }
  ss << std::endl;
  ss << "mode: " << std::hex;
  for (size_t i = 0; i < sizeof ar_hdr.ar_mode; ++i) {
    ss << static_cast<int>(ar_hdr.ar_mode[i]) << " ";
  }
  ss << std::endl;
  ss << "size: " << std::hex;
  for (size_t i = 0; i < sizeof ar_hdr.ar_size; ++i) {
    ss << static_cast<int>(ar_hdr.ar_size[i]) << " ";
  }
  ss << std::endl;
  ss << "fmag: " << std::hex;
  for (size_t i = 0; i < sizeof ar_hdr.ar_fmag; ++i) {
    ss << static_cast<int>(ar_hdr.ar_fmag[i]) << " ";
  }
  ss << std::endl;
  return ss.str();
}

string ArFile::EntryHeader::DebugString() const {
  std::stringstream ss;
  ss << "name:" << ar_name << " ";
  ss << "date:" << ar_date << " ";
  ss << "uid:" << ar_uid << " ";
  ss << "gid:" << ar_gid << " ";
  ss << "mode:" << ar_mode << " ";
  ss << "size:" << ar_size;
  return ss.str();
}

bool ArFile::EntryHeader::SerializeToString(string* output) const {
  DCHECK(output);
  struct ar_hdr hdr;
  size_t len;
  memset(&hdr, ' ', sizeof(hdr));
  memmove(hdr.ar_name, orig_ar_name.c_str(), sizeof(hdr.ar_name));
  len = snprintf(hdr.ar_date, sizeof(hdr.ar_date), "%llu",
      static_cast<unsigned long long>(ar_date));
  if (len < ABSL_ARRAYSIZE(hdr.ar_date))
    hdr.ar_date[len] = ' ';
  len = snprintf(hdr.ar_uid,  sizeof(hdr.ar_uid),   "%u", ar_uid);
  if (len < ABSL_ARRAYSIZE(hdr.ar_uid))
    hdr.ar_uid[len] = ' ';
  len = snprintf(hdr.ar_gid,  sizeof(hdr.ar_gid),   "%u", ar_gid);
  if (len < ABSL_ARRAYSIZE(hdr.ar_gid))
    hdr.ar_gid[len] = ' ';
  len = snprintf(hdr.ar_mode, sizeof(hdr.ar_mode),  "%o", ar_mode);
  if (len < ABSL_ARRAYSIZE(hdr.ar_mode))
    hdr.ar_mode[len] = ' ';
  len = snprintf(hdr.ar_size, sizeof(hdr.ar_size), "%zu", ar_size);
  if (len < ABSL_ARRAYSIZE(hdr.ar_size))
    hdr.ar_size[len] = ' ';
  memmove(hdr.ar_fmag, ARFMAG, sizeof(hdr.ar_fmag));
  output->assign(reinterpret_cast<char*>(&hdr),  sizeof(hdr));
  return true;
}

ArFile::ArFile(string filename, off_t offset)
    : filename_(std::move(filename)),
      thin_archive_(false),
      valid_(true),
      offset_(offset) {
  Init();
}

ArFile::ArFile(string filename)
    : filename_(std::move(filename)),
      thin_archive_(false),
      valid_(true),
      offset_(0) {
  Init();
}

ArFile::~ArFile() {
}

void ArFile::Init() {
  fd_.reset(ScopedFd::OpenForRead(filename_));
  if (fd_.Seek(offset_, ScopedFd::SeekAbsolute) == static_cast<off_t>(-1)) {
    PLOG(WARNING) << "seek " << offset_ << ":" << filename_;
    fd_.Close();
    return;
  }

  char magic[SARMAG];
  if (fd_.Read(&magic, SARMAG) != SARMAG) {
    PLOG(WARNING) << "read magic:" << filename_;
    fd_.Close();
    return;
  }
  if (memcmp(magic, ARMAG, SARMAG) == 0) {
    VLOG(1) << "normal ar file:" << filename_;
    return;
  }
  if (memcmp(magic, kThinArMagic, SARMAG) == 0) {
    VLOG(1) << "thin ar file:" << filename_;
    thin_archive_ = true;
    return;
  }

  // This is not expected ar file.  It is possibly linker script.
  valid_ = false;
}

bool ArFile::Exists() const {
  return fd_.valid();
}

bool ArFile::IsThinArchive() const {
  return thin_archive_;
}

bool ArFile::ReadHeader(string* ar_header) const {
  DCHECK(ar_header);
  if (!fd_.valid() || !valid_) {
    LOG(WARNING) << "invalid file:" << filename_
                 << fd_.valid() << valid_;
    return false;
  }

  if (fd_.Seek(offset_, ScopedFd::SeekAbsolute) == static_cast<off_t>(-1)) {
    PLOG(WARNING) << "seek " << offset_ << ":" << filename_;
    return false;
  }

  ar_header->resize(SARMAG);
  if (fd_.Read(&(*ar_header)[0], SARMAG) != SARMAG) {
    PLOG(WARNING) << "read SARMAG:" << filename_;
    return false;
  }
  return true;
}

bool ArFile::ReadEntry(EntryHeader* header, string* body) {
  DCHECK(header);
  DCHECK(body);
  const off_t offset = fd_.Seek(0, ScopedFd::SeekRelative);
  VLOG(3) << "offset=" << offset;
  LOG_IF(WARNING, (offset & 1) != 0)
      << "ar_hdr must be on even boundary: offset:" << offset;

  struct ar_hdr hdr;
  if (fd_.Read(&hdr, sizeof(hdr)) != sizeof(hdr)) {
    LOG(ERROR) << "failed to read."
               << " offset=" << offset;
    return false;
  }

  if (!ConvertArHeader(hdr, header)) {
    LOG(ERROR) << "failed to convert."
               << " offset=" << offset;
    return false;
  }

  body->clear();
  if (IsSymbolTableEntry(*header) ||
      IsLongnameEntry(*header) ||
      !thin_archive_) {
    if (!ReadEntryData(*header, body)) {
      PLOG(ERROR) << "read failed:" << header->ar_name;
      return false;
    }
    if (header->ar_size & 1) {
      body->append(1, '\n');
    }
  }
#ifdef __MACH__
  if (!CleanIfRanlib(*header, body)) {
    LOG(WARNING) << "failed to clean ranlib:"
                 << " filename=" << filename_;
  }
#endif

  return true;
}

void ArFile::GetEntries(std::vector<EntryHeader>* entries) {
  if (fd_.Seek(offset_ + SARMAG, ScopedFd::SeekAbsolute)
      == static_cast<off_t>(-1)) {
    PLOG(WARNING) << "seek SARMAG:" << filename_;
    return;
  }
  string longnames;
  struct ar_hdr hdr;
  int i = 0;
  while (fd_.Read(&hdr, sizeof(hdr)) == sizeof(hdr)) {
    // offset of the beginning of each entry.
    const off_t offset = fd_.Seek(0, ScopedFd::SeekRelative) - sizeof(hdr);
    LOG_IF(WARNING, (offset & 1) != 0)
        << "ar_hdr must be on even boundary: i:" << i << " offset:" << offset;
    VLOG(2) << "i:" << i << " offset:" << offset << " " << DumpArHdr(hdr);
    ++i;
    EntryHeader entry;
    if (!ConvertArHeader(hdr, &entry)) {
      VLOG(1) << DumpArHdr(hdr);
      continue;
    }
    VLOG(1) << "entry:" << entry.DebugString();
    if (IsSymbolTableEntry(entry)) {
      if (!SkipEntryData(entry)) {
        PLOG(ERROR) << "skip failed:" << entry.ar_name;
      }
      continue;
    }
    if (IsLongnameEntry(entry)) {
      if (!ReadEntryData(entry, &longnames_)) {
        PLOG(ERROR) << "read failed:" << entry.ar_name;
      }
      continue;
    }
    if (!FixEntryName(&entry.ar_name)) {
      LOG(ERROR) << "Fix name failed:" << entry.ar_name;
      continue;
    }
    entries->push_back(entry);
    if (!thin_archive_) {
      SkipEntryData(entry);
    }
  }
}

/* static */
bool ArFile::ConvertArHeader(const struct ar_hdr& hdr,
                             EntryHeader* entry_header) {
  DCHECK(entry_header != nullptr);
  if (memcmp(hdr.ar_fmag, ARFMAG, sizeof hdr.ar_fmag) != 0) {
    LOG(ERROR) << "BAD header name: ["
               << string(hdr.ar_name, sizeof hdr.ar_name)
               << "] fmag: [" << string(hdr.ar_fmag, 2) << "]";
    return false;
  }
  entry_header->orig_ar_name = entry_header->ar_name =
      string(hdr.ar_name, sizeof hdr.ar_name);
  entry_header->ar_date = static_cast<time_t>(
      atoll(string(hdr.ar_date, sizeof hdr.ar_date).c_str()));
  entry_header->ar_uid = static_cast<uid_t>(
      atoi(string(hdr.ar_uid, sizeof hdr.ar_uid).c_str()));
  entry_header->ar_gid = static_cast<gid_t>(
      atoi(string(hdr.ar_gid, sizeof hdr.ar_gid).c_str()));
  entry_header->ar_mode = static_cast<mode_t>(
      strtol(string(hdr.ar_mode, sizeof hdr.ar_mode).c_str(), nullptr, 8));
  entry_header->ar_size = static_cast<size_t>(
      atoi(string(hdr.ar_size, sizeof hdr.ar_size).c_str()));
  return true;
}

bool ArFile::SkipEntryData(const EntryHeader& entry_header) {
  size_t size = entry_header.ar_size + (entry_header.ar_size & 1);
  if (fd_.Seek(size, ScopedFd::SeekRelative) == static_cast<off_t>(-1)) {
    return false;
  }
  return true;
}

bool ArFile::ReadEntryData(const EntryHeader& entry_header, string* data) {
  DCHECK(data != nullptr);
  data->resize(entry_header.ar_size);
  size_t nr = 0;
  while (nr < entry_header.ar_size) {
    int n = fd_.Read(const_cast<char*>(data->data() + nr),
                      entry_header.ar_size - nr);
    if (n <= 0) {
      return false;
    }
    nr += n;
  }
  if (entry_header.ar_size & 1) {
    if (fd_.Seek(1, ScopedFd::SeekRelative) == static_cast<off_t>(-1)) {
      return false;
    }
  }
  return true;
}

bool ArFile::FixEntryName(string* name) {
  if ((*name)[0] == '/') {
    /* long name */
    size_t i = static_cast<size_t>(strtoul(name->c_str() + 1, nullptr, 10));
    size_t j = i;
    while ((j < longnames_.size()) &&
           longnames_[j] != '\n' &&
           longnames_[j] != '\0') {
      ++j;
    }
    if (longnames_[j - 1] == '/')
      --j;
    name->assign(longnames_.data() + i, j - i);
    return true;
  }
  /* short name */
  const char* kDelimiters = " /";
  size_t pos = name->find_last_not_of(kDelimiters);
  if (pos != string::npos)
    name->erase(pos + 1);

  return true;
}

/* static */
bool ArFile::IsSymbolTableEntry(const EntryHeader& entry_header) {
  return (entry_header.ar_name == kSymbolTableName ||
          entry_header.ar_name == kSym64TableName);
}

/* static */
bool ArFile::IsLongnameEntry(const EntryHeader& entry_header) {
  return (entry_header.ar_name == kLongnameTableName);
}

#ifdef __MACH__
/* static */
bool ArFile::CleanIfRanlib(const EntryHeader& hdr, string* body) {
  // Only support ar files on Intel mac (little endian).
  // You need to convert endian if you need support of big endian such as ppc.
  //
  // It is known that mac has a special pattern at the beginning of ranlib.
  // The magic is given as BSD 4.4 style long name.
  // However, I do not provide full-spec parser of BSD 4.4 style long name
  // because thin archive might not be used on mac.
  static const char* kRanlibName = "#1/20           ";
  static const size_t kSymdefMagicSize = 20;  // size of SYMDEF magic.
  if (hdr.orig_ar_name != kRanlibName ||
      body->size() <= kSymdefMagicSize || !absl::StartsWith(*body, SYMDEF)) {
    VLOG(1) << "Not mac ranlib file.";
    return true;
  }

  // Format of the ranlib entry:
  // ar header
  // SYMDEF magic (e.g. __.SYMDEF SORTED): 20 bytes
  // ranlib area size: 4 bytes.
  // ranlib area
  // string area size: 4 bytes.
  // string area.
  //
  // We need to remove garbage bytes at the end of string area.
  const char* base = &(*body)[0];
  char* pos = const_cast<char*>(base) + kSymdefMagicSize;
  uint32_t ranlib_size;
  memcpy(&ranlib_size, pos, sizeof(ranlib_size));
  const ranlib* ranlib_base = reinterpret_cast<const ranlib*>(
      pos + sizeof(ranlib_size));
  pos += sizeof(ranlib_size) + ranlib_size;
  if (pos - base > static_cast<ssize_t>(hdr.ar_size)) {
    LOG(WARNING) << "ranlib size broken:"
                 << " ar_size=" << hdr.ar_size
                 << " ranlib size=0x" << std::hex << ranlib_size;
    return false;
  }
  uint32_t string_size;
  memcpy(&string_size, pos, sizeof(string_size));
  const char* string_base = pos + sizeof(string_size);
  pos += sizeof(string_size) + string_size;
  if (pos - base > static_cast<ssize_t>(hdr.ar_size)) {
    LOG(WARNING) << "string size broken:"
                 << " ar_size=" << hdr.ar_size
                 << " string size=0x" << std::hex << string_size;
    return false;
  }

  // See ranlib entries to recognize end of strings.
  uint32_t last_offset = 0;
  for (size_t i = 0; i < ranlib_size / sizeof(ranlib); ++i) {
    uint32_t str_offset = ranlib_base[i].ran_un.ran_strx;
    if (last_offset < str_offset)
      last_offset = str_offset;
  }
  if (last_offset > string_size) {
    LOG(WARNING) << "string size in ranlib entry broken:"
                 << " ar_size=" << hdr.ar_size
                 << " string size=" << string_size
                 << " str_offset=0x" << std::hex << last_offset
                 << " offset=" << std::dec << pos - base;
    return false;
  }
  uint32_t last_end_of_string =
      last_offset + strlen(string_base + last_offset) + 1;
  int32_t diff = string_size - last_end_of_string;
  if (diff > 0)
    memset(pos - diff, '\0', diff);
  return true;
}
#endif

}  // namespace devtools_goma
