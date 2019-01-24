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

// The type of CompilerInfo. If a new type of CompilerInfo is required,
// you can extend this enum class.
enum class CompilerInfoType {
  Cxx,
  Javac,
  Java,
  Fake,
};

inline std::ostream& operator<<(std::ostream& os, CompilerInfoType type) {
  switch (type) {
    case CompilerInfoType::Cxx:
      return os << "cxx";
    case CompilerInfoType::Javac:
      return os << "javac";
    case CompilerInfoType::Java:
      return os << "java";
    case CompilerInfoType::Fake:
      return os << "fake";
  }

  return os << "unknown compiler info type: " << static_cast<int>(type);
}

// CompilerInfo represents how a compiler is configured.
// Used as const object.
// Most of the data is in CompilerInfoData, which is defined in
// compiler_info_data.proto. See also it.
class CompilerInfo {
 public:
  // SubprogramInfo is an information of subprograms. Subprogram means a program
  // that is used during compile. For example: "as", "objdump".
  struct SubprogramInfo {
    SubprogramInfo() {}
    static void FromData(const CompilerInfoData::SubprogramInfo& info_data,
                         SubprogramInfo* info);
    bool IsValid() const {
      return file_stat.IsValid() && !hash.empty() &&
             !user_specified_path.empty() && !abs_path.empty();
    }
    bool operator==(const SubprogramInfo& rhs) const {
      return user_specified_path == rhs.user_specified_path &&
             abs_path == rhs.abs_path && hash == rhs.hash &&
             file_stat == rhs.file_stat;
    }
    string DebugString() const;

    string abs_path;
    string user_specified_path;
    string hash;
    FileStat file_stat;
  };

  // ResourceInfo is an information of compile resources. A resource means a
  // file that might be used during compile implicitly. For example:
  // asan_blacklist.txt for clang with address sanitizer.
  struct ResourceInfo {
    ResourceInfo() {}
    static ResourceInfo FromData(
        const CompilerInfoData::ResourceInfo& info_data);
    bool IsValid() const {
      return file_stat.IsValid() && !hash.empty() && !name.empty();
    }
    bool operator==(const ResourceInfo& rhs) const {
      return name == rhs.name && type == rhs.type && hash == rhs.hash &&
             file_stat == rhs.file_stat && is_executable == rhs.is_executable &&
             symlink_path == rhs.symlink_path;
    }
    string DebugString() const;

    // Returns true if this resource is up to date.
    // Returns false if not, and set the reason to |reason|.
    bool IsUpToDate(const string& cwd, string* reason) const;

    string name;
    CompilerInfoData::ResourceType type;
    string hash;
    FileStat file_stat;
    bool is_executable = false;
    string symlink_path;
  };

  virtual ~CompilerInfo() = default;
  // Returns compiler info type.
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

  // Returns true if CompilerInfo content depends on cwd.
  // compiler path, subprograms paths and resouce paths
  // can be relative paths. In that case, CompilerInfo content depends on
  // cwd.
  //
  // We say CompilerInfo depends on cwd when one of the followings is
  // satisfied.
  //   (a) a path is relative
  //   (b) a path starts with cwd
  // (b) is to cover a path like /path/to/cwd/../../somewhere/to/gcc.
  virtual bool DependsOnCwd(const string& cwd) const;

  // See field's comment below.
  const FileStat& local_compiler_stat() const { return local_compiler_stat_; }
  // See field's comment below.
  const string& local_compiler_path() const {
    return data_->local_compiler_path();
  }
  // Absolute path of local_compiler_path. Joined with cwd if
  // local_compiler_path() is relative.
  string abs_local_compiler_path() const;
  // See field's comment below.
  const string& local_compiler_hash() const {
    return data_->local_compiler_hash();
  }

  // See field's comment below.
  const FileStat& real_compiler_stat() const { return real_compiler_stat_; }
  // The path to real compiler.
  // For the difference between real compiler and local compiler, see the field
  // comment of this class.
  const string& real_compiler_path() const {
    return data_->real_compiler_path();
  }
  // See field's comment below.
  const string& real_compiler_hash() const {
    return data_->hash();
  }

  // compiler hash to identify the compiler in backend.
  const string& request_compiler_hash() const;

  // compiler family name. (e.g. gcc, g++, clang, clang++).
  // For example, if compiler's basename is "x86_64-linux-gcc-7", name will be
  // "gcc".
  const string& name() const { return data_->name(); }
  // Returns true if name is defined.
  bool HasName() const { return data_->has_name(); }

  // compiler's version. e.g. "4.2.1[clang version 7.0.0 (trunk 338452)]"
  const string& version() const { return data_->version(); }
  // compiler's target. e.g. "x86_64-pc-linux-gnu"
  const string& target() const { return data_->target(); }
  // input source's language. e.g. "c++". The compiler will treat the input
  // language is this.
  const string& lang() const { return data_->lang(); }
  // If taking CopmilerInfo is failed, error message is stored here.
  const string& error_message() const { return data_->error_message(); }

  // See field's comment below.
  const std::vector<string>& additional_flags() const {
    return additional_flags_;
  }
  // Returns true if additional flags exist.
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

  // The internal data of CompilerInfo.
  std::unique_ptr<CompilerInfoData> data_;

  // Note about "local compiler" and "real compiler".
  // Some project uses a compiler wrapper (e.g. chromeos).
  //
  // For example, "gcc" might be just a python script, and it invokes
  // a real "gcc". Even the compiler wrapper wasn't changed, the real
  // compiler might be changed. We have to detect this case.
  //
  // When such a wrapper exists, we think the wrapper is "local compiler",
  // and the real compiler as "real compiler".
  //
  // Otherwise, local_compiler and real_compiler are the same.

  // Local compiler's FileStat.
  FileStat local_compiler_stat_;
  // Real compiler's FileStat if real_compiler_path != local_compiler_path.
  // Otherwise, real_compiler_stat is the same as local_compiler_stat.
  FileStat real_compiler_stat_;

  // Additional flags to correct compile arguments in remote.
  // These flags will be automatically added to compile flags.
  // e.g. -resource-dir for clang.
  std::vector<string> additional_flags_;

  // A list of subprograms specified by -B flag.
  std::vector<SubprogramInfo> subprograms_;

  // Additional resources that a compiler will use during a compile.
  // e.g. clang with address sanizier will use
  // "<resource_dir>/share/asan_blacklist.txt" during a compile.
  std::vector<ResourceInfo> resource_;

  // Protects data_->last_used_at.
  mutable ReadWriteLock last_used_at_mu_;

  DISALLOW_COPY_AND_ASSIGN(CompilerInfo);
};

inline void SetFileStatToData(const FileStat& file_stat,
                              CompilerInfoData::FileStat* data) {
  // TODO: Use protobuf/timestamp.
  data->set_mtime(file_stat.mtime ? absl::ToTimeT(*file_stat.mtime) : 0);
  data->set_size(file_stat.size);
  data->set_is_directory(file_stat.is_directory);
}

inline void GetFileStatFromData(const CompilerInfoData::FileStat& data,
                                FileStat* file_stat) {
  file_stat->mtime = absl::FromTimeT(data.mtime());
  file_stat->size = data.size();
  file_stat->is_directory = data.is_directory();
}

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_COMPILER_INFO_H_
