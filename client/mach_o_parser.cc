// Copyright 2013 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "mach_o_parser.h"

#include <sys/mman.h>

// expect followings would be executed in Mac OS X, or provide headers.
#include <mach-o/fat.h>
#include <mach-o/loader.h>

#include <glog/logging.h>

#include "scoped_fd.h"

namespace {

static void SwapFatArchByteOrder(fat_arch* arch) {
  arch->cputype = OSSwapInt32(arch->cputype);
  arch->cpusubtype = OSSwapInt32(arch->cpusubtype);
  arch->offset = OSSwapInt32(arch->offset);
  arch->size = OSSwapInt32(arch->size);
  arch->align = OSSwapInt32(arch->align);
}

static const string GetArchName(cpu_type_t type, cpu_subtype_t subtype) {
  if (type == CPU_TYPE_I386 && subtype == CPU_SUBTYPE_I386_ALL) {
    return "i386";
  } else if (type == CPU_TYPE_X86_64 && subtype == CPU_SUBTYPE_X86_64_ALL) {
    return "x86_64";
  } else if (type == CPU_TYPE_POWERPC) {
    return "powerpc";
  } else {
    LOG(ERROR) << "unknown CPU type or subtype found:"
                 << " cpu_type=" << type
                 << " cpu_subtype=" << subtype;
    return "";
  }
}

bool GetFatArchs(
    const devtools_goma::ScopedFd& fd,
    std::vector<fat_arch>* archs,
    string* raw) {
  DCHECK(archs);
  // Parse fat header.
  if (fd.Seek(0, devtools_goma::ScopedFd::SeekAbsolute)
      == static_cast<off_t>(-1)) {
    PLOG(WARNING) << "seek 0: fd=" << fd;
    return false;
  }

  fat_header header;
  if (fd.Read(&header, sizeof(header)) != sizeof(header)) {
    PLOG(WARNING) << "read fat header:" << fd;
    return false;
  }
  if (raw != nullptr)
    raw->assign(reinterpret_cast<char*>(&header), sizeof(header));
  bool reversed;
  if (header.magic == FAT_MAGIC) {
    reversed = false;
  } else if (header.magic == FAT_CIGAM) {
    reversed = true;
  } else {
    // Since we may ask GetFatArch to read the file, it won't be error.
    VLOG(1) << "not a FAT file magic: "
            << " fd=" << fd
            << " magic=" << std::hex << header.magic;
    return false;
  }

  // Parse fat arch.
  if (reversed) {
    header.nfat_arch = OSSwapInt32(header.nfat_arch);
  }
  for (uint32_t i = 0; i < header.nfat_arch; ++i) {
    fat_arch arch;
    if (fd.Read(&arch, sizeof(arch)) != sizeof(arch)) {
      PLOG(WARNING) << "read fat arch:"
                    << " entry_id=" << i
                    << " fd=" << fd;
      return false;
    }
    if (raw != nullptr)
      raw->append(reinterpret_cast<char*>(&arch), sizeof(arch));
    if (reversed) {
      SwapFatArchByteOrder(&arch);
    }
    archs->push_back(arch);
  }
  return true;
}

}  // namespace

