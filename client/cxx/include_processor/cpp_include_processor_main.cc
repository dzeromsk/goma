// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cpp_include_processor.h"

#include <time.h>

#ifndef _WIN32
#include <unistd.h>
#endif

#include <iostream>

#include "absl/strings/str_split.h"
#include "compiler_flags.h"
#include "compiler_flags_parser.h"
#include "compiler_type_specific_collection.h"
#include "cxx/cxx_compiler_info.h"
#include "file_helper.h"
#include "glog/logging.h"
#include "glog/stl_logging.h"
#include "goma_init.h"
#include "include_cache.h"
#include "list_dir_cache.h"
#include "mypath.h"
#include "path.h"
#include "path_resolver.h"
#include "scoped_tmp_file.h"
#include "subprocess.h"

#if HAVE_CPU_PROFILER
#include <gperftools/profiler.h>
#endif

using devtools_goma::CompilerTypeSpecificCollection;
using devtools_goma::GCCCompilerInfoBuilder;

// TODO: share this code with include_processor_unittest.
std::set<string> GetExpectedFiles(const std::vector<string>& args,
                                  const std::vector<string>& env,
                                  const string& cwd) {
  std::set<string> expected_files;
#ifndef _WIN32
  // TODO: ReadCommandOutputByPopen couldn't read large outputs
  // and causes exit=512, so use tmpfile.
  devtools_goma::ScopedTmpFile tmpfile("include_processor_verify");
  tmpfile.Close();
  std::vector<string> run_args;
  for (size_t i = 0; i < args.size(); ++i) {
    const string& arg = args[i];
    if (strncmp(arg.c_str(), "-M", 2) == 0) {
      if (arg == "-MF" || arg == "-MT" || arg == "-MQ") {
        ++i;
      }
      continue;
    }
    if (arg == "-o") {
      ++i;
      continue;
    }
    if (strncmp(arg.c_str(), "-o", 2) == 0) {
      continue;
    }
    run_args.push_back(arg);
  }
  run_args.push_back("-M");
  run_args.push_back("-MF");
  run_args.push_back(tmpfile.filename());

  std::vector<string> run_env(env);
  run_env.push_back("LC_ALL=C");

  // The output format of -M will be
  //
  // stdio: /usr/include/stdio.h /usr/include/features.h \\\n
  //   /usr/include/sys/cdefs.h /usr/include/bits/wordsize.h \\\n
  //   ...
  int status;
  devtools_goma::ReadCommandOutputByPopen(run_args[0], run_args, run_env, cwd,
                                          devtools_goma::MERGE_STDOUT_STDERR,
                                          &status);
  if (status != 0) {
    LOG(INFO) << "args:" << run_args;
    LOG(INFO) << "env:" << run_env;
    LOG(FATAL) << "status:" << status;
  }
  string output;
  CHECK(devtools_goma::ReadFileToString(tmpfile.filename(), &output));
  std::vector<string> files = ToVector(
      absl::StrSplit(output, absl::ByAnyChar(" \n\r\\"), absl::SkipEmpty()));
  devtools_goma::PathResolver pr;
  // Skip the first element as it's the make target.
  for (size_t i = 1; i < files.size(); i++) {
    const string& file = files[i];
    // Need normalization as GCC may output a same file in different way.
    // TODO: don't use ResolvePath.
    expected_files.insert(
        pr.ResolvePath(file::JoinPathRespectAbsolute(cwd, file)));
  }
#endif
  return expected_files;
}

std::set<string> NormalizePaths(const string& cwd,
                                const std::set<string>& paths) {
  std::set<string> normalized;
  for (const auto& iter : paths) {
    normalized.insert(devtools_goma::PathResolver::ResolvePath(
        file::JoinPathRespectAbsolute(cwd, iter)));
  }
  return normalized;
}

int CompareFiles(const std::set<string>& expected_files,
                 const std::set<string>& actual_files) {
  std::vector<string> matched;
  std::vector<string> extra;
  std::vector<string> missing;
  std::set_intersection(expected_files.begin(), expected_files.end(),
                        actual_files.begin(), actual_files.end(),
                        back_inserter(matched));

  std::set_difference(actual_files.begin(), actual_files.end(),
                      expected_files.begin(), expected_files.end(),
                      back_inserter(extra));

  std::set_difference(expected_files.begin(), expected_files.end(),
                      actual_files.begin(), actual_files.end(),
                      back_inserter(missing));

  for (const auto& extra_iter : extra) {
    LOG(INFO) << "Extra include:" << extra_iter;
  }
  for (const auto& missing_iter : missing) {
    LOG(ERROR) << "Missing include:" << missing_iter;
  }

  LOG(INFO) << "matched:" << matched.size() << " extra:" << extra.size()
            << " missing:" << missing.size();

  return missing.size();
}

void GetAdditionalEnv(const char** envp,
                      const char* name,
                      std::vector<string>* envs) {
  int namelen = strlen(name);
  for (const char** e = envp; *e; e++) {
    if (
#ifdef _WIN32
        _strnicmp(*e, name, namelen) == 0
#else
        strncmp(*e, name, namelen) == 0
#endif
        && (*e)[namelen] == '=') {
      envs->push_back(*e);
      return;
    }
  }
}

