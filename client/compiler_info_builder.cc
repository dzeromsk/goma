// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "compiler_info_builder.h"

#include "absl/time/clock.h"
#include "compiler_flag_type_specific.h"
#include "compiler_info.h"
#include "counterz.h"
#include "glog/logging.h"
#include "goma_hash.h"
#include "path.h"
#include "path_resolver.h"

namespace devtools_goma {

/* static */
std::unique_ptr<CompilerInfoData> CompilerInfoBuilder::FillFromCompilerOutputs(
    const CompilerFlags& flags,
    const string& local_compiler_path,
    const std::vector<string>& compiler_info_envs) {
  GOMA_COUNTERZ("");
  std::unique_ptr<CompilerInfoData> data(new CompilerInfoData);
  SetLanguageExtension(data.get());

  data->set_last_used_at(time(nullptr));

  // TODO: minimize the execution of ReadCommandOutput.
  // If we execute gcc/clang with -xc -v for example, we can get not only
  // real compiler path but also target and version.
  // However, I understand we need large refactoring of CompilerInfo
  // for minimizing the execution while keeping readability.
  SetCompilerPath(flags, local_compiler_path, compiler_info_envs, data.get());

  if (!file::IsAbsolutePath(local_compiler_path)) {
    data->set_cwd(flags.cwd());
  }

  const string& abs_local_compiler_path = PathResolver::ResolvePath(
      file::JoinPathRespectAbsolute(flags.cwd(), data->local_compiler_path()));
  VLOG(2) << "FillFromCompilerOutputs:"
          << " abs_local_compiler_path=" << abs_local_compiler_path
          << " cwd=" << flags.cwd()
          << " local_compiler_path=" << data->local_compiler_path();
  data->set_real_compiler_path(PathResolver::ResolvePath(
      file::JoinPathRespectAbsolute(flags.cwd(), data->real_compiler_path())));

  if (!hash_cache_.GetHashFromCacheOrFile(
          abs_local_compiler_path, data->mutable_local_compiler_hash())) {
    LOG(ERROR) << "Could not open local compiler file "
               << abs_local_compiler_path;
    data->set_found(false);
    return data;
  }

  if (!hash_cache_.GetHashFromCacheOrFile(data->real_compiler_path(),
                                          data->mutable_hash())) {
    LOG(ERROR) << "Could not open real compiler file "
               << data->real_compiler_path();
    data->set_found(false);
    return data;
  }

  data->set_name(GetCompilerName(*data));
  if (data->name().empty()) {
    AddErrorMessage("Failed to get compiler name of " + abs_local_compiler_path,
                    data.get());
    LOG(ERROR) << data->error_message();
    return data;
  }
  data->set_lang(flags.lang());

  FileStat local_compiler_stat(abs_local_compiler_path);
  if (!local_compiler_stat.IsValid()) {
    LOG(ERROR) << "Failed to get file id of " << abs_local_compiler_path;
    data->set_found(false);
    return data;
  }
  SetFileStatToData(local_compiler_stat, data->mutable_local_compiler_stat());
  data->mutable_real_compiler_stat()->CopyFrom(data->local_compiler_stat());

  data->set_found(true);

  if (abs_local_compiler_path != data->real_compiler_path()) {
    FileStat real_compiler_stat(data->real_compiler_path());
    if (!real_compiler_stat.IsValid()) {
      LOG(ERROR) << "Failed to get file id of " << data->real_compiler_path();
      data->set_found(false);
      return data;
    }
    SetFileStatToData(real_compiler_stat, data->mutable_real_compiler_stat());
  }

  SetTypeSpecificCompilerInfo(flags, local_compiler_path,
                              abs_local_compiler_path, compiler_info_envs,
                              data.get());
  return data;
}

void CompilerInfoBuilder::SetCompilerPath(
    const CompilerFlags& flags,
    const string& local_compiler_path,
    const std::vector<string>& compiler_info_envs,
    CompilerInfoData* data) const {
  data->set_local_compiler_path(local_compiler_path);
  data->set_real_compiler_path(local_compiler_path);
}

string CompilerInfoBuilder::GetCompilerName(
    const CompilerInfoData& data) const {
  // The default implementation is to return compilername from local compiler
  // path.
  return CompilerFlagTypeSpecific::GetCompilerNameFromArg(
      data.local_compiler_path());
}

/* static */
void CompilerInfoBuilder::AddErrorMessage(const std::string& message,
                                          CompilerInfoData* compiler_info) {
  if (compiler_info->failed_at() == 0)
    compiler_info->set_failed_at(absl::ToTimeT(absl::Now()));

  if (compiler_info->has_error_message()) {
    compiler_info->set_error_message(compiler_info->error_message() + "\n");
  }
  compiler_info->set_error_message(compiler_info->error_message() + message);
}

/* static */
void CompilerInfoBuilder::OverrideError(const std::string& message,
                                        absl::optional<absl::Time> failed_at,
                                        CompilerInfoData* compiler_info) {
  DCHECK((message.empty() && !failed_at.has_value()) ||
         (!message.empty() && failed_at.has_value()));
  compiler_info->set_error_message(message);
  if (failed_at.has_value()) {
    compiler_info->set_failed_at(absl::ToTimeT(*failed_at));
  }
}

/* static */
bool CompilerInfoBuilder::ResourceInfoFromPath(
    const string& cwd,
    const string& path,
    CompilerInfoData::ResourceType type,
    CompilerInfoData::ResourceInfo* r) {
  const string abs_path = file::JoinPathRespectAbsolute(cwd, path);
  FileStat file_stat(abs_path);
  if (!file_stat.IsValid()) {
    return false;
  }
  string hash;
  if (!GomaSha256FromFile(abs_path, &hash)) {
    return false;
  }
  r->set_name(path);
  r->set_type(type);
  r->set_hash(std::move(hash));
  SetFileStatToData(file_stat, r->mutable_file_stat());
  return true;
}

}  // namespace devtools_goma
