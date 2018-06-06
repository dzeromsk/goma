// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_ARFILE_H_
#define DEVTOOLS_GOMA_CLIENT_ARFILE_H_

#include <sys/types.h>

#include <string>
#include <vector>

#include "basictypes.h"
#include "gtest/gtest_prod.h"
#include "scoped_fd.h"

struct ar_hdr;

namespace devtools_goma {

// Ar file parser.
class ArFile {
 public:
  struct EntryHeader {
    std::string ar_name;
    time_t ar_date;
    uid_t ar_uid;
    gid_t ar_gid;
    mode_t ar_mode;
    size_t ar_size;

    // original ar_name of the entry. ar_name would be modified for long name in
    // FixEntryName().
    std::string orig_ar_name;
    bool SerializeToString(std::string* output) const;
    std::string DebugString() const;
  };
  explicit ArFile(std::string filename);
  explicit ArFile(std::string filename, off_t offset);
  virtual ~ArFile();

  virtual const std::string& filename() const { return filename_; }
  virtual bool Exists() const;
  virtual bool IsThinArchive() const;
  virtual off_t offset() const { return offset_; }

  // Note:
  // You SHOULD NOT use GetEntries with ReadEntry.
  // It may break ReadEntry result.
  virtual void GetEntries(std::vector<EntryHeader>* entries);

  // Read a header of an archive file.
  // Returns true for success and the header is stored to |ar_header|.
  virtual bool ReadHeader(std::string* ar_header) const;
  // Read an entry in an archive file.
  // Returns true for success.
  // The entry header is stored to |header|.
  // The entry body is stored to |body|.  For thin archive, body could be set to
  // empty string.
  virtual bool ReadEntry(EntryHeader* header, std::string* body);

 private:
  friend class StubArFile;
#ifdef __MACH__
  FRIEND_TEST(ArFileTest, CleanIfRanlibTest);
#endif
  // ArFile() is provided only for testing. You SHOULD NOT use this.
  ArFile() : thin_archive_(false) {}
  static bool ConvertArHeader(const struct ar_hdr& hdr,
                              EntryHeader* entry_header);
  bool SkipEntryData(const EntryHeader& entry_header);
  bool ReadEntryData(const EntryHeader& entry_header, std::string* data);
  bool FixEntryName(string* name);
  void Init();

#ifdef __MACH__
  // Clean garbages in ranlib entry.
  static bool CleanIfRanlib(const EntryHeader& hdr, std::string* body);
#endif

  static bool IsSymbolTableEntry(const EntryHeader& entry_header);
  static bool IsLongnameEntry(const EntryHeader& entry_header);

  std::string filename_;
  ScopedFd fd_;
  bool thin_archive_;
  std::string longnames_;
  bool valid_;
  off_t offset_;

  DISALLOW_COPY_AND_ASSIGN(ArFile);
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_ARFILE_H_
