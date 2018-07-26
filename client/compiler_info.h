// Copyright 2010 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_COMPILER_INFO_H_
#define DEVTOOLS_GOMA_CLIENT_COMPILER_INFO_H_

#include <ostream>
#include <string>

#include "absl/time/time.h"
#include "absl/types/optional.h"
#include "compiler_specific.h"
#include "file_stat.h"
#include "lockhelper.h"
#include "sha256_hash_cache.h"

MSVC_PUSH_DISABLE_WARNING_FOR_PROTO()
#include "prototmp/compiler_info_data.pb.h"
MSVC_POP_WARNING()

using std::string;

namespace devtools_goma {

class CompilerFlags;

enum class CompilerInfoType {
  Cxx,
  Javac,
  Java,
};

inline std::ostream& operator<<(std::ostream& os, CompilerInfoType type) {
  switch (type) {
    case CompilerInfoType::Cxx:
      return os << "cxx";
    case CompilerInfoType::Javac:
      return os << "javac";
    case CompilerInfoType::Java:
      return os << "java";
  }

  return os << "unknown compiler info type: " << static_cast<int>(type);
}

// Represent how a compiler is configured.
// Used as const object.
class CompilerInfo {
 public:
  struct SubprogramInfo {
    SubprogramInfo() {}
    static void FromData(const CompilerInfoData::SubprogramInfo& info_data,
                         SubprogramInfo* info);
    bool IsValid() const {
      return file_stat.IsValid() && !hash.empty() && !name.empty();
    }
    bool operator==(const SubprogramInfo& rhs) const {
      return name == rhs.name && hash == rhs.hash && file_stat == rhs.file_stat;
    }
    string DebugString() const;

    string name;
    string hash;
    FileStat file_stat;
  };

  struct ResourceInfo {
    ResourceInfo() {}
    static ResourceInfo FromData(
        const CompilerInfoData::ResourceInfo& info_data);
    bool IsValid() const {
      return file_stat.IsValid() && !hash.empty() && !name.empty();
    }
    bool operator==(const ResourceInfo& rhs) const {
      return name == rhs.name && type == rhs.type &&
          hash == rhs.hash && file_stat == rhs.file_stat;
    }
    string DebugString() const;

    string name;
    CompilerInfoData::ResourceType type;
    string hash;
    FileStat file_stat;
  };

  virtual ~CompilerInfo() = default;
  virtual CompilerInfoType type() const = 0;

  string DebugString() const;

  // Returns true if |local_compiler_path| is up to date.
  // i.e. FileStat of |local_compiler_path| matches |local_compiler_stat|.
  bool IsUpToDate(const string& local_compiler_path) const;

  // Updates FileStat to the current FileStat when hash is matched.
  // Returns false if hash doesn't match.
  bool UpdateFileStatIfHashMatch(SHA256HashCache* sha256_cache);

  // Returns true if CompilerInfo has some error.
  bool HasError() const { return data_->has_error_message(); }

  virtual bool IsCwdRelative(const string& cwd) const;

  const FileStat& local_compiler_stat() const { return local_compiler_stat_; }
  const string& local_compiler_path() const {
    return data_->local_compiler_path();
  }
  string abs_local_compiler_path() const;
  const string& local_compiler_hash() const {
    return data_->local_compiler_hash();
  }

  const FileStat& real_compiler_stat() const { return real_compiler_stat_; }
  const string& real_compiler_path() const {
    return data_->real_compiler_path();
  }
  const string& real_compiler_hash() const {
    return data_->hash();
  }

  // compiler hash to identify the compiler in backend.
  const string& request_compiler_hash() const;

  const string& name() const { return data_->name(); }
  bool HasName() const { return data_->has_name(); }

  const string& version() const { return data_->version(); }
  const string& target() const { return data_->target(); }
  const string& lang() const { return data_->lang(); }
  const string& error_message() const { return data_->error_message(); }

  const std::vector<string>& additional_flags() const {
    return additional_flags_;
  }
  bool HasAdditionalFlags() const { return !additional_flags_.empty(); }
  const std::vector<SubprogramInfo>& subprograms() const {
    return subprograms_;
  }
  const std::vector<ResourceInfo>& resource() const {
    return resource_;
  }

  absl::optional<absl::Time> failed_at() const {
    if (data_->failed_at() == 0) {
      return absl::nullopt;
    }
    return absl::FromTimeT(data_->failed_at());
  }

  absl::Time last_used_at() const;
  void set_last_used_at(absl::Time t);

  bool found() const { return data_->found(); }

  bool IsSameCompiler(const CompilerInfo& ci) const {
    return data_->target() == ci.data_->target()
        && data_->version() == ci.data_->version()
        && data_->lang() == ci.data_->lang()
        && data_->hash() == ci.data_->hash()
        && data_->real_compiler_path() == ci.data_->real_compiler_path();
  }

  const CompilerInfoData& data() const { return *data_; }
  CompilerInfoData* mutable_data() { return data_.get(); }

 protected:
  friend class CompilerInfoCacheTest;

  explicit CompilerInfo(std::unique_ptr<CompilerInfoData> data);

  std::unique_ptr<CompilerInfoData> data_;

  FileStat local_compiler_stat_;
  // Real compiler's FileStat if real_compiler_path != local_compiler_path.
  // Otherwise, real_compiler_stat is the same as local_compiler_stat.
  FileStat real_compiler_stat_;

  std::vector<string> additional_flags_;

  // A list of subprograms specified by -B flag.
  std::vector<SubprogramInfo> subprograms_;

  std::vector<ResourceInfo> resource_;

  mutable ReadWriteLock last_used_at_mu_;

  DISALLOW_COPY_AND_ASSIGN(CompilerInfo);
};

inline void SetFileStatToData(const FileStat& file_stat,
                              CompilerInfoData::FileStat* data) {
  data->set_mtime(file_stat.mtime);
  data->set_size(file_stat.size);
  data->set_is_directory(file_stat.is_directory);
}

inline void GetFileStatFromData(const CompilerInfoData::FileStat& data,
                                FileStat* file_stat) {
  file_stat->mtime = data.mtime();
  file_stat->size = data.size();
  file_stat->is_directory = data.is_directory();
}

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_COMPILER_INFO_H_