namespace devtools_goma {

bool GetFatHeader(const ScopedFd& fd, MacFatHeader* fheader) {
  std::vector<fat_arch> archs;
  if (!GetFatArchs(fd, &archs, &fheader->raw))
    return false;

  for (std::vector<fat_arch>::iterator it = archs.begin();
       it != archs.end(); ++it) {
    MacFatArch arch;
    arch.arch_name = GetArchName(it->cputype, it->cpusubtype);
    arch.offset = it->offset;
    arch.size = it->size;
    fheader->archs.push_back(arch);
    VLOG(1) << "fat:"
            << " arch=" << arch.arch_name
            << " offset=" << arch.offset
            << " size=" << arch.size;
  }

  return true;
}

MachO::MachO(const string& filename)
    : filename_(filename) {
  fd_.reset(ScopedFd::OpenForRead(filename));
  // TODO: support non-fat mach object if needed.
  std::vector<fat_arch> archs;
  if (!GetFatArchs(fd_, &archs, nullptr)) {
    LOG(WARNING) << "Cannot read FAT header:"
                 << " filename=" << filename
                 << " fd=" << fd_;
  }
  for (std::vector<fat_arch>::iterator it = archs.begin();
       it != archs.end(); ++it) {
    archs_.insert(make_pair(
         GetArchName(it->cputype, it->cpusubtype),
         *it));
  }
}

MachO::~MachO() {
}

bool MachO::GetDylibs(const string& cpu_type, std::vector<DylibEntry>* dylibs) {
  std::map<string, fat_arch>::const_iterator found = archs_.find(cpu_type);
  if (found == archs_.end()) {
    LOG(WARNING) << "unknown cpu type: " << cpu_type;
    return false;
  }

  const size_t offset = found->second.offset;
  const size_t len = found->second.size;
  VLOG(1) << "mmap "
          << " len=" << len
          << " offset=" << offset;
  char* mmapped = reinterpret_cast<char*>(
      mmap(nullptr, len, PROT_READ, MAP_PRIVATE, fd_.fd(), offset));
  if (mmapped == MAP_FAILED) {
    LOG(ERROR) << "mmap failed:"
               << " filename=" << filename_
               << " fd=" << fd_.fd()
               << " len=" << len
               << " offset=" << offset;
    return false;
  }
  const mach_header* header = reinterpret_cast<const mach_header*>(mmapped);
  load_command* command;
  if (header->magic == MH_MAGIC) {
    command = reinterpret_cast<load_command*>(mmapped + sizeof(mach_header));
  } else if (header->magic == MH_MAGIC_64) {
    command = reinterpret_cast<load_command*>(mmapped + sizeof(mach_header_64));
  } else {
    // We might not see the different endian mach object.
    if (header->magic == MH_CIGAM || header->magic == MH_CIGAM_64) {
      LOG(WARNING) << "Mach object with non-supported endian.";
    }
    LOG(WARNING) << "strange magic: "
                 << " filename=" << filename_
                 << " magic=" << std::hex << header->magic;
    munmap(mmapped, len);
    return false;
  }
  VLOG(1) << "mach header info:"
          << " magic=" << header->magic
          << " cputype=" << header->cputype
          << " cpusubtype=" << header->cpusubtype
          << " filetype=" << header->filetype
          << " ncmds=" << header->ncmds
          << " sizeofcmds=" << header->sizeofcmds
          << " flags=" << header->flags;
  CHECK_EQ(header->cputype, found->second.cputype);
  CHECK_EQ(header->cpusubtype, found->second.cpusubtype);

  for (uint32_t i = 0; i < header->ncmds; ++i) {
    // Since we do not support different endian, we do not convert |command|.
    // If we support different endian, we should also convert data structures
    // used in this loop.
    VLOG(2) << "cmd:"
            << " type=" << std::hex << command->cmd
            << " size=" << command->cmdsize;
    switch (command->cmd) {
      case LC_IDFVMLIB:
        FALLTHROUGH_INTENDED;
      case LC_LOADFVMLIB:
        LOG(ERROR) << "Sorry, FVMLIB support is not implemented yet.";
        break;
      case LC_LOAD_DYLIB:
        FALLTHROUGH_INTENDED;
      case LC_LOAD_WEAK_DYLIB:
        FALLTHROUGH_INTENDED;
      case LC_REEXPORT_DYLIB:
        {
          dylib_command* dycom = reinterpret_cast<dylib_command*>(command);
          if (dycom->dylib.name.offset < command->cmdsize) {
            DylibEntry entry;
            entry.name = string(reinterpret_cast<char*>(command) +
                dycom->dylib.name.offset);
            entry.timestamp = dycom->dylib.timestamp;
            entry.current_version = dycom->dylib.current_version;
            entry.compatibility_version = dycom->dylib.compatibility_version;
            dylibs->push_back(entry);
          } else {
            LOG(WARNING) << "dylib command broken:"
                         << " cmd=" << command->cmd
                         << " cmdsize=" << command->cmdsize
                         << " dylib.name.offset=" << dycom->dylib.name.offset;
          }
        }
        break;
      default:
        VLOG(2) << "command is skipped:"
                << " type=" << std::hex << command->cmd
                << " size=" << command->cmdsize;
        break;
    }
    command = reinterpret_cast<load_command*>(
        reinterpret_cast<char*>(command) + command->cmdsize);
    CHECK_GT(reinterpret_cast<char*>(command), mmapped);
    CHECK_LT(reinterpret_cast<char*>(command), mmapped + len);
  }

  munmap(mmapped, len);
  return true;
}

bool MachO::valid() const {
  return fd_.valid();
}

}  // namespace devtools_goma
