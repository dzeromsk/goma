// Copyright 2013 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_MACH_O_PARSER_H_
#define DEVTOOLS_GOMA_CLIENT_MACH_O_PARSER_H_

#include <sys/types.h>

#include <map>
#include <string>
#include <vector>

#include "basictypes.h"
#include "scoped_fd.h"

using std::string;
struct fat_arch;

namespace devtools_goma {

struct MacFatArch {
  string arch_name;
  off_t offset;
  size_t size;
};

struct MacFatHeader {
  string raw;
  std::vector<MacFatArch> archs;
};

// Gets Fat header from the file.
// Returns true if it is FAT file and succeeded to get the header.
// Otherwise, false.
bool GetFatHeader(const ScopedFd& fd, MacFatHeader* fheader);

class MachO {
 public:
  struct DylibEntry {
    string name;
    uint32_t timestamp;
    uint32_t current_version;
    uint32_t compatibility_version;
  };
  explicit MachO(const string& filename);
  ~MachO();
  bool GetDylibs(const string& cpu_type, std::vector<DylibEntry>* dylibs);
  bool valid() const;

 private:
  std::map<string, fat_arch> archs_;
  string filename_;
  ScopedFd fd_;

  DISALLOW_COPY_AND_ASSIGN(MachO);
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_MACH_O_PARSER_H_
