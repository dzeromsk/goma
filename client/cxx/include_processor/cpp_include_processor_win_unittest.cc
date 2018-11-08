// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <set>
#include <string>
#include <vector>

#include "absl/memory/memory.h"
#include "absl/strings/str_split.h"
#include "compiler_flags_parser.h"
#include "compiler_info.h"
#include "compiler_info_cache.h"
#include "compiler_info_state.h"
#include "compiler_type_specific_collection.h"
#include "cpp_include_processor.h"
#include "cpp_include_processor_unittest_helper.h"
#include "file_helper.h"
#include "glog/stl_logging.h"
#include "gtest/gtest.h"
#include "include_cache.h"
#include "include_file_finder.h"
#include "list_dir_cache.h"
#include "mypath.h"
#include "path.h"
#include "path_resolver.h"
#include "subprocess.h"
#include "unittest_util.h"
#include "util.h"

using std::string;

namespace {

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

}  // anonymous namespace

namespace devtools_goma {

class CppIncludeProcessorWinTest : public testing::Test {
 public:
  CppIncludeProcessorWinTest() {
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
  }

  void SetUp() override {
    tmpdir_util_ = absl::make_unique<TmpdirUtil>("include_processor_unittest");
    tmpdir_util_->SetCwd("");
    cl_wrapper_path_ = CreateTmpFile(kClWrapperBat, "clwrapper.bat");

    InstallReadCommandOutputFunc(ReadCommandOutputByRedirector);
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
    CompareFiles(expected_files, actual_files, std::set<string>());
  }

  string CreateTmpFile(const string& content, const string& name) {
    tmpdir_util_->CreateTmpFile(name, content);
    return tmpdir_util_->FullPath(name);
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
  string cl_wrapper_path_;
  string top_dir_;
};

// TODO: Add more CppIncludeProcessorWinTest for VC
TEST_F(CppIncludeProcessorWinTest, stdio) {
  std::vector<string> args;
  RunClTest(CreateTmpFile("#include <stdio.h>", "foo.c"), args);
}

TEST_F(CppIncludeProcessorWinTest, iostream) {
  std::vector<string> args;
  RunClTest(CreateTmpFile("#include <iostream>", "foo.cpp"), args);
}

TEST_F(CppIncludeProcessorWinTest, commandline_define) {
  std::vector<string> args;
  args.push_back("/DDEBUG");
  RunClTest(CreateTmpFile("#ifdef DEBUG\r\n#include <iostream>\r\n#endif\r\n",
                          "foo.cpp"),
            args);
}


TEST_F(CppIncludeProcessorWinTest, AtFile) {
  string at_file = CreateTmpFile("/DDEBUG", "at_file.rsp");
  at_file = "@" + at_file;
  std::vector<string> args;
  args.push_back(at_file.c_str());
  RunClTest(CreateTmpFile("#ifdef DEBUG\r\n#include <iostream>\r\n#endif\r\n",
                          "foo.cpp"),
            args);
}

TEST_F(CppIncludeProcessorWinTest, dont_include_directory) {
  const string& iostream_dir =
      file::JoinPath(tmpdir_util_->tmpdir(), "iostream");
  CreateDirectoryA(iostream_dir.c_str(), nullptr);

  std::vector<string> args;
  args.push_back("/I" + tmpdir_util_->tmpdir());
  RunClTest(CreateTmpFile("#include <iostream>", "foo.cpp"), args);
}

}  // namespace devtools_goma
