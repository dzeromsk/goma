// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cxx_compiler_info_builder.h"

#include "absl/strings/str_split.h"
#include "cmdline_parser.h"
#include "compiler_info.h"
#include "counterz.h"
#include "glog/logging.h"
#include "glog/stl_logging.h"
#include "goma_hash.h"
#include "ioutil.h"
#include "path.h"
#include "scoped_tmp_file.h"
#include "util.h"

namespace devtools_goma {

/* static */
void CxxCompilerInfoBuilder::ParseGetSubprogramsOutput(
    const string& gcc_output,
    std::vector<string>* paths) {
  const std::vector<string> candidates = {"as",      "objcopy", "cc1",
                                          "cc1plus", "cpp",     "nm"};
  std::set<string> known;

  for (auto&& line :
       absl::StrSplit(gcc_output, absl::ByAnyChar("\r\n"), absl::SkipEmpty())) {
    if (!absl::StartsWith(line, " ")) {
      continue;
    }
    std::vector<string> argv;
    // Since clang is not used on Windows now, this won't be the issue.
    ParsePosixCommandLineToArgv(line, &argv);
    if (argv.empty()) {
      continue;
    }
    const string& cmd = argv[0];
    absl::string_view basename = file::Basename(cmd);
    if (basename == cmd) {
      // To keep backword compatibility, we do not add subprogram searched
      // in PATH.
      LOG(INFO) << "ignore subprogram searched in PATH."
                << " cmd=" << cmd;
      continue;
    }
    if (!known.insert(cmd).second) {
      continue;
    }
    for (const auto& candidate : candidates) {
      if (basename == candidate || absl::EndsWith(basename, "-" + candidate)) {
        paths->push_back(cmd);
        break;
      }
    }
  }
}

/* static */
bool CxxCompilerInfoBuilder::GetSubprograms(
    const string& gcc_path,
    const string& lang,
    const std::vector<string>& compiler_info_flags,
    const std::vector<string>& compiler_info_envs,
    const string& cwd,
    bool warn_on_empty,
    std::vector<string>* subprogs) {
  std::vector<string> argv = {gcc_path};
  copy(compiler_info_flags.begin(), compiler_info_flags.end(),
       back_inserter(argv));
  // Since a compiler returns EXIT_FAILURE if fails to output file,
  // we need to use a fake temporary file.
  // Failure of writing *.dwo might be the reason.
  ScopedTmpDir tmp("get_subprograms");
  if (!tmp.valid()) {
    LOG(ERROR) << "cannot make an empty directory";
    return false;
  }
#ifdef _WIN32
  // This code is used by NaCl gcc, PNaCl clang on Windows.
  // Former uses /dev/null as null device, and latter recently uses NUL as
  // null device.  To provide the same code to both, let me use temporary
  // file for that.
  ScopedTmpFile tmpfile("get_subprograms");
  if (!tmpfile.valid()) {
    LOG(ERROR) << "cannot make an empty file";
    return false;
  }
  tmpfile.Close();
  const string& empty_file = tmpfile.filename();
  VLOG(2) << "empty_file=" << empty_file;
#else
  const string& empty_file = "/dev/null";
#endif
  const string output_file = file::JoinPath(tmp.dirname(), "output");
  VLOG(2) << "output_file=" << output_file;
  argv.emplace_back("-x" + lang);
  argv.emplace_back("-c");
  argv.emplace_back(empty_file);
  argv.emplace_back("-o");
  argv.emplace_back(output_file);
  argv.emplace_back("-v");
  int32_t status;
  string gcc_output;
  {
    GOMA_COUNTERZ("ReadCommandOutput(subprogram)");
    gcc_output = ReadCommandOutput(gcc_path, argv, compiler_info_envs, cwd,
                                   MERGE_STDOUT_STDERR, &status);
  }
  if (status != 0) {
    LOG(ERROR) << "ReadCommandOutput exited with non zero status code."
               << " gcc_path=" << gcc_path << " status=" << status
               << " argv=" << argv << " env=" << compiler_info_envs
               << " cwd=" << cwd << " gcc_output=" << gcc_output;
    return false;
  }
  VLOG(1) << "GetSubprograms:"
          << " gcc_path=" << gcc_path << " status=" << status
          << " argv=" << argv << " env=" << compiler_info_envs << " cwd=" << cwd
          << " gcc_output=" << gcc_output;
  ParseGetSubprogramsOutput(gcc_output, subprogs);
  LOG_IF(ERROR, warn_on_empty && subprogs->empty())
      << "Expect to have at least one subprograms but empty."
      << " gcc_path=" << gcc_path << " status=" << status << " argv=" << argv
      << " env=" << compiler_info_envs << " cwd=" << cwd
      << " gcc_output=" << gcc_output;
  return true;
}

/* static */
string CxxCompilerInfoBuilder::GetRealSubprogramPath(
    const string& subprog_path) {
#ifndef __linux__
  return subprog_path;
#else
  // Currently, we only see objcopy runs via shell script wrapper, and
  // nothing else (i.e. no as or so). (b/30571185)
  if (file::Basename(subprog_path) != "objcopy") {
    return subprog_path;
  }

  // Assume ChromeOS objcopy is always in
  // "<target arch>/binutils-bin/<version>-gold/objcopy",
  // and real objcopy is in
  // "<target arch>/binutils-bin/<version>/objcopy.elf".
  if (file::Basename(file::Dirname(file::Dirname(subprog_path))) !=
      "binutils-bin") {
    return subprog_path;
  }
  absl::string_view dirname = file::Dirname(subprog_path);
  static const char kGoldSuffix[] = "-gold";
  if (absl::EndsWith(dirname, kGoldSuffix)) {
    dirname.remove_suffix(sizeof(kGoldSuffix) - 1);
  }
  const string new_subprog_path = file::JoinPath(dirname, "objcopy.elf");
  FileStat new_id(new_subprog_path);
  if (!new_id.IsValid()) {
    LOG(INFO) << ".elf does not exist, might not be chromeos path?"
              << " expect to exist=" << new_subprog_path
              << " orignal subprog_path=" << subprog_path;
    return subprog_path;
  }
  LOG(INFO) << "Hack for objcopy used for ChromeOS simple chrome build:"
            << " apparent subprog_path=" << subprog_path
            << " real subprog_path=" << new_subprog_path;
  return new_subprog_path;
#endif
}

/* static */
bool CxxCompilerInfoBuilder::SubprogramInfoFromPath(
    const string& path,
    CompilerInfoData::SubprogramInfo* s) {
  FileStat file_stat(path);
  if (!file_stat.IsValid()) {
    return false;
  }
  string hash;
  if (!GomaSha256FromFile(GetRealSubprogramPath(path), &hash)) {
    return false;
  }
  s->set_name(path);
  s->set_hash(hash);
  SetFileStatToData(file_stat, s->mutable_file_stat());
  return true;
}

}  // namespace devtools_goma
