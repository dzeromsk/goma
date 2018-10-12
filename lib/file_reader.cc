// Copyright 2013 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "lib/file_reader.h"

#include <stdlib.h>
#include <memory>

namespace devtools_goma {

/* static */
void FileReaderFactory::Register(CreateFunction create) {
  GetInstance()->creators_.push_back(create);
}

std::unique_ptr<FileReader> FileReaderFactory::NewFileReader(
    const string& filename) {
  for (std::vector<CreateFunction>::const_iterator iter = creators_.begin();
       iter != creators_.end();
       ++iter) {
    std::unique_ptr<FileReader> reader = (*iter)(filename);
    if (reader) {
      return reader;
    }
  }
  return FileReader::Create(filename);
}

/* static */
FileReaderFactory* FileReaderFactory::GetInstance() {
  if (factory_ == nullptr) {
    factory_ = new FileReaderFactory();
    atexit(FileReaderFactory::DeleteInstance);
  }
  return factory_;
}

/* static */
void FileReaderFactory::DeleteInstance() {
  if (factory_ != nullptr) {
    delete factory_;
  }
}

FileReaderFactory* FileReaderFactory::factory_ = nullptr;

/* static */
size_t FileReader::FlushDataInBuffer(string* buf, void** ptr, size_t* len) {
  size_t moved = 0;
  if (!buf->empty()) {
    if (*len < buf->size()) {
      memcpy(*ptr, buf->data(), *len);
      moved = *len;
      buf->erase(0, *len);
    } else {
      memcpy(*ptr, buf->data(), buf->size());
      moved = buf->size();
      buf->clear();
    }
    *len -= moved;
    *reinterpret_cast<char**>(ptr) += moved;
  }
  return moved;
}

}  // namespace devtools_goma
