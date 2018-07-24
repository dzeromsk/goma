// Copyright 2010 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// You can specify the clang binary for this test by
//
// GOMATEST_CLANG_PATH=/somewhere/bin/clang ./include_processor_unittest

#include <limits.h>
#include <stddef.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#else
#include <windows.h>
#endif

#include <algorithm>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <glog/logging.h>
#include <glog/stl_logging.h>
#include <gtest/gtest.h>

#include "absl/memory/memory.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "compiler_flags.h"
#include "compiler_flags_parser.h"
#include "compiler_info.h"
#include "compiler_info_builder_facade.h"
#include "compiler_info_cache.h"
#include "compiler_info_state.h"
#include "cpp_include_processor.h"
#include "cxx/cxx_compiler_info.h"
#include "file_helper.h"
#include "file_stat_cache.h"
#include "filesystem.h"
#include "include_cache.h"
#include "include_file_finder.h"
#include "ioutil.h"
#include "list_dir_cache.h"
#include "mypath.h"
#include "path.h"
#include "path_resolver.h"
#include "subprocess.h"
#include "unittest_util.h"
#include "util.h"

using std::string;

namespace {

#ifdef _WIN32
// Wrapper batch file for cl.exe
// "where cl", followed by full paths of cl.exe per line.
// "env", followed by environments for cl.exe.
// "run cl", followed by output of cl.exe command.
static const char* kClWrapperBat =
    "@echo off\r\n"
    "echo where cl\r\n"
    "where cl\r\n"
    "echo env\r\n"
    "set\r\n"
    "echo run cl\r\n"
    "cl %1 %2 %3 %4 %5 %6 %7 %8 %9\r\n";

#endif
}  // namespace

namespace devtools_goma {

class CppIncludeProcessorTest : public testing::Test {
 public:
  CppIncludeProcessorTest() {
#ifndef _WIN32
    char* clang = getenv("GOMATEST_CLANG_PATH");
    clang_path_ = clang ? clang : "/usr/bin/clang";
    if (access(clang_path_.c_str(), X_OK) != 0) {
      PCHECK(!clang) << "The clang you specified doesn't work: " << clang;
      LOG(ERROR) << "We'll skip clang tests.";
      clang_path_ = "";
    }
#else
    // This is out\Release\include_processor_unittest.exe or so.
    const string my_dir = GetMyDirectory();

    top_dir_ = file::JoinPath(my_dir, "..", "..");

    // Read environment.x86 and parse it to env_.
    // environment.x86 contains \0 separated strings.
    const string envfile_path = file::JoinPath(my_dir, "environment.x86");
    string content;
    CHECK(ReadFileToString(envfile_path, &content))
        << "failed to read environment.x86: " << envfile_path;
    env_ = ToVector(absl::StrSplit(content, '\0', absl::SkipEmpty()));
#endif
  }

  void SetUp() override {
    tmpdir_util_ = absl::make_unique<TmpdirUtil>("include_processor_unittest");
    tmpdir_util_->SetCwd("");
#ifndef _WIN32
    InstallReadCommandOutputFunc(ReadCommandOutputByPopen);
#else
    cl_wrapper_path_ = CreateTmpFile(kClWrapperBat, "clwrapper.bat");

    InstallReadCommandOutputFunc(ReadCommandOutputByRedirector);
#endif
    IncludeFileFinder::Init(true);
    ListDirCache::Init(4096);
  }

  void TearDown() override { ListDirCache::Quit(); }

  std::unique_ptr<CompilerInfoData> CreateCompilerInfoWithArgs(
      const CompilerFlags& flags,
      const string& bare_gcc,
      const std::vector<string>& compiler_envs) {
    CompilerInfoBuilderFacade cib;
    return cib.FillFromCompilerOutputs(flags, bare_gcc, compiler_envs);
  }

  ScopedCompilerInfoState GetCompilerInfoFromCacheOrCreate(
      const CompilerFlags& flags,
      const string& bare_gcc,
      const std::vector<string>& compiler_envs) {
    auto key = CompilerInfoCache::CreateKey(flags, bare_gcc, compiler_envs);
    ScopedCompilerInfoState cis(CompilerInfoCache::instance()->Lookup(key));
    if (cis.get() != nullptr) {
      return cis;
    }

    return ScopedCompilerInfoState(CompilerInfoCache::instance()->Store(
        key, CreateCompilerInfoWithArgs(flags, bare_gcc, compiler_envs)));
  };

  std::set<string> RunCppIncludeProcessor(const string& source_file,
                                          const std::vector<string>& args) {
    std::unique_ptr<CompilerFlags> flags(
        CompilerFlagsParser::MustNew(args, tmpdir_util_->tmpdir()));
    std::unique_ptr<CompilerInfoData> data(new CompilerInfoData);
    data->set_found(true);
    data->mutable_cxx();
    CxxCompilerInfo compiler_info(std::move(data));

    CppIncludeProcessor processor;
    std::set<string> files;
    FileStatCache file_stat_cache;
    // ASSERT_TRUE cannot be used here, I don't know why.
    EXPECT_TRUE(processor.GetIncludeFiles(source_file, tmpdir_util_->tmpdir(),
                                          *flags, compiler_info, &files,
                                          &file_stat_cache));
    return files;
  }

  void RunCppIncludeProcessorToEmptySource(const string& compiler,
                                           std::set<string>* files) {
    const string& source_file = CreateTmpFile("", "for_stdcpredef.cc");

    std::vector<string> args;
    args.push_back(compiler);
    args.push_back("-c");
    args.push_back(source_file);

    std::unique_ptr<CompilerFlags> flags(
        CompilerFlagsParser::MustNew(args, tmpdir_util_->tmpdir()));
    ScopedCompilerInfoState cis(
        GetCompilerInfoFromCacheOrCreate(*flags, compiler, env_));

    CppIncludeProcessor processor;
    FileStatCache file_stat_cache;
    ASSERT_TRUE(processor.GetIncludeFiles(
        source_file, tmpdir_util_->tmpdir(), *flags,
        ToCxxCompilerInfo(cis.get()->info()), files, &file_stat_cache));
  }

  void RemoveAndCheckEmptySourceIncludeHeaders(const string& compiler,
                                               std::set<string>* files) {
    std::set<string> emptysource_files;
    RunCppIncludeProcessorToEmptySource(compiler, &emptysource_files);
    for (const auto& it : emptysource_files) {
      EXPECT_GT(files->count(it), 0U);
      files->erase(it);
    }
  }

// Runs test by comparing include_processor with cpp's output.
#ifndef _WIN32
  struct GccLikeCompiler {
    GccLikeCompiler(string path, std::vector<string> additional_args)
        : path(std::move(path)), additional_args(std::move(additional_args)) {}

    string path;
    std::vector<string> additional_args;
  };

  std::vector<GccLikeCompiler> GccLikeCompilers() const {
    std::vector<GccLikeCompiler> compilers;

    std::vector<string> c_args;
    std::vector<string> cpp_args;
    cpp_args.push_back("-x");
    cpp_args.push_back("c++");

    compilers.push_back(GccLikeCompiler("/usr/bin/gcc", c_args));
    compilers.push_back(GccLikeCompiler("/usr/bin/gcc", cpp_args));
#ifndef __MACH__
    // TODO: fix this.
    // On Mac, non system clang seems not know where system libraries exists.
    if (!clang_path_.empty()) {
      compilers.push_back(GccLikeCompiler(clang_path_, c_args));
      compilers.push_back(GccLikeCompiler(clang_path_, cpp_args));
    }
#endif

    return compilers;
  }

  std::set<string> GetExpectedFiles(std::vector<string> args) {
    args.push_back("-M");

    std::vector<string> env(env_);
    env.push_back("LC_ALL=C");

    // The output format of -M will be
    //
    // stdio: /usr/include/stdio.h /usr/include/features.h \\\n
    //   /usr/include/sys/cdefs.h /usr/include/bits/wordsize.h \\\n
    //   ...
    int exit_status;
    string command_output = ReadCommandOutputByPopen(
        args[0], args, env, tmpdir_util_->tmpdir(), STDOUT_ONLY, &exit_status);
    std::vector<string> files = ToVector(absl::StrSplit(
        command_output, absl::ByAnyChar(" \n\r\\"), absl::SkipEmpty()));
    LOG_IF(INFO, exit_status != 0)
        << "non-zero exit status."
        << " args=" << args << " exit_status=" << exit_status;
    std::set<string> expected_files;
    PathResolver pr;
    // Skip the first element as it's the make target.
    for (size_t i = 1; i < files.size(); i++) {
      if (files[i].empty())
        continue;

      // For the include files in the current directory, gcc or clang returns
      // it with relative path. we need to normalize it to absolute path.
      string file =
          file::JoinPathRespectAbsolute(tmpdir_util_->tmpdir(), files[i]);
      // Need normalization as GCC may output a same file in different way.
      // TODO: don't use ResolvePath.
      expected_files.insert(pr.ResolvePath(file));
    }

    return expected_files;
  }

  void RunTest(const string& bare_gcc,
               const string& include_file,
               const std::vector<string>& additional_args) {
    std::vector<string> args;
    args.push_back(bare_gcc);
    copy(additional_args.begin(), additional_args.end(), back_inserter(args));
    args.push_back(include_file);

    std::set<string> expected_files(GetExpectedFiles(args));
    ASSERT_FALSE(expected_files.empty());
    std::unique_ptr<CompilerFlags> flags(
        CompilerFlagsParser::MustNew(args, tmpdir_util_->tmpdir()));

    ScopedCompilerInfoState cis(
        GetCompilerInfoFromCacheOrCreate(*flags, bare_gcc, env_));
    VLOG(1) << cis.get()->info().DebugString();

    CppIncludeProcessor processor;
    std::set<string> files;
    FileStatCache file_stat_cache;
    ASSERT_TRUE(processor.GetIncludeFiles(
        include_file, tmpdir_util_->tmpdir(), *flags,
        ToCxxCompilerInfo(cis.get()->info()), &files, &file_stat_cache));
    // TODO: don't use ResolvePath.
    //  for now, it fails without ResolvePath
    //    recursive: /dir/../dir/foo.c not found, /dir/./foo.c not found
    //    include_twice_with_macro: dir/./tmp.h not found
    PathResolver pr;
    std::set<string> actual_files;
    for (const auto& iter : files) {
      actual_files.insert(pr.ResolvePath(
          file::JoinPathRespectAbsolute(tmpdir_util_->tmpdir(), iter)));
    }
    actual_files.insert(pr.ResolvePath(include_file));

    VLOG(1) << "expected_files: " << expected_files
            << " actual_files: " << actual_files;

    CompareFiles(expected_files, actual_files);
  }
#else
  void RunClTest(const string& include_file,
                 const std::vector<string>& additional_args) {
    std::vector<string> args;
    args.push_back(cl_wrapper_path_);
    args.push_back("/nologo");
    args.push_back("/showIncludes");
    args.push_back("/c");
    args.push_back(include_file);
    copy(additional_args.begin(), additional_args.end(), back_inserter(args));
    LOG(INFO) << args;
    static const char kNoteIncluding[] = "Note: including file: ";

    VLOG(1) << cl_wrapper_path_;
    VLOG(1) << "args:" << args;
    VLOG(1) << "env:" << env_;
    int32_t status;
    string command_output = ReadCommandOutputByRedirector(
        cl_wrapper_path_, args, env_, tmpdir_util_->tmpdir(),
        MERGE_STDOUT_STDERR, &status);
    std::vector<string> lines = ToVector(absl::StrSplit(
        command_output, absl::ByAnyChar("\n\r"), absl::SkipEmpty()));
    if (status != 0) {
      LOG(INFO) << "status: " << status;
      for (size_t i = 0; i < lines.size(); ++i) {
        LOG(INFO) << "line " << i << ":" << lines[i];
      }
      FAIL();
    }
    VLOG(1) << "ReadCommand finished " << lines.size() << " lines.";
    VLOG(2) << lines;

    std::set<string> expected_files;
    PathResolver pr;

    size_t lineno = 0;
    for (; lineno < lines.size(); ++lineno) {
      if (absl::StartsWith(lines[lineno], "where cl")) {
        ++lineno;
        break;
      }
    }

    string bare_cl = lines[lineno];
    LOG(INFO) << "bare_cl=" << bare_cl;
    ++lineno;
    for (; lineno < lines.size(); ++lineno) {
      if (absl::StartsWith(lines[lineno], "env")) {
        ++lineno;
        break;
      }
    }

    std::vector<string> compiler_env;
    for (; lineno < lines.size(); ++lineno) {
      if (absl::StartsWith(lines[lineno], "run cl")) {
        ++lineno;
        break;
      }
      compiler_env.push_back(lines[lineno]);
    }
    VLOG(1) << "compiler_env=" << compiler_env;

    // The output format of /showIncludes will be
    //
    // Note: including file: c:\Program Files (x86)
    //       \Microsoft Visual Studio 9.0\VC\INCLUDE\stdio.h\r\n
    // ...
    //
    // Note: some filenames output by /showIncludes are normalized to lower
    // case charactors. Since it will not cause failure of compile request,
    // let me compare expected result and actual result after converting both
    // to lower case characters.
    for (; lineno < lines.size(); ++lineno) {
      const string& line = lines[lineno];
      if (absl::StartsWith(line, kNoteIncluding)) {
        string path = line.substr(sizeof(kNoteIncluding) - 1);
        size_t pos = path.find_first_not_of(' ');
        if (pos != string::npos) {
          path = path.substr(pos);
          absl::AsciiStrToLower(&path);
          expected_files.insert(pr.ResolvePath(path));
        }
      }
    }
    LOG(INFO) << "# of expected_files=" << expected_files.size();
    VLOG(1) << "expected_files=" << expected_files;
    ASSERT_FALSE(expected_files.empty());
    args[0] = bare_cl;
    std::unique_ptr<CompilerFlags> flags(
        CompilerFlagsParser::MustNew(args, tmpdir_util_->tmpdir()));

    ScopedCompilerInfoState cis(
        GetCompilerInfoFromCacheOrCreate(*flags, bare_cl, env_));

    CppIncludeProcessor processor;
    std::set<string> files;
    FileStatCache file_stat_cache;
    ASSERT_TRUE(processor.GetIncludeFiles(
        include_file, tmpdir_util_->tmpdir(), *flags,
        ToCxxCompilerInfo(cis.get()->info()), &files, &file_stat_cache));
    // TODO: don't use ResolvePath.
    std::set<string> actual_files;
    for (string path : files) {
      absl::AsciiStrToLower(&path);
      actual_files.insert(pr.ResolvePath(path));
    }

    LOG(INFO) << "# of actual_files=" << actual_files.size();
    VLOG(1) << "expected_files: " << expected_files
            << " actual_files: " << actual_files;
    CompareFiles(expected_files, actual_files);
  }
#endif

