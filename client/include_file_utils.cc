// Copyright 2017 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include_file_utils.h"

#include <memory>

#include <glog/logging.h>

#include "content.h"
#include "path.h"
#include "path_util.h"

namespace devtools_goma {

const char* GOMA_GCH_SUFFIX = ".gch.goma";

bool CreateSubframeworkIncludeFilename(
    const std::string& fwdir, const std::string& current_directory,
    const std::string& include_name, std::string* filename) {
  if (!HasPrefixDir(current_directory, fwdir)) {
    return false;
  }
  size_t pos = current_directory.find('/', fwdir.size()+1);
  if (pos == std::string::npos) {
    return false;
  }
  std::string frameworkdir = current_directory.substr(0, pos+1) + "Frameworks/";
  pos = include_name.find('/');
  if (pos == std::string::npos) {
    return false;
  }
  std::string fwname = include_name.substr(0, pos);
  std::string incpath = include_name.substr(pos+1);
  *filename = file::JoinPath(frameworkdir, fwname + ".framework/Headers",
                             incpath);
  return true;
}

bool ReadHeaderMapContent(
    const std::string& hmap_filename,
    std::vector<std::pair<std::string, std::string>>* entries) {
  DCHECK(entries);

  struct HeaderMapBucket {
    uint32_t key;
    uint32_t prefix;
    uint32_t suffix;
  };

  struct HeaderMap {
    char magic[4];
    uint16_t version;
    uint16_t reserved;
    uint32_t string_offset;
    uint32_t string_count;
    uint32_t hash_capacity;
    uint32_t max_value_length;
    HeaderMapBucket buckets[1];
  };

  std::unique_ptr<Content> file(Content::CreateFromFile(hmap_filename));

  if (!file) {
    LOG(WARNING) << "hmap file not existed: " << hmap_filename;
    return false;
  }

  if (file->size() < sizeof(HeaderMap) - sizeof(HeaderMapBucket)) {
    LOG(WARNING) << "hmap file size is less than expected"
                 << " expected: " << sizeof(HeaderMap) - sizeof(HeaderMapBucket)
                 << " actual: " << file->size()
                 << " file: " << hmap_filename;
    return false;
  }

  const HeaderMap* hmap = reinterpret_cast<const HeaderMap*>(file->buf());
  if (strncmp(hmap->magic, "pamh", 4)) {
    LOG(WARNING) << "Invalid hmap file: " << hmap_filename;
    return false;
  }

  if (hmap->version != 1) {
    LOG(WARNING) << "Unknown hmap version (" << hmap->version
              << "): " << hmap_filename;
    return false;
  }

  const char* buf_end = file->buf_end();

  const char* strings =
      reinterpret_cast<const char*>(hmap) + hmap->string_offset;

  if (strings < file->buf() || buf_end <= strings) {
    LOG(WARNING) << "Invalid string_offset: " << hmap_filename;
    return false;
  }


  if (sizeof(HeaderMap) - sizeof(HeaderMapBucket) +
      static_cast<int64_t>(hmap->hash_capacity) * sizeof(HeaderMapBucket) >
      file->size()) {
    LOG(WARNING) << "hmap file size is less than header map's capacity"
                 << " hash_capacity: " << hmap->hash_capacity
                 << " expected size: "
                 << sizeof(HeaderMap) - sizeof(HeaderMapBucket) +
        static_cast<int64_t>(hmap->hash_capacity) * sizeof(HeaderMapBucket)
                 << " actual size: " << file->size()
                 << " file:" << hmap_filename;
    return false;
  }

  const auto last_nullpos = file->ToStringView().rfind('\0');

  if (last_nullpos == absl::string_view::npos &&
      hmap->hash_capacity != 0) {
    LOG(WARNING) << "hmap file does not contain null character"
                 << " in expected place:" << hmap_filename;
    return false;
  }

  for (size_t i = 0; i < hmap->hash_capacity; i++) {
    const HeaderMapBucket& bucket = hmap->buckets[i];
    if (!bucket.key) {
      continue;
    }

    const char* key = strings + bucket.key;
    const char* prefix = strings + bucket.prefix;
    const char* suffix = strings + bucket.suffix;
    if (key >= buf_end || prefix >= buf_end || suffix >= buf_end ||
        key < file->buf() || prefix < file->buf() || suffix < file->buf() ||
        std::max({key, prefix, suffix}) > last_nullpos + file->buf()) {
      LOG(WARNING) << "Invalid hmap file: " << hmap_filename;
      return false;
    }
    std::string filename(prefix);
    filename += suffix;
    entries->emplace_back(key, filename);
  }

  return true;
}

}  // namespace devtools_goma