int main(int argc, char* argv[], const char** envp) {
  devtools_goma::Init(argc, argv, envp);
  devtools_goma::InitLogging(argv[0]);

  devtools_goma::ListDirCache::Init(1024);
  devtools_goma::IncludeCache::Init(32, false);

  bool verify_mode = false;
  if (argc >= 2 && !strcmp(argv[1], "--verify")) {
    verify_mode = true;
    argc--;
    argv++;
#ifdef _WIN32
    std::cerr << "--verify is not yet supported on win32" << std::endl;
    exit(1);
#endif
  }

  int loop_count = 1;
  if (argc >= 2 && absl::StartsWith(argv[1], "--count=")) {
    loop_count = atoi(argv[1] + 8);
    argc--;
    argv++;

    std::cerr << "Run CppIncludeProcessor::GetIncludeFiles " << loop_count
              << " times." << std::endl;
  }

#ifndef _WIN32
  if (argc == 1) {
    std::cerr << argv[0] << " [full path of local compiler [args]]"
              << std::endl;
    std::cerr << "e.g.: " << argv[0] << " /usr/bin/gcc -c tmp.c" << std::endl;
    exit(1);
  }
  if (argv[1][0] != '/') {
    std::cerr << "argv[1] is not absolute path for local compiler."
              << std::endl;
    exit(1);
  }

  devtools_goma::InstallReadCommandOutputFunc(
      devtools_goma::ReadCommandOutputByPopen);
#else
  if (argc == 1) {
    std::cerr << argv[0] << " [full path of local compiler [args]]"
              << std::endl;
    std::cerr << "e.g.: " << argv[0] << " C:\\vs\\vc\\bin\\cl.exe /c c1.c"
              << std::endl;
    std::cerr << "Compiler path must be absolute path." << std::endl;
    exit(1);
  }

  devtools_goma::InstallReadCommandOutputFunc(
      devtools_goma::ReadCommandOutputByRedirector);
#endif

  devtools_goma::IncludeFileFinder::Init(false);

  const string cwd = devtools_goma::GetCurrentDirNameOrDie();
  std::vector<string> args;
  for (int i = 1; i < argc; i++)
    args.push_back(argv[i]);

  std::unique_ptr<devtools_goma::CompilerFlags> flags(
      devtools_goma::CompilerFlagsParser::MustNew(args, cwd));
  std::vector<string> compiler_info_envs;
  flags->GetClientImportantEnvs(envp, &compiler_info_envs);

  // These env variables are needed to run cl.exe
  GetAdditionalEnv(envp, "PATH", &compiler_info_envs);
  GetAdditionalEnv(envp, "TMP", &compiler_info_envs);
  GetAdditionalEnv(envp, "TEMP", &compiler_info_envs);

  std::unique_ptr<devtools_goma::CompilerInfoData> cid(
      CompilerTypeSpecificCollection()
          .Get(flags->type())
          ->BuildCompilerInfoData(*flags, args[0], compiler_info_envs));

  devtools_goma::CxxCompilerInfo compiler_info(std::move(cid));
  if (compiler_info.HasError()) {
    std::cerr << compiler_info.error_message() << std::endl;
    exit(1);
  }

  std::set<string> include_files;

#if HAVE_CPU_PROFILER
  ProfilerStart(file::JoinPathRespectAbsolute(
                    FLAGS_TMP_DIR, FLAGS_INCLUDE_PROCESSOR_CPU_PROFILE_FILE)
                    .c_str());
#endif

  for (int i = 0; i < loop_count; ++i) {
    devtools_goma::CppIncludeProcessor include_processor;
    devtools_goma::FileStatCache file_stat_cache;
    include_files.clear();

    clock_t start_time = clock();
    for (const auto& iter : flags->input_filenames()) {
      bool ok = include_processor.GetIncludeFiles(
          iter, cwd, *flags, compiler_info, &include_files, &file_stat_cache);
      if (!ok) {
        std::cerr << "GetIncludeFiles failed" << std::endl;
        exit(1);
      }
    }
    clock_t end_time = clock();

    // Show the result only for the first time.
    if (i == 0) {
      for (const auto& iter : include_files) {
        std::cout << iter << std::endl;
      }
      std::cerr << "listed/skipped/total files: " << include_files.size()
                << " / " << include_processor.cpp_parser()->skipped_files()
                << " / " << include_processor.cpp_parser()->total_files()
                << std::endl;
    }

    if (loop_count != 1) {
      std::cerr << "Run " << i << ": ";
    }
    std::cerr << (end_time - start_time) * 1000.0 / CLOCKS_PER_SEC << "msec"
              << std::endl;
  }

#if HAVE_CPU_PROFILER
  ProfilerStop();
#endif

  if (verify_mode) {
    for (const auto& iter : flags->input_filenames()) {
      include_files.insert(file::JoinPathRespectAbsolute(cwd, iter));
    }
    std::set<string> actual = NormalizePaths(cwd, include_files);
    std::set<string> expected = GetExpectedFiles(args, compiler_info_envs, cwd);
    std::cout << "expected" << std::endl;
    for (const auto& iter : expected) {
      std::cout << iter << std::endl;
    }
    std::cout << "compare" << std::endl;
    int missings = CompareFiles(expected, actual);
    if (missings > 0) {
      LOG(ERROR) << "missing files:" << missings;
      exit(1);
    }
  }

  devtools_goma::IncludeCache::Quit();
  devtools_goma::ListDirCache::Quit();
}