  static void CompareFiles(const std::set<string>& expected_files,
                           const std::set<string>& actual_files) {
    std::vector<string> matched_files;
    std::vector<string> missing_files;
    std::vector<string> extra_files;

    set_intersection(expected_files.begin(), expected_files.end(),
                     actual_files.begin(), actual_files.end(),
                     back_inserter(matched_files));
    set_difference(expected_files.begin(), expected_files.end(),
                   matched_files.begin(), matched_files.end(),
                   back_inserter(missing_files));
    set_difference(actual_files.begin(), actual_files.end(),
                   matched_files.begin(), matched_files.end(),
                   back_inserter(extra_files));

    LOG(INFO) << "matched:" << matched_files.size()
              << " extra:" << extra_files.size()
              << " missing:" << missing_files.size();
    LOG_IF(INFO, !extra_files.empty())
        << "extra files: " << absl::StrJoin(extra_files, ", ");
    LOG_IF(INFO, !missing_files.empty())
        << "missing files: " << absl::StrJoin(missing_files, ", ");

    EXPECT_EQ(0U, missing_files.size()) << missing_files;
#ifdef __MACH__
    // See: b/26573474
    LOG_IF(WARNING, 0U != extra_files.size()) << extra_files;
#else
    EXPECT_EQ(0U, extra_files.size()) << extra_files;
#endif
  }

  string CreateTmpFile(const string& content, const string& name) {
    tmpdir_util_->CreateTmpFile(name, content);
    return tmpdir_util_->FullPath(name);
  }

#ifndef _WIN32
  string CreateTmpDir(const string& dirname) {
    tmpdir_util_->MkdirForPath(dirname, true);
    return tmpdir_util_->FullPath(dirname);
  }

  string CreateTmpHmapWithOneEntry(const string& key,
                                   const string& prefix,
                                   const string& suffix,
                                   const string& name) {
    struct HeaderMapWithOneEntry {
      char magic[4];
      uint16_t version;
      uint16_t reserved;
      uint32_t string_offset;
      uint32_t string_count;
      uint32_t hash_capacity;
      uint32_t max_value_length;

      uint32_t key;
      uint32_t prefix;
      uint32_t suffix;

      char strings[1];
    };

    size_t hmap_len = sizeof(HeaderMapWithOneEntry);
    hmap_len += key.size() + 1;
    hmap_len += prefix.size() + 1;
    hmap_len += suffix.size() + 1;

    std::unique_ptr<char[], decltype(&free)> hmap_entity(
        reinterpret_cast<char*>(calloc(1, hmap_len)), free);
    HeaderMapWithOneEntry* hmap =
        reinterpret_cast<HeaderMapWithOneEntry*>(hmap_entity.get());
    hmap->magic[0] = 'p';
    hmap->magic[1] = 'a';
    hmap->magic[2] = 'm';
    hmap->magic[3] = 'h';
    hmap->version = 1;
    hmap->string_offset = offsetof(HeaderMapWithOneEntry, strings);
    hmap->hash_capacity = 1;
    hmap->key = 1;
    hmap->prefix = hmap->key + key.size() + 1;
    hmap->suffix = hmap->prefix + prefix.size() + 1;
    strcpy(hmap->strings + hmap->key, key.c_str());
    strcpy(hmap->strings + hmap->prefix, prefix.c_str());
    strcpy(hmap->strings + hmap->suffix, suffix.c_str());

    return CreateTmpFile(string(hmap_entity.get(), hmap_len), name);
  }
#endif

 protected:
  static void SetUpTestCase() {
    // Does not load cache from file.
    CompilerInfoCache::Init("", "", 3600);
    IncludeCache::Init(5, true);
  };

  static void TearDownTestCase() {
    IncludeCache::Quit();
    CompilerInfoCache::Quit();
  };

