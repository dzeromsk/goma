// Copyright 2013 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "arfile_reader.h"

#include <memory>

#include "glog/logging.h"
#ifdef __MACH__
#include "mach_o_parser.h"
#endif
#include "path_util.h"
#include "scoped_fd.h"
#include "string_piece_utils.h"

namespace devtools_goma {

ArFileReader::ArFileReader(std::unique_ptr<ArFile> arfile)
    : FileReader(arfile->filename()),
      current_offset_(arfile->offset()),
      arfile_(std::move(arfile)), is_valid_(true) {
  if (!arfile_->ReadHeader(&read_buffer_)) {
    VLOG(2) << "invalid arfile:" << arfile->filename();
    is_valid_ = false;
  }
}

/* static */
std::unique_ptr<FileReader> ArFileReader::Create(const string& filename) {
  if (!CanHandle(filename)) {
    return nullptr;
  }

#ifdef __MACH__
  std::unique_ptr<MacFatHeader> f_hdr(new MacFatHeader);
  ScopedFd fd(ScopedFd::OpenForRead(filename));
  if (GetFatHeader(fd, f_hdr.get())) {
    std::unique_ptr<FileReader>
        fr(new FatArFileReader(filename, std::move(f_hdr)));
    if (!fr->valid()) {
      LOG(INFO) << "Invalid .a file: " << filename;
      return nullptr;
    }
    return fr;
  }
#endif

  std::unique_ptr<ArFile> arfile(new ArFile(filename));
  std::unique_ptr<FileReader> fr(new ArFileReader(std::move(arfile)));
  if (!fr->valid()) {
    LOG(INFO) << "Invalid .a file: " << filename;
    return nullptr;
  }
  return fr;
}

/* static */
bool ArFileReader::CanHandle(const string& filename) {
  return strings::EndsWith(filename, ".a");
}

ssize_t ArFileReader::Read(void* ptr, size_t len) {
  size_t read_bytes = 0;
  read_bytes += FileReader::FlushDataInBuffer(&read_buffer_, &ptr, &len);
  while (len > 0) {
    VLOG(3) << "reading ...:"
            << " read_bytes=" << read_bytes
            << " len=" << len
            << " total_off=" << read_bytes + current_offset_;
    ArFile::EntryHeader entry_header;
    string entry_body;
    if (!arfile_->ReadEntry(&entry_header, &entry_body)) {
      LOG(ERROR) << "failed to read entry."
                 << " current_offset_=" << current_offset_
                 << " read_bytes=" << read_bytes
                 << " len=" << len;
      return -1;
    }
    NormalizeArHdr(&entry_header);
    entry_header.SerializeToString(&read_buffer_);
    read_buffer_.append(entry_body);
    read_bytes += FileReader::FlushDataInBuffer(&read_buffer_, &ptr, &len);
  }
  current_offset_ += read_bytes;

  return read_bytes;
}

off_t ArFileReader::Seek(off_t offset, ScopedFd::Whence whence) const {
  // ArFileReader should be asked to seek just next to the last read.
  DCHECK_EQ(whence, ScopedFd::SeekAbsolute)
      << "Sorry, this function only support to set absolute position.";
  DCHECK_EQ(offset, current_offset_)
      << "Sorry, this function expects the users to set just next position"
      << " of the last seek.";
  return offset;
}

void ArFileReader::NormalizeArHdr(ArFile::EntryHeader* hdr) {
  hdr->ar_date = 0;
  hdr->ar_uid = 0;
  hdr->ar_gid = 0;
  hdr->ar_mode = 0;
}

#ifdef __MACH__
FatArFileReader::FatArFileReader(
    const string& filename, std::unique_ptr<MacFatHeader> f_hdr)
    : FileReader(filename),
      is_valid_(true),
      filename_(filename),
      f_hdr_(std::move(f_hdr)),
      current_offset_(0),
      cur_arch_idx_(0),
      create_arfile_reader_factory_(nullptr) {
  Init();
}

FatArFileReader::FatArFileReader(
    const string& filename, std::unique_ptr<MacFatHeader> f_hdr,
    ArFileReaderFactory* create_arfile_reader)
    : FileReader(filename),
      is_valid_(true),
      filename_(filename),
      f_hdr_(std::move(f_hdr)),
      current_offset_(0),
      cur_arch_idx_(0),
      create_arfile_reader_factory_(create_arfile_reader) {
  Init();
}

void FatArFileReader::Init() {
  read_buffer_.assign(&f_hdr_->raw[0], f_hdr_->raw.size());
  cur_arch_ = &f_hdr_->archs[0];
  arr_ = CreateArFileReader(filename_, cur_arch_->offset);
  if (!arr_->valid()) {
    is_valid_ = false;
    return;
  }
  read_buffer_.resize(f_hdr_->raw.size() + cur_arch_->size);
  if (arr_->Read(&read_buffer_[f_hdr_->raw.size()], cur_arch_->size)
      != static_cast<ssize_t>(cur_arch_->size)) {
    LOG(WARNING) << "Read failed:"
                 << " arch=" << cur_arch_->arch_name
                 << " off=" << cur_arch_->offset
                 << " size=" << cur_arch_->size
                 << " buf_size=" << read_buffer_.size();
    is_valid_ = false;
  }
}

ssize_t FatArFileReader::Read(void* ptr, size_t len) {
  size_t read_bytes = 0;
  if (!is_valid_) {
    return -1;
  }

  read_bytes += FileReader::FlushDataInBuffer(&read_buffer_, &ptr, &len);
  while (len > 0) {  // OK, I need to read the next arch.
    cur_arch_idx_++;
    if (cur_arch_idx_ >= f_hdr_->archs.size()) {
      LOG(WARNING) << "No more data:"
                   << " filename=" << filename_
                   << " len=" << len
                   << " off=" << current_offset_ + read_bytes;
      return ReturnReadError(read_bytes);
    }
    cur_arch_ = &f_hdr_->archs[cur_arch_idx_];
    arr_ = CreateArFileReader(filename_, cur_arch_->offset);
    if (!arr_->valid()) {
      is_valid_ = false;
      LOG(WARNING) << "got invalid during reading from arfile."
                   << " filename=" << filename_
                   << " off=" << current_offset_;
      return ReturnReadError(read_bytes);
    }
    read_buffer_.resize(cur_arch_->size);
    ssize_t read_size = arr_->Read(&read_buffer_[0], read_buffer_.size());
    if (read_size == -1) {
      LOG(WARNING) << "Read ar file failed:"
                   << " filename=" << filename_
                   << " off=" << cur_arch_->offset
                   << " size=" << cur_arch_->size;
      return ReturnReadError(read_bytes);
    }
    CHECK_EQ(read_size, static_cast<ssize_t>(read_buffer_.size()));
    CHECK(!read_buffer_.empty());
    read_bytes += FileReader::FlushDataInBuffer(&read_buffer_, &ptr, &len);
  }
  CHECK_EQ(0U, len)
      << "Read failed:"
      << " arch=" << cur_arch_->arch_name
      << " off=" << cur_arch_->offset
      << " size=" << cur_arch_->size
      << " len=" << len
      << " buf_size=" << read_buffer_.size();

  current_offset_ += read_bytes;
  return read_bytes;
}

off_t FatArFileReader::Seek(off_t offset, ScopedFd::Whence whence) const {
  // ArFileReader should be asked to seek just next to the last read.
  DCHECK_EQ(whence, ScopedFd::SeekAbsolute)
      << "Sorry, this function only support to set absolute position.";
  DCHECK_EQ(offset, current_offset_)
      << "Sorry, this function expects the users to set just next position"
      << " of the last seek.";
  return offset;
}

std::unique_ptr<ArFileReader> FatArFileReader::CreateArFileReader(
    const string& filename, off_t offset) {
  if (create_arfile_reader_factory_) {
    return create_arfile_reader_factory_->CreateArFileReader(filename, offset);
  } else {
    std::unique_ptr<ArFile> arfile(new ArFile(filename, offset));
    return std::unique_ptr<ArFileReader>(new ArFileReader(std::move(arfile)));
  }
}

ssize_t FatArFileReader::ReturnReadError(ssize_t read_bytes) {
  is_valid_ = false;
  if (read_bytes == 0)
    return -1;
  return read_bytes;
}

#endif

}  // namespace devtools_goma
