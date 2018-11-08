// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// You can specify the clang binary for this test by
//
// GOMATEST_CLANG_PATH=/somewhere/bin/clang ./include_processor_unittest

#include "absl/strings/str_split.h"
#include "compiler_flags.h"
#include "compiler_flags_parser.h"
#include "compiler_info.h"
#include "compiler_info_cache.h"
#include "compiler_info_state.h"
#include "compiler_type_specific_collection.h"
#include "cpp_include_processor.h"
#include "cpp_include_processor_unittest_helper.h"
#include "file_stat_cache.h"
#include "glog/stl_logging.h"
#include "gtest/gtest.h"
#include "include_cache.h"
#include "include_file_finder.h"
#include "list_dir_cache.h"
#include "path.h"
#include "path_resolver.h"
#include "subprocess.h"
#include "unittest_util.h"

namespace devtools_goma {

class CppIncludeProcessorPosixTest : public testing::Test {
 public:
  CppIncludeProcessorPosixTest() {
    // Implementation Note: In ctor, we cannot use ASSERT_TRUE.
    // So, we delegate clang_path_ initialization to another function.
    InitClangPath();
  }

  void InitClangPath() {
    clang_path_ = GetClangPath();
    ASSERT_TRUE(!clang_path_.empty());
  }

  void SetUp() override {
    tmpdir_util_ = absl::make_unique<TmpdirUtil>("include_processor_unittest");
    tmpdir_util_->SetCwd("");
    InstallReadCommandOutputFunc(ReadCommandOutputByPopen);
    IncludeFileFinder::Init(true);
    ListDirCache::Init(4096);
  }

  void TearDown() override { ListDirCache::Quit(); }