  std::unique_ptr<TmpdirUtil> tmpdir_util_;
  std::vector<string> env_;
#ifndef _WIN32
  string clang_path_;
#else
  string cl_wrapper_path_;
  string top_dir_;
#endif
};

#ifndef _WIN32
TEST_F(CppIncludeProcessorTest, stdio) {
  std::vector<string> args;
  RunTest("/usr/bin/gcc", CreateTmpFile("#include <stdio.h>", "foo.c"), args);
}

TEST_F(CppIncludeProcessorTest, iostream) {
  std::vector<string> args;
  RunTest("/usr/bin/g++", CreateTmpFile("#include <iostream>", "foo.cc"), args);
}

TEST_F(CppIncludeProcessorTest, iostream_with_gcc) {
  std::vector<string> args;
  RunTest("/usr/bin/gcc", CreateTmpFile("#include <iostream>", "foo.cpp"),
          args);
}

TEST_F(CppIncludeProcessorTest, macro) {
  std::vector<string> args;
  RunTest("/usr/bin/g++",
          CreateTmpFile("#define ios <iostream>\n#include ios\n", "foo.cc"),
          args);
}

TEST_F(CppIncludeProcessorTest, commandline_macro) {
  std::vector<string> args;
  args.push_back("-Dios=<iostream>");
  RunTest("/usr/bin/g++", CreateTmpFile("#include ios\n", "foo.cc"), args);
}

TEST_F(CppIncludeProcessorTest, commandline_macro_undef) {
  std::vector<string> args;
  // Undefnie predefined macro.
  args.push_back("-U__ELF__");
  args.push_back("-D__ELF__=<stdio.h>");
  RunTest("/usr/bin/g++", CreateTmpFile("#include __ELF__\n", "foo.cc"), args);
}

TEST_F(CppIncludeProcessorTest, unclosed_macro) {
  std::vector<string> args;
  RunTest("/usr/bin/g++", CreateTmpFile("#define wrong_macro \"foo", "foo.cc"),
          args);
}

TEST_F(CppIncludeProcessorTest, opt_include_in_system_path) {
  std::vector<string> args;
  args.push_back("-include");
  args.push_back("stdio.h");
  RunTest("/usr/bin/gcc", CreateTmpFile("", "foo.cc"), args);
}

// See b/74321868
TEST_F(CppIncludeProcessorTest, opt_include_evil) {
  const std::vector<string> args{
      "-IA", "-IB", "-include", "foo.h",
  };

  CreateTmpFile(
      "#pragma once\n"
      "#include_next <foo.h>\n",
      file::JoinPath("A", "foo.h"));
  CreateTmpFile("", file::JoinPath("B", "foo.h"));

  RunTest("/usr/bin/g++", CreateTmpFile("", "foo.cc"), args);
}

TEST_F(CppIncludeProcessorTest, include_twice) {
  const std::vector<string> args{
      "-include", "foo.h", "-include", "foo.h",
  };

  CreateTmpFile(
      "#ifndef FOO_H_\n"
      "#define FOO_H_\n"
      "#else\n"
      "// The second include\n"
      "#include \"bar.h\"\n"
      "#endif\n",
      "foo.h");
  CreateTmpFile("", "bar.h");

  RunTest("/usr/bin/g++", CreateTmpFile("", "foo.cc"), args);
}

TEST_F(CppIncludeProcessorTest, stdcpredef) {
  const string& bare_gcc = "/usr/bin/g++";
  const string& source_file = CreateTmpFile("", "foo.cc");
  CreateTmpFile("", "stdc-predef.h");

  std::vector<string> args;
  args.push_back(bare_gcc);
  args.push_back("-I.");
  args.push_back("-c");
  args.push_back(source_file);

  std::unique_ptr<CompilerFlags> flags(
      CompilerFlagsParser::MustNew(args, tmpdir_util_->tmpdir()));
  std::unique_ptr<CompilerInfoData> data(
      CreateCompilerInfoWithArgs(*flags, bare_gcc, env_));

  data->set_name("g++");
  data->set_version("g++ (Ubuntu 4.8.2-19ubuntu1) 4.8.2");
  data->mutable_cxx()->set_predefined_macros(
      "#define __GNUC__ 4\n"
      "#define __GNUC_MINOR__ 8\n");

  CxxCompilerInfo compiler_info(std::move(data));

  CppIncludeProcessor processor;
  std::set<string> files;
  FileStatCache file_stat_cache;
  ASSERT_TRUE(processor.GetIncludeFiles(source_file, tmpdir_util_->tmpdir(),
                                        *flags, compiler_info, &files,
                                        &file_stat_cache));

  // stdc-predef.h should be included.
  EXPECT_EQ(1U, files.size());
}

TEST_F(CppIncludeProcessorTest, ffreestanding) {
  const string& bare_gcc = "/usr/bin/g++";
  const string& source_file = CreateTmpFile("", "foo.cc");

  std::vector<string> args;
  args.push_back(bare_gcc);
  args.push_back("-ffreestanding");
  args.push_back("-c");
  args.push_back(source_file);

  std::unique_ptr<CompilerFlags> flags(
      CompilerFlagsParser::MustNew(args, tmpdir_util_->tmpdir()));
  CxxCompilerInfo compiler_info(
      CreateCompilerInfoWithArgs(*flags, bare_gcc, env_));

  CppIncludeProcessor processor;
  std::set<string> files;
  FileStatCache file_stat_cache;
  ASSERT_TRUE(processor.GetIncludeFiles(source_file, tmpdir_util_->tmpdir(),
                                        *flags, compiler_info, &files,
                                        &file_stat_cache));

  // stdc-predef.h should not be included.
  EXPECT_EQ(0U, files.size());
}

#ifndef __MACH__
// Mac's /usr/bin/gcc is actually clang, and it does not know '-fno-hosted'.
// So, skip this test on Mac.
TEST_F(CppIncludeProcessorTest, fnohosted) {
  // -fno-hosted is not effective for C++.
  // So, test with gcc (not g++).
  //
  // $ g++ -fno-hosted -c ./test.cc
  // cc1plus: warning: command line option '-fno-hosted' is valid for
  // C/ObjC but not for C++ [enabled by default]

  const string& bare_gcc = "/usr/bin/gcc";
  const string& source_file = CreateTmpFile("", "foo.c");

  std::vector<string> args;
  args.push_back(bare_gcc);
  args.push_back("-fno-hosted");
  args.push_back("-c");
  args.push_back(source_file);

  std::unique_ptr<CompilerFlags> flags(
      CompilerFlagsParser::MustNew(args, tmpdir_util_->tmpdir()));
  CxxCompilerInfo compiler_info(
      CreateCompilerInfoWithArgs(*flags, bare_gcc, env_));
  ASSERT_FALSE(compiler_info.HasError());

  CppIncludeProcessor processor;
  std::set<string> files;
  FileStatCache file_stat_cache;
  ASSERT_TRUE(processor.GetIncludeFiles(source_file, tmpdir_util_->tmpdir(),
                                        *flags, compiler_info, &files,
                                        &file_stat_cache));

  // stdc-predef.h should not be included.
  EXPECT_EQ(0U, files.size());
}
#endif  // !__MACH__

// TODO: Move some tests out from ifndef _WIN32 to share test cases.

TEST_F(CppIncludeProcessorTest, recursive) {
  absl::string_view tmp_dir_basename = file::Basename(tmpdir_util_->tmpdir());
  CHECK(!tmp_dir_basename.empty());

  // If we don't normalize .. and ., this will take exponential time.
  std::ostringstream source;
  source << "#ifndef FOO_C_\n"
         << "#define FOO_C_\n"
         << "#include \"../" << tmp_dir_basename << "/foo.c\"\n"
         << "#include \"./foo.c\"\n"
         << "#endif\n";

  std::vector<string> args;
  RunTest("/usr/bin/gcc", CreateTmpFile(source.str(), "foo.c"), args);
}

TEST_F(CppIncludeProcessorTest, opt_include_gch) {
  const string& bare_gcc = "/usr/bin/g++";

  std::vector<string> args;
  args.push_back(bare_gcc);
  const string& orig_header = CreateTmpFile(
      "#include <stdio.h> // This file must not be parsed", "foo.h");
  const string& gch_header = CreateTmpFile(
      "#include <stdio.h> // This file must not be parsed", "foo.h.gch.goma");

  const string& source_file = CreateTmpFile("", "foo.cc");
  args.push_back("-c");
  args.push_back(source_file);
  args.push_back("-include");
  args.push_back(orig_header);

  std::unique_ptr<CompilerFlags> flags(
      CompilerFlagsParser::MustNew(args, tmpdir_util_->tmpdir()));
  CxxCompilerInfo compiler_info(
      CreateCompilerInfoWithArgs(*flags, bare_gcc, env_));

  CppIncludeProcessor processor;
  std::set<string> files;
  FileStatCache file_stat_cache;
  ASSERT_TRUE(processor.GetIncludeFiles(source_file, tmpdir_util_->tmpdir(),
                                        *flags, compiler_info, &files,
                                        &file_stat_cache));

  RemoveAndCheckEmptySourceIncludeHeaders(bare_gcc, &files);
  ASSERT_EQ(1, static_cast<int>(files.size()));
  EXPECT_EQ(gch_header, *files.begin());
}

TEST_F(CppIncludeProcessorTest, gch) {
  const string& bare_gcc = "/usr/bin/g++";

  std::vector<string> args;
  args.push_back(bare_gcc);
  // We have foo.h, foo.h.gch.goma, a/foo.h, and a/foo.h.gch.goma in this test.
  CreateTmpDir("a");
  const string& content = "#include <stdio.h> // This file must not be parsed";
  // The order of creating of these files are important to ensure the
  // converage as readdir tends to return new files later.
  // We want to check both of the following cases:
  //
  // 1. Normal header is found first, then pre-compiled one is found.
  // 2. Pre-compiled header is found first, then normal one is found.
  CreateTmpFile(content, "foo.h");
  CreateTmpFile(content, "foo.h.gch.goma");
  CreateTmpFile(content, "a/foo.h.gch.goma");
  CreateTmpFile(content, "a/foo.h");

  // Including "foo.h" should fetch foo.h.gch.goma.
  CreateTmpFile("#include <stdio.h> // This file must not be parsed",
                "foo.h.gch.goma");

  string source_file = CreateTmpFile("#include \"foo.h\"", "foo.cc");

  args.push_back("-c");
  args.push_back(source_file);

  std::unique_ptr<CompilerFlags> flags(
      CompilerFlagsParser::MustNew(args, tmpdir_util_->tmpdir()));
  CxxCompilerInfo compiler_info(
      CreateCompilerInfoWithArgs(*flags, bare_gcc, env_));

  std::unique_ptr<CppIncludeProcessor> processor(new CppIncludeProcessor());

  std::set<string> files;
  FileStatCache file_stat_cache;
  ASSERT_TRUE(processor->GetIncludeFiles(source_file, tmpdir_util_->tmpdir(),
                                         *flags, compiler_info, &files,
                                         &file_stat_cache));

  RemoveAndCheckEmptySourceIncludeHeaders(bare_gcc, &files);
  ASSERT_EQ(1, static_cast<int>(files.size()));
  EXPECT_EQ(tmpdir_util_->FullPath("foo.h.gch.goma"), *files.begin());

  // Get foo.h.gch.goma by including <foo.h> with -I. option.
  source_file = CreateTmpFile("#include <foo.h>", "foo.cc");

  args.clear();
  args.push_back(bare_gcc);
  args.push_back("-I.");
  args.push_back("-c");
  args.push_back(source_file);

  flags = CompilerFlagsParser::MustNew(args, tmpdir_util_->tmpdir());
  processor = absl::make_unique<CppIncludeProcessor>();

  files.clear();
  file_stat_cache.Clear();
  ASSERT_TRUE(processor->GetIncludeFiles(source_file, tmpdir_util_->tmpdir(),
                                         *flags, compiler_info, &files,
                                         &file_stat_cache));

  RemoveAndCheckEmptySourceIncludeHeaders(bare_gcc, &files);
  ASSERT_EQ(1, static_cast<int>(files.size()));
  EXPECT_EQ("./foo.h.gch.goma", *files.begin());

  // We should get a/foo.h.gch.goma by including <a/foo.h> with -I. option.
  source_file = CreateTmpFile("#include <a/foo.h>", "foo.cc");

  args.clear();
  args.push_back(bare_gcc);
  args.push_back("-I.");
  args.push_back("-c");
  args.push_back(source_file);
  flags = CompilerFlagsParser::MustNew(args, tmpdir_util_->tmpdir());
  processor = absl::make_unique<CppIncludeProcessor>();

  files.clear();
  file_stat_cache.Clear();
  ASSERT_TRUE(processor->GetIncludeFiles(source_file, tmpdir_util_->tmpdir(),
                                         *flags, compiler_info, &files,
                                         &file_stat_cache));

  RemoveAndCheckEmptySourceIncludeHeaders(bare_gcc, &files);
  ASSERT_EQ(1, static_cast<int>(files.size()));
  EXPECT_EQ("./a/foo.h.gch.goma", *files.begin());

  // We should get a/foo.h.gch.goma by including <foo.h> with -Ia -I. option.
  source_file = CreateTmpFile("#include <foo.h>", "foo.cc");

  args.clear();
  args.push_back(bare_gcc);
  args.push_back("-Ia");
  args.push_back("-I.");
  args.push_back("-c");
  args.push_back(source_file);
  flags = CompilerFlagsParser::MustNew(args, tmpdir_util_->tmpdir());
  processor = absl::make_unique<CppIncludeProcessor>();

  files.clear();
  file_stat_cache.Clear();
  ASSERT_TRUE(processor->GetIncludeFiles(source_file, tmpdir_util_->tmpdir(),
                                         *flags, compiler_info, &files,
                                         &file_stat_cache));

  RemoveAndCheckEmptySourceIncludeHeaders(bare_gcc, &files);
  ASSERT_EQ(1, static_cast<int>(files.size()));
  EXPECT_EQ("a/foo.h.gch.goma", *files.begin());
  // We should get foo.h.gch.goma by including <foo.h> with -I. -Ia option.
  source_file = CreateTmpFile("#include <foo.h>", "foo.cc");

  args.clear();
  args.push_back(bare_gcc);
  args.push_back("-I.");
  args.push_back("-Ia");
  args.push_back("-c");
  args.push_back(source_file);
  flags = CompilerFlagsParser::MustNew(args, tmpdir_util_->tmpdir());
  processor = absl::make_unique<CppIncludeProcessor>();

  files.clear();
  file_stat_cache.Clear();
  ASSERT_TRUE(processor->GetIncludeFiles(source_file, tmpdir_util_->tmpdir(),
                                         *flags, compiler_info, &files,
                                         &file_stat_cache));

  RemoveAndCheckEmptySourceIncludeHeaders(bare_gcc, &files);
  ASSERT_EQ(1, static_cast<int>(files.size()));
  EXPECT_EQ("./foo.h.gch.goma", *files.begin());

  // A crazy case: when foo.h.gch.goma is explicitly included, we should
  // examine its content.
  source_file = CreateTmpFile("#include <foo.h.gch.goma>", "foo.cc");
  CreateTmpFile("#include <a/foo.h>", "foo.h.gch.goma");

  args.clear();
  args.push_back(bare_gcc);
  args.push_back("-I.");
  args.push_back("-c");
  args.push_back(source_file);
  flags = CompilerFlagsParser::MustNew(args, tmpdir_util_->tmpdir());
  processor = absl::make_unique<CppIncludeProcessor>();

  files.clear();
  file_stat_cache.Clear();
  ASSERT_TRUE(processor->GetIncludeFiles(source_file, tmpdir_util_->tmpdir(),
                                         *flags, compiler_info, &files,
                                         &file_stat_cache));

  RemoveAndCheckEmptySourceIncludeHeaders(bare_gcc, &files);
  ASSERT_EQ(2, static_cast<int>(files.size()));
  std::set<string>::const_iterator iter = files.begin();
  EXPECT_EQ("./a/foo.h.gch.goma", *iter);
  ++iter;
  EXPECT_EQ("./foo.h.gch.goma", *iter);
}

TEST_F(CppIncludeProcessorTest, dir_cache) {
  std::vector<string> args;
  args.push_back("-I" + tmpdir_util_->tmpdir());

  CreateTmpFile("", "bar.h");
  // The cache will be constructed here.
  RunTest("/usr/bin/g++", CreateTmpFile("#include <bar.h>\n", "foo.cc"), args);

  // As another file is added, the cache must be discarded.
  CreateTmpFile("", "baz.h");
  RunTest("/usr/bin/g++", CreateTmpFile("#include <baz.h>\n", "foo.cc"), args);
}

TEST_F(CppIncludeProcessorTest, I_system_path) {
  std::vector<string> args;
  // Though /usr/include is specified before tmpdir_util_->tmpdir(),
  // we don't use this because system path has this path.
  args.emplace_back("-I/usr/include");
  args.emplace_back("-I//////usr///include///");
  args.emplace_back("-I" + tmpdir_util_->tmpdir());

  CreateTmpFile("", "stdio.h");
  // The cache will be constructed here.
  RunTest("/usr/bin/g++", CreateTmpFile("#include <stdio.h>\n", "foo.cc"),
          args);

  // As another file is added, the cache must be discarded.
  CreateTmpFile("", "baz.h");
  RunTest("/usr/bin/g++", CreateTmpFile("#include <stdio.h>\n", "foo.cc"),
          args);
}

TEST_F(CppIncludeProcessorTest, iquote) {
  std::vector<string> args{
      "-iquote", "include",
  };
  CreateTmpFile("", "include/foo.h");
  RunTest("/usr/bin/g++", CreateTmpFile("#include \"foo.h\"\n", "foo.cc"),
          args);
}

TEST_F(CppIncludeProcessorTest, hmap) {
  const string& bare_gcc = "/usr/bin/g++";

  std::vector<string> args;
  args.push_back(bare_gcc);
  const string& include_foo = CreateTmpFile("#include <foo.h>", "foo.cc");
  const string& bar_header = CreateTmpFile("", "bar.h");
  const string& hmap_file = "hmap.hmap";
  CreateTmpHmapWithOneEntry("foo.h", "", bar_header, hmap_file);

  args.push_back("-Ihmap.hmap");
  args.push_back(include_foo);

  std::unique_ptr<CompilerFlags> flags(
      CompilerFlagsParser::MustNew(args, tmpdir_util_->tmpdir()));
  CxxCompilerInfo compiler_info(
      CreateCompilerInfoWithArgs(*flags, bare_gcc, env_));

  std::unique_ptr<CppIncludeProcessor> processor(new CppIncludeProcessor());
  std::set<string> files;
  FileStatCache file_stat_cache;
  ASSERT_TRUE(processor->GetIncludeFiles(include_foo, tmpdir_util_->tmpdir(),
                                         *flags, compiler_info, &files,
                                         &file_stat_cache));

  RemoveAndCheckEmptySourceIncludeHeaders(bare_gcc, &files);
  EXPECT_EQ(2, static_cast<int>(files.size()));
  EXPECT_EQ(1, static_cast<int>(files.count(hmap_file)));
  EXPECT_EQ(1, static_cast<int>(files.count(bar_header)));

  const string& baz_header = CreateTmpFile("", "baz.h");
  // Now we should fetch baz.h for #include <foo.h>.
  CreateTmpHmapWithOneEntry("foo.h", "", baz_header, hmap_file);
  flags = CompilerFlagsParser::MustNew(args, tmpdir_util_->tmpdir());
  processor = absl::make_unique<CppIncludeProcessor>();
  files.clear();
  file_stat_cache.Clear();
  ASSERT_TRUE(processor->GetIncludeFiles(include_foo, tmpdir_util_->tmpdir(),
                                         *flags, compiler_info, &files,
                                         &file_stat_cache));

  RemoveAndCheckEmptySourceIncludeHeaders(bare_gcc, &files);
  EXPECT_EQ(2, static_cast<int>(files.size()));
  EXPECT_EQ(1, static_cast<int>(files.count(hmap_file)));
  EXPECT_EQ(1, static_cast<int>(files.count(baz_header)));
}

TEST_F(CppIncludeProcessorTest, hmap_with_dir) {
  const string& bare_gcc = "/usr/bin/g++";

  std::vector<string> args;
  args.push_back(bare_gcc);
  const string& include_foo = CreateTmpFile(
      "#include <dir1/foo.h>\n"
      "#include <dir1/dir2/bar.h>\n",
      "foo.cc");
  const string& foo_header = CreateTmpFile("", "foo.h");
  CreateTmpFile("", "bar.h");
  CreateTmpHmapWithOneEntry("dir1/foo.h", "", foo_header, "foo.hmap");
  CreateTmpHmapWithOneEntry("dir1/dir2/bar.h", "", "bar.h", "bar.hmap");

  args.push_back("-Ifoo.hmap");
  args.push_back("-Ibar.hmap");
  args.push_back(include_foo);

  std::unique_ptr<CompilerFlags> flags(
      CompilerFlagsParser::MustNew(args, tmpdir_util_->tmpdir()));
  CxxCompilerInfo compiler_info(
      CreateCompilerInfoWithArgs(*flags, bare_gcc, env_));

  std::unique_ptr<CppIncludeProcessor> processor(new CppIncludeProcessor());
  std::set<string> files;
  FileStatCache file_stat_cache;
  ASSERT_TRUE(processor->GetIncludeFiles(include_foo, tmpdir_util_->tmpdir(),
                                         *flags, compiler_info, &files,
                                         &file_stat_cache));

  RemoveAndCheckEmptySourceIncludeHeaders(bare_gcc, &files);
  EXPECT_EQ(4, static_cast<int>(files.size()));
  EXPECT_EQ(1, static_cast<int>(files.count("foo.hmap")));
  EXPECT_EQ(1, static_cast<int>(files.count(foo_header)));
  EXPECT_EQ(1, static_cast<int>(files.count("bar.hmap")));
  EXPECT_EQ(1, static_cast<int>(files.count("bar.h")));
}

TEST_F(CppIncludeProcessorTest, cpp_and_isystem) {
  std::vector<string> args;
  CreateTmpFile("", "typeinfo");
  args.push_back("-isystem");
  args.push_back(tmpdir_util_->tmpdir());
  RunTest("/usr/bin/g++", CreateTmpFile("#include <typeinfo>\n", "foo.cc"),
          args);
}

TEST_F(CppIncludeProcessorTest, funclike_macro1) {
  std::vector<string> args;
  RunTest("/usr/bin/g++",
          CreateTmpFile("#define s(x) #x\n"
                        "#include s(stdio.h)\n",
                        "foo.cc"),
          args);
}

TEST_F(CppIncludeProcessorTest, funclike_macro2) {
  std::vector<string> args;
  RunTest("/usr/bin/gcc",
          CreateTmpFile("#define X(name) <std##name.h>\n"
                        "#include X(io)\n",
                        "foo.c"),
          args);
}

TEST_F(CppIncludeProcessorTest, funclike_macro3) {
  std::vector<string> args;
  RunTest("/usr/bin/gcc",
          CreateTmpFile("#define XY \"stdio.h\"\n"
                        "#define C(x, y) x ## y\n"
                        "#include C(X, Y)\n",
                        "foo.c"),
          args);
}

TEST_F(CppIncludeProcessorTest, include_nested_macros) {
  std::vector<string> args;
  CreateTmpFile("#include <stdio.h>\n", "foo1.h");
  RunTest("/usr/bin/gcc",
          CreateTmpFile("#define S(a) #a\n"
                        "#define _X(x) S(foo##x.h)\n"
                        "#define X(x) _X(x)\n"
                        "#include X(__STDC__)\n",
                        "foo.c"),
          args);
}
TEST_F(CppIncludeProcessorTest, commandline_funclike_macro) {
  std::vector<string> args;
  args.push_back("-DS(a)=#a");
  RunTest("/usr/bin/g++", CreateTmpFile("#include S(iostream)\n", "foo.cc"),
          args);
}

TEST_F(CppIncludeProcessorTest, escaped_newline) {
  std::vector<string> args;
  RunTest("/usr/bin/g++",
          CreateTmpFile("#include <io\\\nstream>\n"
                        "#inc\\\nlude <string>\n",
                        "foo.cc"),
          args);
}

TEST_F(CppIncludeProcessorTest, macro_false_recursion) {
  CreateTmpFile("#include <string>\n", "99");
  CreateTmpFile("#include <vector>\n", "X(99)");
  std::vector<string> args;
  RunTest("/usr/bin/g++",
          CreateTmpFile("#define X(x) x\n"
                        "#define Y99(x) x(99)\n"
                        "#define _S(x) #x\n"
                        "#define S(x) _S(x)\n"
                        "#include S(Y99(X))\n"
                        "#include S(Y99(X(X)))\n",
                        "foo.cc"),
          args);
}

TEST_F(CppIncludeProcessorTest, macro_nested_args) {
  CreateTmpFile(
      "#define _S(x) #x\n"
      "#define S(x) _S(x)\n"
      "#define _C(x, y) x ## y\n"
      "#define C(x, y) _C(x, y)\n",
      "util.h");
  CreateTmpFile("#include <vector>\n", "2.h");
  std::vector<string> args;
  RunTest("/usr/bin/g++",
          CreateTmpFile("#include \"util.h\"\n"
                        "#define E1(a, b) a\n"
                        "#define E2(a, b) b\n"
                        "#include S(C(E2(1, 2), E1(.h, .c)))\n",
                        "foo.cc"),
          args);
}

TEST_F(CppIncludeProcessorTest, macro_varargs) {
  CreateTmpFile("#include <vector>\n", "c");
  std::vector<string> args;
  RunTest("/usr/bin/g++",
          CreateTmpFile("#define X(a, b, c, ...) c\n"
                        "#include X(\"a\", \"b\", \"c\", \"d\", \"e\")\n",
                        "foo.cc"),
          args);
}

TEST_F(CppIncludeProcessorTest, macro_with_defined) {
  CreateTmpFile("#include <map>\n", "x.h");
  CreateTmpFile("#include <set>\n", "y.h");
  std::vector<string> args;
  RunTest("/usr/bin/g++",
          CreateTmpFile("#define USE(a) (defined(USE_ ## a) && USE_ ## a)\n"
                        "#define USE_X 1\n"
                        "#define USE_Y !USE_X\n"
                        "#if USE(X)\n"
                        " #include \"x.h\"\n"
                        "#endif\n"
                        "#if USE(Y)\n"
                        "# include \"y.h\"\n"
                        "#endif\n",
                        "foo.cc"),
          args);
}

TEST_F(CppIncludeProcessorTest, include_in_comment) {
  std::vector<string> args;
  RunTest("/usr/bin/g++",
          CreateTmpFile("#include <string> /* \n"
                        "#include <iostream> */\n",
                        "foo.cc"),
          args);
}

TEST_F(CppIncludeProcessorTest, include_in_linecomment) {
  std::vector<string> args;
  RunTest("/usr/bin/g++",
          CreateTmpFile("#include <string> // comment \\\n"
                        "#include <iostream>\n",
                        "foo.cc"),
          args);
}

TEST_F(CppIncludeProcessorTest, include_with_predefined) {
  const string bare_gcc = "/usr/bin/gcc";
  std::unique_ptr<CompilerFlags> flags(CompilerFlagsParser::MustNew(
      std::vector<string>{bare_gcc, "-c", "foo.c"}, tmpdir_util_->tmpdir()));
  ScopedCompilerInfoState cis(
      GetCompilerInfoFromCacheOrCreate(*flags, bare_gcc, env_));

  const char kDefineGnuc[] = "#define __GNUC__ ";

  // Taking __GNUC__ value from predefined macros.
  const string& macros =
      ToCxxCompilerInfo(cis.get()->info()).predefined_macros();
  auto begin_pos = macros.find(kDefineGnuc);
  ASSERT_NE(begin_pos, string::npos);
  ASSERT_LT(begin_pos + strlen(kDefineGnuc), macros.length());
  begin_pos += strlen(kDefineGnuc);  // skip #define __GNUC__

  // __GNUC__ is 4, 5, 6, ... (it depends on gcc version)
  auto end_pos = macros.find('\n', begin_pos);
  ASSERT_NE(end_pos, string::npos);

  const string gnuc = macros.substr(begin_pos, end_pos - begin_pos);
  CreateTmpFile("#include <stdio.h>\n", "foo" + gnuc + ".h");
  RunTest(bare_gcc,
          CreateTmpFile("#define S(x) #x\n"
                        "#define _X(x) S(foo##x.h)\n"
                        "#define X(x) _X(x)\n"
                        "#include X(__GNUC__)\n",
                        "foo.c"),
          std::vector<string>());
}

TEST_F(CppIncludeProcessorTest, include_with_cpp_predefined) {
  CreateTmpFile("#include <stdio.h>\n", "foo4.h");
  std::vector<string> args;
  RunTest("/usr/bin/g++",
          CreateTmpFile("#define S(x) #x\n"
                        "#define _X(x) S(foo##x.h)\n"
                        "#define X(x) _X(x)\n"
                        "#include X(__LINE__)\n",
                        "foo.cc"),
          args);
}

TEST_F(CppIncludeProcessorTest, include_with_pragma_once) {
  CreateTmpFile(
      "#pragma once\n"
      "#ifdef ONCE\n"
      "#include <stdio.h>\n"
      "#endif\n"
      "#ifndef ONCE\n"
      "#define ONCE\n"
      "#endif\n",
      "once.h");
  std::vector<string> args;
  RunTest("/usr/bin/gcc",
          CreateTmpFile("#include \"once.h\"\n"
                        "#include \"once.h\"\n",
                        "foo.c"),
          args);
}

TEST_F(CppIncludeProcessorTest, include_with_ifdefs) {
  CreateTmpFile("#include <string>\n", "foo.h");
  CreateTmpFile("#include <vector>\n", "dummy1.h");
  CreateTmpFile("#include <set>\n", "dummy2.h");
  CreateTmpFile("#include <map>\n", "dummy3.h");
  std::vector<string> args;
  RunTest("/usr/bin/g++",
          CreateTmpFile("#define T 1\n"
                        "#ifndef T\n"
                        "#include \"dummy1.h\"\n"
                        "#elif !__STDC__\n"
                        "#include \"dummy2.h\"\n"
                        "elif defined(__DATE__)\n"
                        "#include \"foo.h\"\n"
                        "#else\n"
                        "#include \"dummy3.h\"\n"
                        "#endif\n",
                        "foo.cc"),
          args);
}

TEST_F(CppIncludeProcessorTest, include_with_if_elif_else) {
  std::vector<string> args;
  RunTest("/usr/bin/g++",
          CreateTmpFile("#define A 1\n"
                        "#define B 0\n"
                        "#if A\n"
                        "# define A_DEFINED 1\n"
                        "#elif B\n"
                        "# define B_DEFINED 1\n"
                        "#else\n"
                        "# define A_DEFINED 0\n"
                        "# define B_DEFINED 0\n"
                        "#endif\n"
                        "\n"
                        "#if A_DEFINED\n"
                        "# include <vector>\n"
                        "#endif\n",
                        "foo.cc"),
          args);
}

TEST_F(CppIncludeProcessorTest, include_with_cond_expr_1) {
  CreateTmpFile(
      "#define A(a, b) a + b\n"
      "#define B(x) 4\n"
      "#define C(x) -(x)\n",
      "util.h");
  CreateTmpFile("#include <string>\n", "foo.h");
  std::vector<string> args;
  RunTest("/usr/bin/g++",
          CreateTmpFile("#include \"util.h\"\n"
                        "#if A(1, 2) * B() == 9\n"
                        "#include \"foo.h\"\n"
                        "#endif\n",
                        "foo.cc"),
          args);
  RunTest("/usr/bin/g++",
          CreateTmpFile("#include \"util.h\"\n"
                        "#if C(A(1, 2)) * B() == -12\n"
                        "#include \"foo.h\"\n"
                        "#endif\n",
                        "foo.cc"),
          args);
  RunTest("/usr/bin/g++",
          CreateTmpFile("#include \"util.h\"\n"
                        "#if A(1, 2) < 4\n"
                        "#include \"foo.h\"\n"
                        "#endif\n",
                        "foo.cc"),
          args);
  RunTest("/usr/bin/g++",
          CreateTmpFile("#include \"util.h\"\n"
                        "#if 0\n"
                        "#if A(1, 2) < 4\n"
                        "#include \"dummy.h\"\n"
                        "#endif\n"
                        "#endif\n",
                        "foo.cc"),
          args);
  RunTest("/usr/bin/g++",
          CreateTmpFile("#include \"util.h\"\n"
                        "#if defined(A) || defined AB\n"
                        "#include \"foo.h\"\n"
                        "#endif\n",
                        "foo.cc"),
          args);
  RunTest("/usr/bin/g++",
          CreateTmpFile("#include \"util.h\"\n"
                        "#if defined(A) && defined(AB)\n"
                        "#include \"dummy.h\"\n"
                        "#endif\n",
                        "foo.cc"),
          args);
}

TEST_F(CppIncludeProcessorTest, include_nested) {
  std::vector<string> args;
  CreateTmpFile(
      "#ifdef A\n"
      "# include <stdio.h>\n"
      "#else\n"
      "# define A\n"
      "# include \"foo.h\"\n"
      "#endif\n",
      "foo.h");
  RunTest("/usr/bin/gcc", CreateTmpFile("#include \"foo.h\"\n", "foo.c"), args);
}

TEST_F(CppIncludeProcessorTest, include_with_macro) {
  const string& bare_gcc = "/usr/bin/g++";

  const string& source_file = CreateTmpFile(
      "#define INCLUDE <a.h>\n"
      "#include INCLUDE\n",
      "a.cc");

  CreateTmpFile("#define FOO 100\n", "a.h");
  CreateTmpFile("#define FOO 200\n", file::JoinPath("a", "a.h"));

  std::vector<string> args;
  args.push_back(bare_gcc);
  args.push_back("-Ia");
  args.push_back("-c");
  args.push_back(source_file);

  std::set<string> expected;
  expected.insert(file::JoinPath("a", "a.h"));

  std::set<string> files = RunCppIncludeProcessor(source_file, args);
  ASSERT_EQ(expected.size(), files.size());
  EXPECT_EQ(expected, files);
}

TEST_F(CppIncludeProcessorTest, include_twice_with_macro) {
  std::vector<string> args;
  CreateTmpFile("#include A\n", "foo.h");
  CreateTmpFile("#include <string>\n", "tmp.h");
  RunTest("/usr/bin/g++",
          CreateTmpFile("#define A <vector>\n"
                        "#include \"foo.h\"\n"
                        "#undef A\n"
                        "#define A \"./tmp.h\"\n"
                        "#include \"foo.h\"\n",
                        "foo.cc"),
          args);
}

TEST_F(CppIncludeProcessorTest, include_time_h) {
  std::vector<string> args;
  RunTest("/usr/bin/gcc",
          CreateTmpFile("#include <sys/types.h>\n"
                        "#include <time.h>\n",
                        "time.c"),
          args);
}

TEST_F(CppIncludeProcessorTest, base_file) {
  std::vector<string> args;
  RunTest("/usr/bin/gcc",
          CreateTmpFile("#ifdef X\n"
                        "# include <stdio.h>\n"
                        "#else\n"
                        "# define X\n"
                        "# include __BASE_FILE__\n"
                        "#endif",
                        "foo.c"),
          args);
}

TEST_F(CppIncludeProcessorTest, has_include) {
  const string define_has_include =
      "#ifndef __has_include\n"
      "# define __has_include(x) 0\n"
      "#endif\n";

  for (const auto& compiler : GccLikeCompilers()) {
    std::vector<string> args(compiler.additional_args);

    // Check __has_include is defined.
    RunTest(compiler.path,
            CreateTmpFile("#ifdef __has_include\n"
                          "# include <stddef.h>\n"
                          "#endif",
                          "foo.c"),
            args);

    // Check __has_include__ is hidden. (for GCC 5 hack)
    RunTest(compiler.path,
            CreateTmpFile("#ifdef __has_include__\n"
                          "# include <stdint.h>\n"
                          "#endif\n"
                          "#if defined(__has_include) && !defined(__clang__)\n"
                          "# if __has_include__(<stddef.h>)\n"
                          "#  include <stddef.h>\n"
                          "# endif\n"
                          "#endif\n",
                          "foo.c"),
            args);

    // '<' include check.
    RunTest(compiler.path,
            CreateTmpFile(define_has_include + "#if __has_include(<stddef.h>)\n"
                                               "# include <stddef.h>\n"
                                               "#endif\n",
                          "foo.c"),
            args);

    // '<' include check with expansion.
    RunTest(compiler.path,
            CreateTmpFile(define_has_include + "#define X(name) <std##name.h>\n"
                                               "#if __has_include(X(int))\n"
                                               "# include X(int)\n"
                                               "#endif\n",
                          "foo.c"),
            args);

    // Nonexistent '<' include check.
    RunTest(compiler.path,
            CreateTmpFile(define_has_include + "#if __has_include(<foo.h>)\n"
                                               "# include <foo.h>\n"
                                               "#else\n"
                                               "# include <string.h>\n"
                                               "#endif\n",
                          "foo.c"),
            args);

    // '<' include check with whitespaces.
    CreateTmpFile("", "white  space.h");
    RunTest(compiler.path,
            CreateTmpFile(define_has_include +
                              "#if __has_include(<white  space.h>)\n"
                              "# include <white  space.h>\n"
                              "#endif\n",
                          "foo.c"),
            args);

    // Nonexistent '"' include check.
    RunTest(compiler.path,
            CreateTmpFile(define_has_include + "#if __has_include(\"bar.h\")\n"
                                               "# include \"bar.h\"\n"
                                               "#else\n"
                                               "# include <string.h>\n"
                                               "#endif\n",
                          "bar.c"),
            args);

    // '"' include check.
    CreateTmpFile("#include <stdio.h>\n", "baz.h");
    RunTest(compiler.path,
            CreateTmpFile(define_has_include + "#if __has_include(\"baz.h\")\n"
                                               "# include \"baz.h\"\n"
                                               "#else\n"
                                               "# include <string.h>\n"
                                               "#endif\n",
                          "baz.c"),
            args);

    CreateTmpFile("#define FOOBAR 100\n", "a.h");
    CreateTmpFile("#define FOOBAR 100\n", file::JoinPath("a", "c.h"));
    args.push_back("-Ia");

    RunTest(compiler.path,
            CreateTmpFile(define_has_include + "#if __has_include(<a.h>)\n"
                                               "# include <a.h>\n"
                                               "#else\n"
                                               "# include <string.h>\n"
                                               "#endif\n",
                          "a.c"),
            args);
    RunTest(compiler.path,
            CreateTmpFile(define_has_include + "#if __has_include(<b.h>)\n"
                                               "# include <b.h>\n"
                                               "#else\n"
                                               "# include <string.h>\n"
                                               "#endif\n",
                          "b.c"),
            args);
    RunTest(compiler.path,
            CreateTmpFile(define_has_include + "#if __has_include(<c.h>)\n"
                                               "# include <c.h>\n"
                                               "#else\n"
                                               "# include <string.h>\n"
                                               "#endif\n",
                          "c.c"),
            args);
  }
}

TEST_F(CppIncludeProcessorTest, has_include_next) {
  const string define_has_include_next =
      "#ifndef __has_include_next\n"
      "# define __has_include_next(x) 0\n"
      "#endif\n";

  for (const auto& compiler : GccLikeCompilers()) {
    std::vector<string> args(compiler.additional_args);

    // Check __has_include_next existence.
    RunTest(compiler.path,
            CreateTmpFile("#ifndef __has_include\n"
                          " #include <stdio.h>\n"
                          "#endif",
                          "foo.c"),
            args);

    // include_next check.
    args = compiler.additional_args;
    args.push_back("-I" + tmpdir_util_->tmpdir());
    CreateTmpFile(define_has_include_next +
                      "#if __has_include_next(<stdio.h>)\n"
                      "# include_next <stdio.h>\n"
                      "#else\n"
                      "# include <stddef.h>\n"
                      "#endif\n",
                  "stdio.h");
    RunTest(compiler.path, CreateTmpFile("#include <stdio.h>\n", "foo.c"),
            args);

    // Nonexistent include_next check.
    CreateTmpFile(define_has_include_next +
                      "#if __has_include_next(<foo.h>)\n"
                      "# include_next <foo.h>\n"
                      "#endif\n",
                  "foo.h");
    RunTest(compiler.path, CreateTmpFile("#include <foo.h>\n", "foo.c"), args);

    CreateTmpFile(define_has_include_next +
                      "#if __has_include_next(<a.h>)\n"
                      "# include_next <a.h>\n"
                      "#else\n"
                      "# include <stddef.h>\n"
                      "#endif\n",
                  "a.h");
    args.push_back("-I.");
    args.push_back("-Ia");

    RunTest(compiler.path, CreateTmpFile("#include <a.h>\n", "foo.c"), args);

    const string& ah =
        CreateTmpFile("#define FOOBAR 100\n", file::JoinPath("a", "a.h"));
    RunTest(compiler.path, CreateTmpFile("#include <a.h>\n", "foo.c"), args);

    // Remove ah because it should not exist in next loop.
    remove(ah.c_str());
  }
}

TEST_F(CppIncludeProcessorTest, has_feature) {
  const string define_has_feature =
      "#ifndef __has_feature\n"
      "# define __has_feature(x) 0\n"
      "#endif\n";

  for (const auto& compiler : GccLikeCompilers()) {
    std::vector<string> args(compiler.additional_args);

    // Check the pre-defined macro itself.
    RunTest(compiler.path,
            CreateTmpFile("#ifdef __has_feature\n"
                          "# include <stdio.h>\n"
                          "#else\n"
                          "# include <stddef.h>\n"
                          "#endif\n",
                          "foo.c"),
            args);

    RunTest(
        compiler.path,
        CreateTmpFile(define_has_feature +
                          "#if __has_feature(attribute_cf_returns_retained)\n"
                          "# include <stdio.h>\n"
                          "#else\n"
                          "# include <stddef.h>\n"
                          "#endif\n",
                      "foo.c"),
        args);

    RunTest(compiler.path,
            CreateTmpFile(define_has_feature +
                              "#if __has_feature(no_such_feature)\n"
                              "# include <stdio.h>\n"
                              "#else\n"
                              "# include <stddef.h>\n"
                              "#endif\n",
                          "foo.c"),
            args);

    // When feature name has both leading and trailing __,
    // they should be ignored. __feature__ is normalized to feature.
    RunTest(compiler.path,
            CreateTmpFile(
                define_has_feature +
                    "#if __has_feature(__attribute_cf_returns_retained__)\n"
                    "# include <stdio.h>\n"
                    "#else\n"
                    "# include <stddef.h>\n"
                    "#endif\n",
                "foo.c"),
            args);

    // When feature name has one of leading or trailing __,
    // they cannot be ignored.
    RunTest(
        compiler.path,
        CreateTmpFile(define_has_feature +
                          "#if __has_feature(attribute_cf_returns_retained__)\n"
                          "# include <stdio.h>\n"
                          "#else\n"
                          "# include <stddef.h>\n"
                          "#endif\n",
                      "foo.c"),
        args);
    RunTest(
        compiler.path,
        CreateTmpFile(define_has_feature +
                          "#if __has_feature(__attribute_cf_returns_retained)\n"
                          "# include <stdio.h>\n"
                          "#else\n"
                          "# include <stddef.h>\n"
                          "#endif\n",
                      "foo.c"),
        args);
  }
}

TEST_F(CppIncludeProcessorTest, has_extension) {
  const string define_has_extension =
      "#ifndef __has_extension\n"
      "# define __has_extension(x) 0\n"
      "#endif\n";

  for (const auto& compiler : GccLikeCompilers()) {
    std::vector<string> args(compiler.additional_args);

    // Check the pre-defined macro itself.
    RunTest(compiler.path,
            CreateTmpFile("#ifdef __has_extension\n"
                          "# include <stdio.h>\n"
                          "#else\n"
                          "# include <stddef.h>\n"
                          "#endif\n",
                          "foo.c"),
            args);

    RunTest(compiler.path,
            CreateTmpFile(define_has_extension +
                              "#if __has_extension(c_static_assert)\n"
                              "# include <stdio.h>\n"
                              "#else\n"
                              "# include <stddef.h>\n"
                              "#endif\n",
                          "foo.c"),
            args);
  }
}

TEST_F(CppIncludeProcessorTest, has_cpp_attribute) {
  const string define_has_cpp_attribute =
      "#ifndef __has_cpp_attribute\n"
      "# define __has_cpp_attribute(x) 0\n"
      "#endif\n";

  for (const auto& compiler : GccLikeCompilers()) {
    std::vector<string> args(compiler.additional_args);

    // Check __has_cpp_attribute existence.
    // Don't add define_has_cpp_attribute.
    RunTest(compiler.path,
            CreateTmpFile("#ifdef __has_cpp_attribute\n"
                          "# include <stdio.h>\n"
                          "#else\n"
                          "# include <stddef.h>\n"
                          "#endif\n",
                          "foo.c"),
            args);

    // This example is taken from
    // http://clang.llvm.org/docs/LanguageExtensions.html
    // Note: __has_cpp_attribute(clang::fallthrough) does not work in c mode.
    // So, added #ifdef__cplusplus.
    RunTest(compiler.path,
            CreateTmpFile(define_has_cpp_attribute +
                              "#ifdef __cplusplus\n"
                              "#if __has_cpp_attribute(clang::fallthrough)\n"
                              "# include <stdio.h>\n"
                              "#else\n"
                              "# include <stddef.h>\n"
                              "#endif\n"
                              "#endif\n",
                          "foo.c"),
            args);

    // This example is taken from
    // http://isocpp.org/std/standing-documents/sd-6-sg10-feature-test-recommendations
    RunTest(compiler.path,
            CreateTmpFile(define_has_cpp_attribute +
                              "#if __has_cpp_attribute(deprecated)\n"
                              "# include <stdio.h>\n"
                              "#else\n"
                              "# include <stddef.h>\n"
                              "#endif\n",
                          "foo.c"),
            args);
  }
}

TEST_F(CppIncludeProcessorTest, has_declspec_attribute) {
  const string define_has_declspec_attribute =
      "#ifndef __has_declspec_attribute\n"
      "# define __has_declspec_attribute(x) 0\n"
      "#endif\n";

  for (const auto& compiler : GccLikeCompilers()) {
    std::vector<string> args(compiler.additional_args);

    // Check __has_declspec_attribute existence.
    // Don't add define_has_declspec_attribute.
    RunTest(compiler.path,
            CreateTmpFile("#ifdef __has_declspec_attribute\n"
                          "# include <stdio.h>\n"
                          "#else\n"
                          "# include <stddef.h>\n"
                          "#endif\n",
                          "foo.c"),
            args);

    RunTest(compiler.path,
            CreateTmpFile(define_has_declspec_attribute +
                              "#if __has_declspec_attribute(__stdcall)\n"
                              "# include <stdio.h>\n"
                              "#else\n"
                              "# include <stddef.h>\n"
                              "#endif\n",
                          "foo.c"),
            args);
  }
}

TEST_F(CppIncludeProcessorTest, has_builtin) {
  const string define_has_builtin =
      "#ifndef __has_builtin\n"
      "# define __has_builtin(x) 0\n"
      "#endif\n";

  for (const auto& compiler : GccLikeCompilers()) {
    std::vector<string> args(compiler.additional_args);

    // Check __has_builtin existence.
    // Don't add define_has_builtin.
    RunTest(compiler.path,
            CreateTmpFile("#ifdef __has_builtin\n"
                          "# include <stdio.h>\n"
                          "#else\n"
                          "# include <stddef.h>\n"
                          "#endif\n",
                          "foo.c"),
            args);

    RunTest(compiler.path,
            CreateTmpFile(define_has_builtin +
                              "#if __has_builtin(_InterlockedExchange)\n"
                              "# include <stdio.h>\n"
                              "#else\n"
                              "# include <stddef.h>\n"
                              "#endif\n",
                          "foo.c"),
            args);

    RunTest(compiler.path,
            CreateTmpFile(define_has_builtin +
                              "#if __has_builtin(__atomic_exchange)\n"
                              "# include <stdio.h>\n"
                              "#else\n"
                              "# include <stddef.h>\n"
                              "#endif\n",
                          "foo.c"),
            args);
  }
}

TEST_F(CppIncludeProcessorTest, dont_include_directory) {
  CreateTmpDir("iostream");

  std::vector<string> args;
  args.push_back("-I" + tmpdir_util_->tmpdir());
  RunTest("/usr/bin/gcc", CreateTmpFile("#include <iostream>", "foo.cpp"),
          args);
}

#else

// TODO: Add more CppIncludeProcessorTest for VC
TEST_F(CppIncludeProcessorTest, stdio) {
  std::vector<string> args;
  RunClTest(CreateTmpFile("#include <stdio.h>", "foo.c"), args);
}

TEST_F(CppIncludeProcessorTest, iostream) {
  std::vector<string> args;
  RunClTest(CreateTmpFile("#include <iostream>", "foo.cpp"), args);
}

TEST_F(CppIncludeProcessorTest, commandline_define) {
  std::vector<string> args;
  args.push_back("/DDEBUG");
  RunClTest(CreateTmpFile("#ifdef DEBUG\r\n#include <iostream>\r\n#endif\r\n",
                          "foo.cpp"),
            args);
}


TEST_F(CppIncludeProcessorTest, AtFile) {
  string at_file = CreateTmpFile("/DDEBUG", "at_file.rsp");
  at_file = "@" + at_file;
  std::vector<string> args;
  args.push_back(at_file.c_str());
  RunClTest(CreateTmpFile("#ifdef DEBUG\r\n#include <iostream>\r\n#endif\r\n",
                          "foo.cpp"),
            args);
}

TEST_F(CppIncludeProcessorTest, dont_include_directory) {
  const string& iostream_dir =
      file::JoinPath(tmpdir_util_->tmpdir(), "iostream");
  CreateDirectoryA(iostream_dir.c_str(), nullptr);

  std::vector<string> args;
  args.push_back("/I" + tmpdir_util_->tmpdir());
  RunClTest(CreateTmpFile("#include <iostream>", "foo.cpp"), args);
}

#endif  // !_WIN32

TEST_F(CppIncludeProcessorTest, define_defined_with_paren) {
  const string& bare_gcc = "/usr/bin/g++";
  const string& bare_cl = "cl.exe";
  const string& source_file = CreateTmpFile(
      "#define FOO\n"
      "#define DEFINED defined(FOO)\n"
      "#if DEFINED\n"
      "# include \"bar.h\"\n"
      "#endif\n",
      "foo.cc");
  string included = CreateTmpFile("", "bar.h");

  {
    std::vector<string> args;
    args.push_back(bare_gcc);
    args.push_back("-c");

    std::set<string> files = RunCppIncludeProcessor(source_file, args);
    ASSERT_EQ(1U, files.size());
    EXPECT_EQ(included, *files.begin());
  }

  {
    std::vector<string> args;
    args.push_back(bare_cl);
    args.push_back("/c");

    std::set<string> files = RunCppIncludeProcessor(source_file, args);
    EXPECT_TRUE(files.empty());
  }
}

TEST_F(CppIncludeProcessorTest, define_defined_without_paren) {
  const string& bare_gcc = "/usr/bin/g++";
  const string& bare_cl = "cl.exe";
  const string& source_file = CreateTmpFile(
      "#define FOO\n"
      "#define DEFINED defined FOO\n"
      "#if DEFINED\n"
      "# include \"bar.h\"\n"
      "#endif\n",
      "foo.cc");
  string included = CreateTmpFile("", "bar.h");

  {
    std::vector<string> args;
    args.push_back(bare_gcc);
    args.push_back("-c");
    args.push_back(source_file);

    std::set<string> files = RunCppIncludeProcessor(source_file, args);
    ASSERT_EQ(1U, files.size());
    EXPECT_EQ(included, *files.begin());
  }

  {
    std::vector<string> args;
    args.push_back(bare_cl);
    args.push_back("/c");
    args.push_back(source_file);

    std::set<string> files = RunCppIncludeProcessor(source_file, args);
    ASSERT_EQ(1U, files.size());
    EXPECT_EQ(included, *files.begin());
  }
}

TEST_F(CppIncludeProcessorTest, comment_in_macro) {
  const string& bare_gcc = "/usr/bin/g++";
  const string& bare_cl = "cl.exe";
  const string& source_file = CreateTmpFile(
      "#define BAR bar.h /**/\n"
      "#define STR_I(x) #x\n"
      "#define STR(x) STR_I(x)\n"
      "#include STR(BAR)\n",
      "foo.cc");
  string included = CreateTmpFile("", "bar.h");

  {
    std::vector<string> args;
    args.push_back(bare_gcc);
    args.push_back("-c");
    args.push_back(source_file);

    std::set<string> files = RunCppIncludeProcessor(source_file, args);
    ASSERT_EQ(1U, files.size());
    EXPECT_EQ(included, *files.begin());
  }

  {
    std::vector<string> args;
    args.push_back(bare_cl);
    args.push_back("/c");
    args.push_back(source_file);

    std::set<string> files = RunCppIncludeProcessor(source_file, args);
    ASSERT_EQ(1U, files.size());
    EXPECT_EQ(included, *files.begin());
  }
}

TEST_F(CppIncludeProcessorTest, comment_in_func_macro) {
  const string& bare_gcc = "/usr/bin/g++";
  const string& bare_cl = "cl.exe";
  const string& source_file = CreateTmpFile(
      "#define BAR(x) bar.h /**/\n"
      "#define STR_I(x) #x\n"
      "#define STR(x) STR_I(x)\n"
      "#include STR(BAR(hoge))\n",
      "foo.cc");
  string included = CreateTmpFile("", "bar.h");

  {
    std::vector<string> args;
    args.push_back(bare_gcc);
    args.push_back("-c");
    args.push_back(source_file);

    std::set<string> files = RunCppIncludeProcessor(source_file, args);
    ASSERT_EQ(1U, files.size());
    EXPECT_EQ(included, *files.begin());
  }

  {
    std::vector<string> args;
    args.push_back(bare_cl);
    args.push_back("/c");
    args.push_back(source_file);

    std::set<string> files = RunCppIncludeProcessor(source_file, args);
    ASSERT_EQ(1U, files.size());
    EXPECT_EQ(included, *files.begin());
  }
}

TEST_F(CppIncludeProcessorTest, opt_include) {
  const string& header = CreateTmpFile("", "foo.h");
  std::vector<string> args;
  args.push_back("gcc");
  args.push_back("-include");
  args.push_back(header);

  std::set<string> files =
      RunCppIncludeProcessor(CreateTmpFile("", "foo.c"), args);
  ASSERT_EQ(1U, files.size());
  EXPECT_EQ(header, *files.begin());
}

TEST_F(CppIncludeProcessorTest, opt_include_in_cwd) {
  CreateTmpFile("", "foo.h");
  std::vector<string> args;
  args.push_back("gcc");
  args.push_back("-include");
  args.push_back("foo.h");

  std::set<string> files =
      RunCppIncludeProcessor(CreateTmpFile("", "foo.c"), args);
  ASSERT_EQ(1U, files.size());
  EXPECT_EQ("foo.h", *files.begin());
}

TEST_F(CppIncludeProcessorTest, vc_opt_fi) {
  const string& header = CreateTmpFile("", "foo.h");
  std::vector<string> args;
  args.push_back("cl.exe");
  args.push_back("/c");
  args.push_back("/FI" + header);

  std::set<string> files =
      RunCppIncludeProcessor(CreateTmpFile("", "foo.c"), args);
  ASSERT_EQ(1U, files.size());
  ASSERT_EQ(header, *files.begin());
}

TEST_F(CppIncludeProcessorTest, no_newline_at_eof) {
  const string& bare_gcc = "/usr/bin/g++";
  const string& bare_cl = "cl.exe";
  const string& source_file = CreateTmpFile(
      "#if 1\n"
      "#include \"bar.h\"\n"
      "#include \"baz.h\"\n"
      "#endif\n",
      "foo.cc");
  string bar_h = CreateTmpFile(
      "#if 0\n"
      "#include \"hoge.h\"\n"
      "#endif",
      "bar.h");
  string baz_h = CreateTmpFile("", "baz.h");
  string hoge_h = CreateTmpFile("", "hoge.h");

  std::set<string> expected;
  expected.insert(bar_h);
  expected.insert(baz_h);

  {
    std::vector<string> args;
    args.push_back(bare_gcc);
    args.push_back("-c");
    args.push_back(source_file);

    std::set<string> files = RunCppIncludeProcessor(source_file, args);
    ASSERT_EQ(2U, files.size());
    EXPECT_EQ(expected, files);
  }

  {
    std::vector<string> args;
    args.push_back(bare_cl);
    args.push_back("/c");
    args.push_back(source_file);

    std::set<string> files = RunCppIncludeProcessor(source_file, args);
    ASSERT_EQ(2U, files.size());
    EXPECT_EQ(expected, files);
  }
}

TEST_F(CppIncludeProcessorTest, no_newline_at_eof_identifier) {
  const string& bare_gcc = "/usr/bin/gcc";
  const string& bare_cl = "cl.exe";
  const string& source_file = CreateTmpFile(
      "#include \"foo.h\"\n"
      "#include \"bar.h\"\n"
      "#\n",
      "foo.cc");
  const string& foo_h = CreateTmpFile(
      "#define foo",  // No newline at the end after an identifier.
      "foo.h");
  const string& bar_h = CreateTmpFile(
      "#ifdef foo\n"
      "#include \"baz.h\"\n"
      "#endif\n",
      "bar.h");
  const string& baz_h = CreateTmpFile("", "baz.h");

  std::set<string> expected;
  expected.insert(foo_h);
  expected.insert(bar_h);
  expected.insert(baz_h);

  {
    std::vector<string> args;
    args.push_back(bare_gcc);
    args.push_back("-c");
    args.push_back(source_file);

    std::set<string> files = RunCppIncludeProcessor(source_file, args);
    ASSERT_EQ(expected.size(), files.size());
    EXPECT_EQ(expected, files);
  }

  {
    std::vector<string> args;
    args.push_back(bare_cl);
    args.push_back("/c");
    args.push_back(source_file);

    std::set<string> files = RunCppIncludeProcessor(source_file, args);
    ASSERT_EQ(expected.size(), files.size());
    EXPECT_EQ(expected, files);
  }
}

TEST_F(CppIncludeProcessorTest, no_newline_at_eof_number) {
  const string& bare_gcc = "/usr/bin/gcc";
  const string& bare_cl = "cl.exe";
  const string& source_file = CreateTmpFile(
      "#include \"foo.h\"\n"
      "#define S(a) #a\n"
      "#define X(a) S(a.h)\n"
      "#include X(FOO)\n"
      "#\n",
      "foo.cc");
  const string& foo_h = CreateTmpFile(
      "#define FOO 999",  // No newline at the end after a pp-number.
      "foo.h");
  const string& nine_h = CreateTmpFile("", "999.h");

  std::set<string> expected;
  expected.insert(foo_h);
  expected.insert(nine_h);

  {
    std::vector<string> args;
    args.push_back(bare_gcc);
    args.push_back("-c");
    args.push_back(source_file);

    std::set<string> files = RunCppIncludeProcessor(source_file, args);
    ASSERT_EQ(expected.size(), files.size());
    EXPECT_EQ(expected, files);
  }

  {
    std::vector<string> args;
    args.push_back(bare_cl);
    args.push_back("/c");
    args.push_back(source_file);

    std::set<string> files = RunCppIncludeProcessor(source_file, args);
    ASSERT_EQ(expected.size(), files.size());
    EXPECT_EQ(expected, files);
  }
}

TEST_F(CppIncludeProcessorTest, condition_lines_lf) {
  const string& bare_gcc = "/usr/bin/g++";
  const string& source_file = CreateTmpFile(
      "#define A 1\n"
      "#define B 1\n"
      "#if defined(A) && \\\n"
      "    defined(B)\n"
      "#include \"bar.h\"\n"
      "#endif\n",
      "foo.cc");
  string bar_h = CreateTmpFile("", "bar.h");

  std::set<string> expected;
  expected.insert(bar_h);

  {
    std::vector<string> args;
    args.push_back(bare_gcc);
    args.push_back("-c");
    args.push_back(source_file);

    std::set<string> files = RunCppIncludeProcessor(source_file, args);
    ASSERT_EQ(1U, files.size());
    EXPECT_EQ(expected, files);
  }
}

TEST_F(CppIncludeProcessorTest, condition_lines_crlf) {
  const string& bare_cl = "cl.exe";
  const string& source_file = CreateTmpFile(
      "#define A 1\r\n"
      "#define B 1\r\n"
      "#if defined(A) && \\\r\n"
      "    defined(B)\r\n"
      "#include \"bar.h\"\r\n"
      "#endif\\r\n",
      "foo.cc");
  string bar_h = CreateTmpFile("", "bar.h");

  std::set<string> expected;
  expected.insert(bar_h);
  {
    std::vector<string> args;
    args.push_back(bare_cl);
    args.push_back("/c");
    args.push_back(source_file);

    std::set<string> files = RunCppIncludeProcessor(source_file, args);
    ASSERT_EQ(1U, files.size());
    EXPECT_EQ(expected, files);
  }
}

TEST_F(CppIncludeProcessorTest, include_cur_from_include_paths) {
  // b/7626343

  const string& bare_gcc = "/usr/bin/g++";
  const string& bare_cl = "cl.exe";
  const string& source_file =
      CreateTmpFile("#include \"primpl.h\"\n", "foo.cc");
  const string& dir1 = "dir1";
  const string& nspr_h = file::JoinPath(dir1, "nspr.h");
  CreateTmpFile("", nspr_h);
  const string& dir2 = "dir2";
  const string& primpl_h = file::JoinPath(dir2, "primpl.h");
  CreateTmpFile("#include \"nspr.h\"\n", primpl_h);

  std::set<string> expected{nspr_h, primpl_h};

  {
    std::vector<string> args;
    args.push_back(bare_gcc);
    args.push_back("-I" + dir1);
    args.push_back("-I" + dir2);
    args.push_back("-c");
    args.push_back(source_file);

    std::set<string> files = RunCppIncludeProcessor(source_file, args);
    ASSERT_EQ(expected.size(), files.size());
    EXPECT_EQ(expected, files);
  }

  {
    std::vector<string> args;
    args.push_back(bare_cl);
    args.push_back("/I" + dir1);
    args.push_back("/I" + dir2);
    args.push_back("/c");
    args.push_back(source_file);

    std::set<string> files = RunCppIncludeProcessor(source_file, args);
    ASSERT_EQ(expected.size(), files.size());
    EXPECT_EQ(expected, files);
  }
}

TEST_F(CppIncludeProcessorTest, include_next_multiple_file) {
  // b/7461986
  const string& bare_gcc = "/usr/bin/g++";
  const string& source_file =
      CreateTmpFile("#include \"limits.h\"\n",  // limits_h_0
                    "foo.cc");
  const string& limits_h_0 =
      CreateTmpFile("#include_next \"limits.h\"\n",  // limits_h_1
                    "limits.h");
  const string& dir1 = "dir1";
  const string& limits_h_1 = file::JoinPath(dir1, "limits.h");
  CreateTmpFile(
      "#ifndef _LIBC_LIMITS_H\n"    // not defined yet
      "#include \"syslimits.h\"\n"  // so it should be included
      "#endif\n",
      limits_h_1);
  const string& syslimits_h = file::JoinPath(dir1, "syslimits.h");
  CreateTmpFile("", syslimits_h);
  const string& dir2 = "dir2";
  // If limits_h_2 is included (before limits_h_1), syslimits.h would not be
  // included.
  const string& limits_h_2 = CreateTmpFile("#define _LIBC_LIMITS_H\n",
                                           file::JoinPath(dir2, "limits.h"));

  ASSERT_NE(limits_h_1, limits_h_2);

  std::set<string> expected{
      limits_h_0, limits_h_1, syslimits_h,
  };

  {
    std::vector<string> args;
    args.push_back(bare_gcc);
    args.push_back("-I" + dir1);
    args.push_back("-I" + dir2);
    args.push_back("-c");
    args.push_back(source_file);

    std::set<string> files = RunCppIncludeProcessor(source_file, args);
    ASSERT_EQ(expected.size(), files.size());
    EXPECT_EQ(expected, files);
  }
}

TEST_F(CppIncludeProcessorTest, include_next_from_include_current_dir) {
  // b/7461986
  const string& bare_gcc = "/usr/bin/g++";
  const string& source_file =
      CreateTmpFile("#include \"limits.h\"\n",  // include limits_h_0 (curdir)
                    "foo.cc");
  const string& limits_h_0 = CreateTmpFile(
      "#include_next <limits.h>\n",  // include limits_h_1 (first inc dir)
      "limits.h");
  const string& dir1 = "dir1";
  const string& limits_h_1 = file::JoinPath(dir1, "limits.h");
  CreateTmpFile(
      "#ifndef _LIBC_LIMITS_H\n"    // not defined yet
      "#include \"syslimits.h\"\n"  // so it should be included
      "#endif\n",
      limits_h_1);
  const string& syslimits_h = file::JoinPath(dir1, "syslimits.h");
  CreateTmpFile(
      "#include_next <limits.h>\n",  // include limits_h_2 (second inc dir)
      syslimits_h);
  const string& dir2 = "dir2";
  // If limits_h_2 is included from syslimits.h
  const string& limits_h_2 = file::JoinPath(dir2, "limits.h");
  CreateTmpFile("#define _LIBC_LIMITS_H\n", limits_h_2);

  ASSERT_NE(limits_h_1, limits_h_2);

  std::set<string> expected{
      limits_h_0, limits_h_1, syslimits_h, limits_h_2,
  };

  {
    std::vector<string> args;
    args.push_back(bare_gcc);
    args.push_back("-I" + dir1);
    args.push_back("-I" + dir2);
    args.push_back("-c");
    args.push_back(source_file);

    std::set<string> files = RunCppIncludeProcessor(source_file, args);
    ASSERT_EQ(expected.size(), files.size());
    EXPECT_EQ(expected, files);
  }
}

TEST_F(CppIncludeProcessorTest, include_next_from_next_dir) {
  // b/7462563

  const string& bare_gcc = "/usr/bin/g++";
  const string& source_file =
      CreateTmpFile("#include <_clocale.h>\n",  // clocate_h
                    "foo.cc");
  const string& dir1 = "dir1";
  const string& clocale_h = file::JoinPath(dir1, "_clocale.h");
  CreateTmpFile("#include_next <clocale>\n",  // include clocale_2
                clocale_h);
  const string& clocale_1 = CreateTmpFile("", file::JoinPath(dir1, "clocale"));
  const string& dir2 = "dir2";
  const string& clocale_2 = file::JoinPath(dir2, "clocale");
  CreateTmpFile("", clocale_2);

  ASSERT_NE(clocale_1, clocale_2);

  std::set<string> expected{clocale_h, clocale_2};

  {
    std::vector<string> args;
    args.push_back(bare_gcc);
    args.push_back("-I" + dir1);
    args.push_back("-I" + dir2);
    args.push_back("-c");
    args.push_back(source_file);

    std::set<string> files = RunCppIncludeProcessor(source_file, args);
    ASSERT_EQ(expected.size(), files.size());
    EXPECT_EQ(expected, files);
  }
}

TEST_F(CppIncludeProcessorTest, invalidated_macro_in_offspring) {
  const string& bare_gcc = "/usr/bin/gcc";
  const string& bare_cl = "cl.exe";
  const string& source_file = CreateTmpFile(
      "#define var1\n"
      "#include \"step1.h\"\n"
      "#include \"step1.h\"\n"
      "#\n",
      "foo.cc");
  const string& step1_h = CreateTmpFile(
      "#include \"step2.h\"\n"
      "#undef var1\n",
      "step1.h");
  const string& step2_h = CreateTmpFile(
      "#if !defined var1\n"
      "#define var2\n"
      "#endif\n"
      "\n"
      "#ifdef var2\n"
      "#include \"step3.h\"\n"
      "#endif\n",
      "step2.h");
  const string& step3_h = CreateTmpFile("\n", "step3.h");

  std::set<string> expected;
  expected.insert(step1_h);
  expected.insert(step2_h);
  expected.insert(step3_h);

  {
    std::vector<string> args;
    args.push_back(bare_gcc);
    args.push_back("-c");
    args.push_back(source_file);

    std::set<string> files = RunCppIncludeProcessor(source_file, args);
    ASSERT_EQ(expected.size(), files.size());
    EXPECT_EQ(expected, files);
  }

  {
    std::vector<string> args;
    args.push_back(bare_cl);
    args.push_back("/c");
    args.push_back(source_file);

    std::set<string> files = RunCppIncludeProcessor(source_file, args);
    ASSERT_EQ(expected.size(), files.size());
    EXPECT_EQ(expected, files);
  }
}

TEST_F(CppIncludeProcessorTest, include_ignore_dir) {
  const string& bare_gcc = "/usr/bin/gcc";
  const string& bare_cl = "cl.exe";
  const string& source_file = CreateTmpFile("#include \"string\"\n", "foo.cc");
  const string& string_dir = "string";
  CHECK(file::CreateDir(tmpdir_util_->FullPath(string_dir).c_str(),
                        file::CreationMode(0777))
            .ok());
  const string& dir1 = "dir1";
  const string& string_h = file::JoinPath(dir1, "string");
  CreateTmpFile("", string_h);

  std::set<string> expected{string_h};

  {
    std::vector<string> args;
    args.push_back(bare_gcc);
    args.push_back("-I" + dir1);
    args.push_back("-c");
    args.push_back(source_file);

    std::set<string> files = RunCppIncludeProcessor(source_file, args);
    ASSERT_EQ(expected.size(), files.size());
    EXPECT_EQ(expected, files);
  }

  {
    std::vector<string> args;
    args.push_back(bare_cl);
    args.push_back("/I" + dir1);
    args.push_back("/c");
    args.push_back(source_file);

    std::set<string> files = RunCppIncludeProcessor(source_file, args);
    ASSERT_EQ(expected.size(), files.size());
    EXPECT_EQ(expected, files);
  }
}

TEST_F(CppIncludeProcessorTest, include_next_ignore_dir) {
  const string& bare_gcc = "/usr/bin/gcc";
  const string& bare_cl = "cl.exe";
  const string& source_file = CreateTmpFile("#include <foo.h>\n", "foo.cc");
  const string& dir1 = "dir1";
  const string& foo_h = file::JoinPath(dir1, "foo.h");
  CreateTmpFile("#include <string>\n", foo_h);
  const string& string1 = file::JoinPath(dir1, "string");
  CreateTmpFile("#include_next <string>\n", string1);
  const string& dir2 = "dir2";
  const string& dir3 = "dir3";
  const string& string3 = file::JoinPath(dir3, "string");
  CreateTmpFile("", string3);

  std::set<string> expected{
      foo_h, string1, string3,
  };

  {
    std::vector<string> args;
    args.push_back(bare_gcc);
    args.push_back("-I" + dir1);
    args.push_back("-I" + dir2);
    args.push_back("-I" + dir3);
    args.push_back("-c");
    args.push_back(source_file);
    std::set<string> files = RunCppIncludeProcessor(source_file, args);
    ASSERT_EQ(expected.size(), files.size());
    EXPECT_EQ(expected, files);
  }

  {
    std::vector<string> args;
    args.push_back(bare_cl);
    args.push_back("/I" + dir1);
    args.push_back("/I" + dir2);
    args.push_back("/I" + dir3);
    args.push_back("/c");
    args.push_back(source_file);
    std::set<string> files = RunCppIncludeProcessor(source_file, args);
    ASSERT_EQ(expected.size(), files.size());
    EXPECT_EQ(expected, files);
  }
}

TEST_F(CppIncludeProcessorTest, include_path_two_slashes_in_dir_cache) {
  // b/7618390

  const string& bare_gcc = "/usr/bin/g++";
  const string& bare_cl = "cl.exe";
  const string& source_file = CreateTmpFile(
      "#include \"dir2//foo.h\"\n"        // foo_h
      "#include \"dir3//dir4//bar.h\"\n"  // bar_h
      "#include \"dir3/dir4/baz.h\"\n",   // baz_h
      "foo.cc");
  const string& dir1 = "dir1";
  const string& dir2 = file::JoinPath(dir1, "dir2");
  const string& foo_h = file::JoinPath(dir2, "foo.h");
  CreateTmpFile("", foo_h);
  const string& dir3 = file::JoinPath(dir1, "dir3");
  const string& dir4 = file::JoinPath(dir3, "dir4");
  const string& bar_h = file::JoinPath(dir4, "bar.h");
  CreateTmpFile("", bar_h);
  const string& baz_h = file::JoinPath(dir4, "baz.h");
  CreateTmpFile("", baz_h);

  std::set<string> expected{foo_h, bar_h, baz_h};

  {
    std::vector<string> args;
    args.push_back(bare_gcc);
    args.push_back("-I" + dir1);
    args.push_back("-c");
    args.push_back(source_file);

    std::set<string> files = RunCppIncludeProcessor(source_file, args);
    ASSERT_EQ(expected.size(), files.size());
    EXPECT_EQ(expected, files);
  }

  {
    std::vector<string> args;
    args.push_back(bare_cl);
    args.push_back("/I" + dir1);
    args.push_back("/c");
    args.push_back(source_file);

    std::set<string> files = RunCppIncludeProcessor(source_file, args);
    ASSERT_EQ(expected.size(), files.size());
    EXPECT_EQ(expected, files);
  }
}

TEST_F(CppIncludeProcessorTest, include_unresolved_path) {
  const string& bare_gcc = "/usr/bin/g++";
  const string& bare_cl = "cl.exe";
  const string& source_file = CreateTmpFile(
      "#include \"dir2/../foo.h\"\n"         // foo_h
      "#include \"dir2//../hoge.h\"\n"       // hoge_h
      "#include \"dir3/../dir4/bar.h\"\n"    // bar_h
      "#include \"dir3/..//dir4/baz.h\"\n",  // baz_h
      "foo.cc");
  const string& dir1 = "dir1";
  const string& full_dir1 = file::JoinPath(tmpdir_util_->tmpdir(), dir1);
  CHECK(file::CreateDir(full_dir1.c_str(), file::CreationMode(0777)).ok());
  const string& foo_h = CreateTmpFile("", file::JoinPath(dir1, "foo.h"));
  const string& hoge_h = CreateTmpFile("", file::JoinPath(dir1, "hoge.h"));
  const string& dir2 = file::JoinPath(dir1, "dir2");
  const string& full_dir2 = file::JoinPath(tmpdir_util_->tmpdir(), dir2);
  CHECK(file::CreateDir(full_dir2.c_str(), file::CreationMode(0777)).ok());
  const string& unresolved_foo_h =
      file::JoinPath(file::JoinPath(dir2, ".."), "foo.h");
  ASSERT_NE(unresolved_foo_h, foo_h);
  const string& unresolved_hoge_h =
      file::JoinPath(file::JoinPath(dir2, ".."), "hoge.h");
  ASSERT_NE(unresolved_hoge_h, hoge_h);
  const string& dir3 = file::JoinPath(dir1, "dir3");
  const string& full_dir3 = file::JoinPath(tmpdir_util_->tmpdir(), dir3);
  CHECK(file::CreateDir(full_dir3.c_str(), file::CreationMode(0777)).ok());
  const string& dir4 = file::JoinPath(dir1, "dir4");
  CHECK(file::CreateDir(file::JoinPath(tmpdir_util_->tmpdir(), dir4).c_str(),
                        file::CreationMode(0777))
            .ok());
  const string& bar_h = CreateTmpFile("", file::JoinPath(dir4, "bar.h"));
  const string& baz_h = CreateTmpFile("", file::JoinPath(dir4, "baz.h"));
  const string& unresolved_bar_h = file::JoinPath(
      file::JoinPath(file::JoinPath(dir3, ".."), "dir4"), "bar.h");
  ASSERT_NE(unresolved_bar_h, bar_h);
  const string& unresolved_baz_h = file::JoinPath(
      file::JoinPath(file::JoinPath(dir3, ".."), "dir4"), "baz.h");
  ASSERT_NE(unresolved_baz_h, baz_h);

  std::set<string> expected;
  expected.insert(unresolved_foo_h);
  expected.insert(unresolved_hoge_h);
  expected.insert(unresolved_bar_h);
  expected.insert(unresolved_baz_h);

  {
    std::vector<string> args;
    args.push_back(bare_gcc);
    args.push_back("-I" + dir1);
    args.push_back("-c");
    args.push_back(source_file);

    std::set<string> files = RunCppIncludeProcessor(source_file, args);
    ASSERT_EQ(expected.size(), files.size());
    EXPECT_EQ(expected, files);
  }

  {
    std::vector<string> args;
    args.push_back(bare_cl);
    args.push_back("/I" + dir1);
    args.push_back("/c");
    args.push_back(source_file);

    std::set<string> files = RunCppIncludeProcessor(source_file, args);
    ASSERT_EQ(expected.size(), files.size());
    EXPECT_EQ(expected, files);
  }
}

TEST_F(CppIncludeProcessorTest, newline_before_include) {
  const string& dir1 = "dir1";

  const string& foo_h = CreateTmpFile("", file::JoinPath(dir1, "foo.h"));
  const string& foo_cc =
      CreateTmpFile("\n#include \"foo.h\"", file::JoinPath(dir1, "foo.cc"));

  std::vector<string> args;
  args.push_back("/usr/bin/g++");
  args.push_back("-c");
  args.push_back("-I" + dir1);
  args.push_back(foo_cc);

  std::set<string> expected;
  expected.insert(foo_h);

  std::set<string> files = RunCppIncludeProcessor(foo_cc, args);
  ASSERT_EQ(expected.size(), files.size());
  EXPECT_EQ(expected, files);
}

TEST_F(CppIncludeProcessorTest, newline_and_spaces_before_include) {
  const string& dir1 = "dir1";

  const string& foo_h = CreateTmpFile("", file::JoinPath(dir1, "foo.h"));
  const string& foo_cc = CreateTmpFile("f();   \n   #include \"foo.h\"",
                                       file::JoinPath(dir1, "foo.cc"));

  std::vector<string> args;
  args.push_back("/usr/bin/g++");
  args.push_back("-c");
  args.push_back("-I" + dir1);
  args.push_back(foo_cc);

  std::set<string> expected;
  expected.insert(foo_h);

  std::set<string> files = RunCppIncludeProcessor(foo_cc, args);
  ASSERT_EQ(expected.size(), files.size());
  EXPECT_EQ(expected, files);
}

TEST_F(CppIncludeProcessorTest, noncomment_token_before_include) {
  const string& dir1 = "dir1";

  CreateTmpFile("", file::JoinPath(dir1, "foo.h"));
  const string& foo_cc = CreateTmpFile("f(); \t   #include \"foo.h\"",
                                       file::JoinPath(dir1, "foo.cc"));

  std::vector<string> args;
  args.push_back("/usr/bin/g++");
  args.push_back("-c");
  args.push_back("-I" + dir1);
  args.push_back(foo_cc);

  std::set<string> expected;

  std::set<string> files = RunCppIncludeProcessor(foo_cc, args);
  ASSERT_EQ(expected.size(), files.size());
  EXPECT_EQ(expected, files);
}

TEST_F(CppIncludeProcessorTest, comment_slash_followed_by_include_simple) {
  const string& dir1 = "dir1";

  const string& foo1_h = CreateTmpFile("", file::JoinPath(dir1, "foo1.h"));
  const string& foo2_h = CreateTmpFile("", file::JoinPath(dir1, "foo2.h"));
  const string& foo_cc = CreateTmpFile(
      "   \\\n#include \"foo1.h\"\n  /* test */ \\\n#include \"foo2.h\"",
      file::JoinPath(dir1, "foo.cc"));

  std::vector<string> args;
  args.push_back("/usr/bin/g++");
  args.push_back("-c");
  args.push_back("-I" + dir1);
  args.push_back(foo_cc);

  std::set<string> expected;
  expected.insert(foo1_h);
  expected.insert(foo2_h);

  std::set<string> files = RunCppIncludeProcessor(foo_cc, args);
  ASSERT_EQ(expected.size(), files.size());
  EXPECT_EQ(expected, files);
}

TEST_F(CppIncludeProcessorTest, comment_slash_followed_by_include_complex1) {
  const string& dir1 = "dir1";

  const string& foo_h = CreateTmpFile("", file::JoinPath(dir1, "foo.h"));
  const string& foo_cc = CreateTmpFile(
      "  /* test */ \\\r\n /* test 2 */ /* */ \\\n"
      "\\\n /* foo bar */ \\\n#include \"foo.h\"",
      file::JoinPath(dir1, "foo.cc"));

  std::vector<string> args;
  args.push_back("/usr/bin/g++");
  args.push_back("-c");
  args.push_back("-I" + dir1);
  args.push_back(foo_cc);

  std::set<string> expected;
  expected.insert(foo_h);

  std::set<string> files = RunCppIncludeProcessor(foo_cc, args);
  ASSERT_EQ(expected.size(), files.size());
  EXPECT_EQ(expected, files);
}

TEST_F(CppIncludeProcessorTest, comment_slash_followed_by_include_complex2) {
  const string& dir1 = "dir1";

  const string& foo_h = CreateTmpFile("", file::JoinPath(dir1, "foo.h"));
  const string& foo_cc = CreateTmpFile(
      "#define FOO \"foo.h\"\n"
      "  /* test */ \\\r\n /* test 2 */ /* */ \\\n"
      "\\\n /* foo bar */ \\\n#include FOO",
      file::JoinPath(dir1, "foo.cc"));

  std::vector<string> args;
  args.push_back("/usr/bin/g++");
  args.push_back("-c");
  args.push_back("-I" + dir1);
  args.push_back(foo_cc);

  std::set<string> expected;
  expected.insert(foo_h);

  std::set<string> files = RunCppIncludeProcessor(foo_cc, args);
  ASSERT_EQ(expected.size(), files.size());
  EXPECT_EQ(expected, files);
}

TEST_F(CppIncludeProcessorTest, include_boost_pp_iterate) {
  const string& foo_cc = CreateTmpFile(
      // simplified case for BOOST_PP_ITERATE
      // cf. b/14593802
      // <boost/preprocessor/cat.hpp>
      "#define CAT(a, b) CAT_I(a, b)\n"
      "#define CAT_I(a, b) CAT_II(~, a ## b)\n"
      "#define CAT_II(p, res) res\n"
      // <boost/preprocessor/arithmetic/inc.cpp>
      "#define INC(x) INC_I(x)\n"
      "#define INC_I(x) INC_ ## x\n"
      "#define INC_0 1\n"
      "#define INC_1 2\n"
      // <boost/preprocessor/iteration/iterate.hpp>
      "#define DEPTH() 0\n"
      "\n"
      "#define ITERATE() CAT(ITERATE_, INC(DEPTH()))\n"
      "#define ITERATE_1 <bar1.h>\n"
      "#define ITERATE_2 <bar2.h>\n"
      // use ITERATE
      "#include ITERATE()\n",
      "foo.cc");
  CreateTmpFile("", "bar1.h");
  CreateTmpFile("", "bar2.h");
  std::set<string> expected{file::JoinPath(".", "bar1.h")};

  {
    std::vector<string> args;
    args.push_back("/usr/bin/g++");
    args.push_back("-c");
    args.push_back("-I.");
    args.push_back(foo_cc);

    std::set<string> files = RunCppIncludeProcessor(foo_cc, args);
    ASSERT_EQ(expected.size(), files.size());
    EXPECT_EQ(expected, files);
  }

  {
    std::vector<string> args;
    args.push_back("cl.exe");
    args.push_back("/c");
    args.push_back("/I.");
    args.push_back(foo_cc);

    std::set<string> files = RunCppIncludeProcessor(foo_cc, args);
    ASSERT_EQ(expected.size(), files.size());
    EXPECT_EQ(expected, files);
  }
}

TEST_F(CppIncludeProcessorTest, include_boost_pp_iterate_va_args) {
  const string& foo_cc = CreateTmpFile(
      // simplified case for BOOST_PP_ITERATE
      // cf.
      // boost v1.49.0
      // TODO: MSVC has slightly different semantics in __VA_ARGS__,
      // one more BOOST_PP_CAT needed?
      // e.g. #define BOOST_PP_VARIADIC_SIZE(...) \
      //  BOOST_PP_CAT(BOOST_PP_VARIADIC_SIZE_I(<same>),)
      // <boost/preprocessor/cat.hpp>
      "#define BOOST_PP_CAT(a, b) BOOST_PP_CAT_I(a, b)\n"
      "#define BOOST_PP_CAT_I(a, b) a ## b\n"
      // <boost/preprocessor/tuple/rem.hpp>
      "#define BOOST_PP_REM(...) __VA_ARGS__\n"
      // <boost/preprocessor/variadic/size.hpp>
      "#define BOOST_PP_VARIADIC_SIZE(...) "
      " BOOST_PP_VARIADIC_SIZE_I(__VA_ARGS__, 64, 63, 62, 61, 60, 59, 58, 57,"
      " 56, 55, 54, 53, 52, 51, 50, 49, 48, 47, 46, 45, 44, 43, 42, 41,40,"
      " 39, 38, 37, 36, 35, 34, 33, 32, 31, 30, 29, 28, 27, 26, 25, 24, 23,"
      " 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5,"
      " 4, 3, 2, 1,)\n"
      "#define BOOST_PP_VARIADIC_SIZE_I(e0, e1, e2, e3, e4, e5, e6, e7, e8,"
      " e9, e10, e11, e12, e13, e14, e15, e16, e17, e18, e19, e20, e21, e22,"
      " e23, e24, e25, e26, e27, e28, e29, e30, e31, e32, e33, e34, e35, e36,"
      " e37, e38, e39, e40, e41, e42, e43, e44, e45, e46, e47,e48, e49, e50,"
      " e51, e52, e53, e54, e55, e56, e57, e58, e59, e60, e61, e62, e63,"
      " size, ...) size\n"
      // <boost/preprocessor/facilities/overload.hpp>
      "#define BOOST_PP_OVERLOAD(prefix, ...) "
      " BOOST_PP_CAT(prefix, BOOST_PP_VARIADIC_SIZE(__VA_ARGS__))\n"
      // <boost/preprocessor/variadic/elem.hpp>
      "#define BOOST_PP_VARIADIC_ELEM(n, ...) "
      " BOOST_PP_CAT(BOOST_PP_VARIADIC_ELEM_, n)(__VA_ARGS__,)\n"
      "#define BOOST_PP_VARIADIC_ELEM_0(e0, ...) e0\n"
      "#define BOOST_PP_VARIADIC_ELEM_1(e0, e1, ...) e1\n"
      "#define BOOST_PP_VARIADIC_ELEM_2(e0, e1, e2, ...) e2\n"
      // <boost/preprocessor/tuple/elem.hpp>
      "#define BOOST_PP_TUPLE_ELEM(...) "
      "  BOOST_PP_OVERLOAD(BOOST_PP_TUPLE_ELEM_O_, __VA_ARGS__)(__VA_ARGS__)\n"
      "#define BOOST_PP_TUPLE_ELEM_O_2(n, tuple) "
      " BOOST_PP_VARIADIC_ELEM(n, BOOST_PP_REM tuple)\n"
      "#define BOOST_PP_TUPLE_ELEM_O_3(size, n, tuple) "
      " BOOST_PP_TUPLE_ELEM_O_2(n, tuple)\n"
      // <boost/preprocessor/array/size.hpp>
      "#define BOOST_PP_ARRAY_SIZE(array) BOOST_PP_TUPLE_ELEM(2, 0, array)\n"
      // <boost/preprocessor/array/data.hpp>
      "#define BOOST_PP_ARRAY_DATA(array) BOOST_PP_TUPLE_ELEM(2, 1, array)\n"
      // <boost/preprocessor/array/elem.hpp>
      "#define BOOST_PP_ARRAY_ELEM(i, array) "
      " BOOST_PP_TUPLE_ELEM(BOOST_PP_ARRAY_SIZE(array), i,"
      " BOOST_PP_ARRAY_DATA(array))\n"
      // <boost/utility/result_of.hpp>
      "#define BOOST_RESULT_OF_NUM_ARGS 10\n"
      "#define BOOST_PP_ITERATION_PARAMS_1 "
      " (3,(0,BOOST_RESULT_OF_NUM_ARGS,<bar1.h>))\n"
      // <boost/preprocessor/iteration/detail/iter/forward1.hpp>
      "#define BOOST_PP_FILENAME_1 "
      "  BOOST_PP_ARRAY_ELEM(2, BOOST_PP_ITERATION_PARAMS_1)\n"
      "#define BOOST_PP_ITERATION_1 0\n"
      "#include BOOST_PP_FILENAME_1\n",
      "foo.cc");
  CreateTmpFile("", "bar1.h");
  std::set<string> expected{file::JoinPath(".", "bar1.h")};
  {
    std::vector<string> args;
    args.push_back("/usr/bin/g++");
    args.push_back("-c");
    args.push_back("-I.");
    args.push_back(foo_cc);

    std::set<string> files = RunCppIncludeProcessor(foo_cc, args);
    ASSERT_EQ(expected.size(), files.size());
    EXPECT_EQ(expected, files);
  }
}

TEST_F(CppIncludeProcessorTest, include_next_self) {
  const string& bare_gcc = "/usr/bin/g++";

  const string& source_file = CreateTmpFile("#include \"a.h\"\n", "a.cc");
  const string& ah = CreateTmpFile("#include_next <a.h>\n", "a.h");

  const string& aah = file::JoinPath("a", "a.h");
  CreateTmpFile("", aah);

  std::vector<string> args;
  args.push_back(bare_gcc);
  args.push_back("-I.");
  args.push_back("-Ia");
  args.push_back("-c");
  args.push_back(source_file);

  std::set<string> expected{ah, file::JoinPath(".", "a.h"), aah};

  std::set<string> files = RunCppIncludeProcessor(source_file, args);
  EXPECT_EQ(expected, files);
}

TEST_F(CppIncludeProcessorTest, include_quote_from_current) {
  const string& bare_gcc = "/usr/bin/g++";

  const string& source_file =
      CreateTmpFile("#include \"a.h\"\n", file::JoinPath("a", "a.cc"));
  const string& aah = CreateTmpFile("", file::JoinPath("a", "a.h"));

  std::vector<string> args;
  args.push_back(bare_gcc);
  args.push_back("-c");
  args.push_back(source_file);

  std::set<string> expected;
  expected.insert(aah);

  std::set<string> files = RunCppIncludeProcessor(source_file, args);
  ASSERT_EQ(expected.size(), files.size());
  EXPECT_EQ(expected, files);
}

TEST_F(CppIncludeProcessorTest, include_sibling) {
  const string& bare_gcc = "/usr/bin/g++";

  const string& source_file =
      CreateTmpFile("#include \"../b/b.h\"\n", file::JoinPath("a", "a.cc"));
  const string& bbh = CreateTmpFile("", file::JoinPath("a", "..", "b", "b.h"));

  std::vector<string> args;
  args.push_back(bare_gcc);
  args.push_back("-c");
  args.push_back(source_file);

  std::set<string> expected;
  expected.insert(bbh);

  std::set<string> files = RunCppIncludeProcessor(source_file, args);
  ASSERT_EQ(expected.size(), files.size());
  EXPECT_EQ(expected, files);
}

#ifdef __MACH__

TEST_F(CppIncludeProcessorTest, curdir_framework) {
  // b/31843347
  CreateTmpDir("EarlGrey.framework");
  CreateTmpDir("EarlGrey.framework/Headers");
  CreateTmpFile("", "EarlGrey.framework/Headers/EarlGrey.h");

  std::vector<string> args;
  args.push_back("-F");
  args.push_back(".");
  RunTest("/usr/bin/gcc",
          CreateTmpFile("#import <EarlGrey/EarlGrey.h>\n", "foo.mm"), args);
}

TEST_F(CppIncludeProcessorTest, sub_framework) {
  // b/23128924
  std::vector<string> args;
  RunTest("/usr/bin/gcc",
          CreateTmpFile("#include <Accelerate/Accelerate.h>", "foo.cc"), args);
}
#endif

TEST_F(CppIncludeProcessorTest, include_from_dir) {
  const string& ac = file::JoinPath("test", "a.c");
  CreateTmpFile("#include \"a.h\"\n", ac);

  const string& ah = file::JoinPath("test", "a.h");
  CreateTmpFile("", ah);

  std::vector<string> args{"/usr/bin/gcc", "-c", ac};
  std::set<string> expected{ah};

  std::set<string> files = RunCppIncludeProcessor(ac, args);
  ASSERT_EQ(expected.size(), files.size());
  EXPECT_EQ(expected, files);
}

TEST_F(CppIncludeProcessorTest, include_from_dir_in_include_dir) {
  const string& ac = "a.c";
  CreateTmpFile("#include <test/a.h>\n", ac);

  const string& ah = file::JoinPath(".", "test", "a.h");
  CreateTmpFile("#include \"b.h\"", ah);

  const string& bh = file::JoinPath(".", "test", "b.h");
  CreateTmpFile("", bh);

  std::vector<string> args{"/usr/bin/gcc", "-I.", "-c", ac};
  std::set<string> expected{ah, bh};

  std::set<string> files = RunCppIncludeProcessor(ac, args);
  ASSERT_EQ(expected.size(), files.size());
  EXPECT_EQ(expected, files);
}

TEST_F(CppIncludeProcessorTest, include_from_abs_rel_include_dir) {
  const string& ac = "a.c";
  CreateTmpFile(
      "#include <abs.h>\n"
      "#include <rel.h>\n",
      ac);

  const string& relh = file::JoinPath("rel", "rel.h");
  CreateTmpFile("", relh);

  const string& absh = CreateTmpFile("", file::JoinPath("abs", "abs.h"));

  std::vector<string> args{"/usr/bin/gcc", "-Irel",
                           "-I" + tmpdir_util_->FullPath("abs"), "-c", ac};
  std::set<string> expected{relh, absh};

  std::set<string> files = RunCppIncludeProcessor(ac, args);
  ASSERT_EQ(expected.size(), files.size());
  EXPECT_EQ(expected, files);
}

TEST_F(CppIncludeProcessorTest, include_guard_once_alias) {
  const string& ac = file::JoinPath("a", "a.c");
  CreateTmpFile("#include \"../b/b.h\"\n", ac);

  const string& bh = file::JoinPath("a", "..", "b", "b.h");
  CreateTmpFile(
      "#pragma once\n"
      "#include \"../b/b.h\"\n",
      bh);

  std::vector<string> args{"/usr/bin/gcc", "-c", ac};
  std::set<string> expected{bh};

  std::set<string> files = RunCppIncludeProcessor(ac, args);
  EXPECT_EQ(expected, files);
}

TEST_F(CppIncludeProcessorTest, undef_content) {
  const string& inc = file::JoinPath(".", "inc.h");
  CreateTmpFile(
      "#define THIS FILE\n"
      "#include THIS\n"
      "#undef THIS\n",
      inc);

  const string& ac = file::JoinPath(".", "a.c");
  CreateTmpFile(
      "#define FILE \"a.h\"\n"
      "#include \"inc.h\"\n"
      "#undef FILE\n"
      "#define FILE \"b.h\"\n"
      "#include \"inc.h\"\n",
      ac);

  const string& ah = file::JoinPath(".", "a.h");
  const string& bh = file::JoinPath(".", "b.h");
  CreateTmpFile("", ah);
  CreateTmpFile("", bh);

  std::vector<string> args{"/usr/bin/gcc", "-c", ac};
  std::set<string> expected{inc, ah, bh};

  std::set<string> files = RunCppIncludeProcessor(ac, args);
  EXPECT_EQ(expected, files);
}

}  // namespace devtools_goma