  std::unique_ptr<CompilerInfoData> CreateCompilerInfoWithArgs(
      const CompilerFlags& flags,
      const string& bare_gcc,
      const std::vector<string>& compiler_envs) {
    return CompilerTypeSpecificCollection()
        .Get(flags.type())
        ->BuildCompilerInfoData(flags, bare_gcc, compiler_envs);
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

  void RunTestInternal(const string& bare_gcc,
                       const string& include_file,
                       const std::vector<string>& additional_args,
                       const std::set<string>& allowed_extra_files) {
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

    CompareFiles(expected_files, actual_files, allowed_extra_files);
  }

  void RunTest(const string& bare_gcc,
               const string& include_file,
               const std::vector<string>& additional_args) {
    RunTestInternal(bare_gcc, include_file, additional_args,
                    std::set<string>());
  }

  void RunTestAllowExtra(const string& bare_gcc,
                         const string& include_file,
                         const std::vector<string>& additional_args,
                         const std::set<string>& allowed_extra_files) {
    RunTestInternal(bare_gcc, include_file, additional_args,
                    allowed_extra_files);
  }

  string CreateTmpFile(const string& content, const string& name) {
    tmpdir_util_->CreateTmpFile(name, content);
    return tmpdir_util_->FullPath(name);
  }

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

 protected:
  static void SetUpTestCase() {
    // Does not load cache from file.
    CompilerInfoCache::Init("", "", absl::Hours(1));
    IncludeCache::Init(5, true);
  };

  static void TearDownTestCase() {
    IncludeCache::Quit();
    CompilerInfoCache::Quit();
  };

  std::unique_ptr<TmpdirUtil> tmpdir_util_;
  std::vector<string> env_;
  string clang_path_;
};

TEST_F(CppIncludeProcessorPosixTest, stdio) {
  std::vector<string> args;
  RunTest("/usr/bin/gcc", CreateTmpFile("#include <stdio.h>", "foo.c"), args);
}

TEST_F(CppIncludeProcessorPosixTest, iostream) {
  std::vector<string> args;
  RunTest("/usr/bin/g++", CreateTmpFile("#include <iostream>", "foo.cc"), args);
}

TEST_F(CppIncludeProcessorPosixTest, iostream_with_gcc) {
  std::vector<string> args;
  RunTest("/usr/bin/gcc", CreateTmpFile("#include <iostream>", "foo.cpp"),
          args);
}

TEST_F(CppIncludeProcessorPosixTest, macro) {
  std::vector<string> args;
  RunTest("/usr/bin/g++",
          CreateTmpFile("#define ios <iostream>\n#include ios\n", "foo.cc"),
          args);
}

TEST_F(CppIncludeProcessorPosixTest, commandline_macro) {
  std::vector<string> args;
  args.push_back("-Dios=<iostream>");
  RunTest("/usr/bin/g++", CreateTmpFile("#include ios\n", "foo.cc"), args);
}

TEST_F(CppIncludeProcessorPosixTest, commandline_macro_undef) {
  std::vector<string> args;
  // Undefnie predefined macro.
  args.push_back("-U__ELF__");
  args.push_back("-D__ELF__=<stdio.h>");
  RunTest("/usr/bin/g++", CreateTmpFile("#include __ELF__\n", "foo.cc"), args);
}

TEST_F(CppIncludeProcessorPosixTest, unclosed_macro) {
  std::vector<string> args;
  RunTest("/usr/bin/g++", CreateTmpFile("#define wrong_macro \"foo", "foo.cc"),
          args);
}

TEST_F(CppIncludeProcessorPosixTest, opt_include_in_system_path) {
  std::vector<string> args;
  args.push_back("-include");
  args.push_back("stdio.h");
  RunTest("/usr/bin/gcc", CreateTmpFile("", "foo.cc"), args);
}

// See b/74321868
TEST_F(CppIncludeProcessorPosixTest, opt_include_evil) {
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

TEST_F(CppIncludeProcessorPosixTest, include_twice) {
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

TEST_F(CppIncludeProcessorPosixTest, stdcpredef) {
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

TEST_F(CppIncludeProcessorPosixTest, ffreestanding) {
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
TEST_F(CppIncludeProcessorPosixTest, fnohosted) {
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

TEST_F(CppIncludeProcessorPosixTest, recursive) {
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

TEST_F(CppIncludeProcessorPosixTest, opt_include_gch) {
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

TEST_F(CppIncludeProcessorPosixTest, gch) {
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

TEST_F(CppIncludeProcessorPosixTest, dir_cache) {
  std::vector<string> args;
  args.push_back("-I" + tmpdir_util_->tmpdir());

  CreateTmpFile("", "bar.h");
  // The cache will be constructed here.
  RunTest("/usr/bin/g++", CreateTmpFile("#include <bar.h>\n", "foo.cc"), args);

  // As another file is added, the cache must be discarded.
  CreateTmpFile("", "baz.h");
  RunTest("/usr/bin/g++", CreateTmpFile("#include <baz.h>\n", "foo.cc"), args);
}

TEST_F(CppIncludeProcessorPosixTest, I_system_path) {
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

TEST_F(CppIncludeProcessorPosixTest, iquote) {
  std::vector<string> args{
      "-iquote", "include",
  };
  CreateTmpFile("", "include/foo.h");
  RunTest("/usr/bin/g++", CreateTmpFile("#include \"foo.h\"\n", "foo.cc"),
          args);
}

TEST_F(CppIncludeProcessorPosixTest, hmap) {
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

TEST_F(CppIncludeProcessorPosixTest, hmap_with_dir) {
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

TEST_F(CppIncludeProcessorPosixTest, cpp_and_isystem) {
  std::vector<string> args;
  CreateTmpFile("", "typeinfo");
  args.push_back("-isystem");
  args.push_back(tmpdir_util_->tmpdir());
  RunTest("/usr/bin/g++", CreateTmpFile("#include <typeinfo>\n", "foo.cc"),
          args);
}

TEST_F(CppIncludeProcessorPosixTest, funclike_macro1) {
  std::vector<string> args;
  RunTest("/usr/bin/g++",
          CreateTmpFile("#define s(x) #x\n"
                        "#include s(stdio.h)\n",
                        "foo.cc"),
          args);
}

TEST_F(CppIncludeProcessorPosixTest, funclike_macro2) {
  std::vector<string> args;
  RunTest("/usr/bin/gcc",
          CreateTmpFile("#define X(name) <std##name.h>\n"
                        "#include X(io)\n",
                        "foo.c"),
          args);
}

TEST_F(CppIncludeProcessorPosixTest, funclike_macro3) {
  std::vector<string> args;
  RunTest("/usr/bin/gcc",
          CreateTmpFile("#define XY \"stdio.h\"\n"
                        "#define C(x, y) x ## y\n"
                        "#include C(X, Y)\n",
                        "foo.c"),
          args);
}

TEST_F(CppIncludeProcessorPosixTest, include_nested_macros) {
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
TEST_F(CppIncludeProcessorPosixTest, commandline_funclike_macro) {
  std::vector<string> args;
  args.push_back("-DS(a)=#a");
  RunTest("/usr/bin/g++", CreateTmpFile("#include S(iostream)\n", "foo.cc"),
          args);
}

TEST_F(CppIncludeProcessorPosixTest, escaped_newline) {
  std::vector<string> args;
  RunTest("/usr/bin/g++",
          CreateTmpFile("#include <io\\\nstream>\n"
                        "#inc\\\nlude <string>\n",
                        "foo.cc"),
          args);
}

TEST_F(CppIncludeProcessorPosixTest, macro_false_recursion) {
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

TEST_F(CppIncludeProcessorPosixTest, macro_nested_args) {
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

TEST_F(CppIncludeProcessorPosixTest, macro_varargs) {
  CreateTmpFile("#include <vector>\n", "c");
  std::vector<string> args;
  RunTest("/usr/bin/g++",
          CreateTmpFile("#define X(a, b, c, ...) c\n"
                        "#include X(\"a\", \"b\", \"c\", \"d\", \"e\")\n",
                        "foo.cc"),
          args);
}

TEST_F(CppIncludeProcessorPosixTest, macro_with_defined) {
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

TEST_F(CppIncludeProcessorPosixTest, include_in_comment) {
  std::vector<string> args;
  RunTest("/usr/bin/g++",
          CreateTmpFile("#include <string> /* \n"
                        "#include <iostream> */\n",
                        "foo.cc"),
          args);
}

TEST_F(CppIncludeProcessorPosixTest, include_in_linecomment) {
  std::vector<string> args;
  RunTest("/usr/bin/g++",
          CreateTmpFile("#include <string> // comment \\\n"
                        "#include <iostream>\n",
                        "foo.cc"),
          args);
}

TEST_F(CppIncludeProcessorPosixTest, include_with_predefined) {
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

TEST_F(CppIncludeProcessorPosixTest, include_with_cpp_predefined) {
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

TEST_F(CppIncludeProcessorPosixTest, include_with_pragma_once) {
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

TEST_F(CppIncludeProcessorPosixTest, include_with_ifdefs) {
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

TEST_F(CppIncludeProcessorPosixTest, include_with_if_elif_else) {
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

TEST_F(CppIncludeProcessorPosixTest, include_with_cond_expr_1) {
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

TEST_F(CppIncludeProcessorPosixTest, include_nested) {
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

TEST_F(CppIncludeProcessorPosixTest, include_with_macro) {
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

TEST_F(CppIncludeProcessorPosixTest, include_twice_with_macro) {
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

TEST_F(CppIncludeProcessorPosixTest, include_time_h) {
  std::vector<string> args;
  RunTest("/usr/bin/gcc",
          CreateTmpFile("#include <sys/types.h>\n"
                        "#include <time.h>\n",
                        "time.c"),
          args);
}

TEST_F(CppIncludeProcessorPosixTest, base_file) {
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

TEST_F(CppIncludeProcessorPosixTest, has_include) {
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

TEST_F(CppIncludeProcessorPosixTest, has_include_relative) {
  // Use clang here, because gcc in trusty does not support __has_include.
  const std::string& clang_path = GetClangPath();
  std::unique_ptr<CompilerFlags> flags(CompilerFlagsParser::MustNew(
      std::vector<string>{clang_path, "-c", "foo.cc", "-I."},
      tmpdir_util_->tmpdir()));
  ScopedCompilerInfoState cis(
      GetCompilerInfoFromCacheOrCreate(*flags, clang_path, env_));

  CreateTmpFile(
      "#if __has_include(<a.h>)\n"
      "#define A\n"
      "#endif\n",
      "foo.cc");
  CreateTmpFile("", "a.h");

  CppIncludeProcessor processor;
  std::set<string> files;
  FileStatCache file_stat_cache;
  ASSERT_TRUE(processor.GetIncludeFiles(
      "foo.cc", tmpdir_util_->tmpdir(), *flags,
      ToCxxCompilerInfo(cis.get()->info()), &files, &file_stat_cache));
  EXPECT_TRUE(files.count("./a.h"));
}

TEST_F(CppIncludeProcessorPosixTest, has_include_next) {
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

TEST_F(CppIncludeProcessorPosixTest, has_feature) {
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

TEST_F(CppIncludeProcessorPosixTest, has_extension) {
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

TEST_F(CppIncludeProcessorPosixTest, has_cpp_attribute) {
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

TEST_F(CppIncludeProcessorPosixTest, has_declspec_attribute) {
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

TEST_F(CppIncludeProcessorPosixTest, has_builtin) {
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

TEST_F(CppIncludeProcessorPosixTest, dont_include_directory) {
  CreateTmpDir("iostream");

  std::vector<string> args;
  args.push_back("-I" + tmpdir_util_->tmpdir());
  RunTest("/usr/bin/gcc", CreateTmpFile("#include <iostream>", "foo.cpp"),
          args);
}

TEST_F(CppIncludeProcessorPosixTest, fmodules) {
  const std::vector<string> args{"-fmodules"};
  const string a_cc = CreateTmpFile("#include \"a.h\"", "a.cc");

  CreateTmpFile("#define A 100\n", "a.h");
  CreateTmpFile(
      "module foo {\n"
      "  header \"a.h\"\n"
      "}\n",
      "module.modulemap");

  RunTest(clang_path_, a_cc, args);
}

TEST_F(CppIncludeProcessorPosixTest, fmodule_map_file) {
  // -fmodule-map-file is considered as input.
  // Needs to pass -fmodule-name=foo otherwise module-map-file won't be used.
  const string a_cc = CreateTmpFile("#include \"a.h\"", "a.cc");
  CreateTmpFile("#define A 100\n", "a.h");
  CreateTmpFile(
      "module foo {\n"
      "  header \"a.h\"\n"
      "}\n",
      "tmp.modulemap");

  const std::vector<string> args{"-fmodules", "-fmodule-map-file=tmp.modulemap",
                                 "-fmodule-name=foo"};
  RunTest(clang_path_, a_cc, args);
}

TEST_F(CppIncludeProcessorPosixTest, fmodule_map_file_extern) {
  const string a_cc = CreateTmpFile("", "a.cc");
  CreateTmpFile(R"(
module foo {
  extern module bar "bar/bar.modulemap"
})",
                "foo.modulemap");
  CreateTmpFile(R"(
module bar {
  extern module baz "baz/baz.modulemap"
})",
                "bar/bar.modulemap");
  CreateTmpFile(R"(
module baz {
  header "a.h"
})",
                "bar/baz/baz.modulemap");

  const std::vector<string> args{"-fmodules", "-fmodule-map-file=foo.modulemap",
                                 "-fmodule-name=foo"};
  RunTest(clang_path_, a_cc, args);
}

TEST_F(CppIncludeProcessorPosixTest, fmodule_map_file_extern_dup) {
  // "foo.modulemap" includes "bar.modulemap" and "baz.modulemap".
  // Both "bar.modulemap" and "baz.modulemap" includes "qux.modulemap".

  const string a_cc = CreateTmpFile("", "a.cc");
  CreateTmpFile(R"(
module foo {
  extern module bar "bar.modulemap"
  extern module baz "baz.modulemap"
})",
                "foo.modulemap");
  CreateTmpFile(R"(
module bar {
  extern module qux "qux.modulemap"
})",
                "bar.modulemap");
  CreateTmpFile(R"(
module baz {
  extern module qux "qux.modulemap"
})",
                "baz.modulemap");
  CreateTmpFile(R"(
module qux {
  header "a.h"
})",
                "qux.modulemap");

  const std::vector<string> args{"-fmodules", "-fmodule-map-file=foo.modulemap",
                                 "-fmodule-name=foo"};
  RunTest(clang_path_, a_cc, args);
}

TEST_F(CppIncludeProcessorPosixTest, fmodule_file) {
  const string a_cc = CreateTmpFile("#include \"a.h\"", "a.cc");
  const string a_h = CreateTmpFile("#define A 100\n", "a.h");
  CreateTmpFile(
      "module foo {\n"
      "  header \"a.h\"\n"
      "}\n",
      "module.modulemap");

  // First, make "module.pcm".
  {
    const std::vector<string> args{clang_path_,
                                   "-x",
                                   "c++",
                                   "-fmodules",
                                   "-fmodule-name=foo",
                                   "-Xclang",
                                   "-emit-module",
                                   "-Xclang",
                                   "-fmodule-map-file-home-is-cwd",
                                   "-c",
                                   "module.modulemap",
                                   "-o",
                                   "module.pcm"};
    const std::vector<string> envs{
        "LC_ALL=C",
    };
    int exit_status = -1;
    (void)ReadCommandOutputByPopen(args[0], args, envs, tmpdir_util_->tmpdir(),
                                   STDOUT_ONLY, &exit_status);
    ASSERT_EQ(0, exit_status);
  }

  // Set it as module-file.
  const std::vector<string> args{"-fmodules", "-fmodule-file=module.pcm"};

  // TODO: If there is a precompiled module, clang does not read "a.h",
  // since it's already compiled into module.pcm.
  // If we'd like to run this correctly, we have to parse "module.pcm"
  // binary file.
  //
  // "a.h" will be extra.
  const std::set<string> allowed_extra_files{a_h};
  RunTestAllowExtra(clang_path_, a_cc, args, allowed_extra_files);
}

#ifdef __MACH__
TEST_F(CppIncludeProcessorPosixTest, curdir_framework) {
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

TEST_F(CppIncludeProcessorPosixTest, sub_framework) {
  // b/23128924
  std::vector<string> args;
  RunTest("/usr/bin/gcc",
          CreateTmpFile("#include <Accelerate/Accelerate.h>", "foo.cc"), args);
}
#endif

}  // namespace devtools_goma
