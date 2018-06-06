// Copyright 2016 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "compile_task.h"

#include <memory>
#include <set>
#include <string>

#include "absl/strings/ascii.h"
#include "compiler_flags.h"
#include "gtest/gtest.h"
#include "path.h"

namespace devtools_goma {

namespace {

const char kRootDir[] =
#ifdef _WIN32
    "C:\\";
#else
    "/";
#endif

}  // anonymous namespace

class CompileTaskTest : public testing::Test {
 public:
  static bool IsLocalCompilerPathValid(
      const string& trace_id,
      const ExecReq& req,
      const CompilerFlags* flags) {
    return CompileTask::IsLocalCompilerPathValid(trace_id, req, flags);
  }

  static void RemoveDuplicateFiles(const std::string& cwd,
                                   std::set<std::string>* filenames) {
    CompileTask::RemoveDuplicateFiles(cwd, filenames);
  }
};

TEST_F(CompileTaskTest, IsLocalCompilerPathValidWithEmptyLocalCompilerPath) {
  ExecReq req;
  req.mutable_command_spec()->set_name("gcc");
  req.mutable_command_spec()->set_version("1.2.3");
  req.mutable_command_spec()->set_target("x86_64-linux-gnu");
  req.mutable_command_spec()->set_binary_hash("deadbeaf");
  req.set_cwd("/tmp");

  std::vector<string> args = {"gcc", "-c", "dummy.cc"};
  std::unique_ptr<CompilerFlags> flag(CompilerFlags::New(args, "."));

  EXPECT_TRUE(CompileTaskTest::IsLocalCompilerPathValid(
      "dummy", req, flag.get()));
}

TEST_F(CompileTaskTest, IsLocalCompilerPathValidWithSameCommandSpec) {
  ExecReq req;
  req.mutable_command_spec()->set_name("gcc");
  req.mutable_command_spec()->set_version("1.2.3");
  req.mutable_command_spec()->set_target("x86_64-linux-gnu");
  req.mutable_command_spec()->set_binary_hash("deadbeaf");
  req.set_cwd("/tmp");
  req.mutable_command_spec()->set_local_compiler_path("/usr/bin/gcc");

  std::vector<string> args = {"gcc", "-c", "dummy.cc"};
  std::unique_ptr<CompilerFlags> flag(CompilerFlags::New(args, "."));

  EXPECT_TRUE(CompileTaskTest::IsLocalCompilerPathValid(
      "dummy", req, flag.get()));
}

TEST_F(CompileTaskTest, IsLocalCompilerPathValidWithUnknownName) {
  ExecReq req;
  req.mutable_command_spec()->set_name("gcc");
  req.mutable_command_spec()->set_version("1.2.3");
  req.mutable_command_spec()->set_target("x86_64-linux-gnu");
  req.mutable_command_spec()->set_binary_hash("deadbeaf");
  req.set_cwd("/tmp");
  req.mutable_command_spec()->set_local_compiler_path("/usr/bin/id");

  std::vector<string> args = {"gcc", "-c", "dummy.cc"};
  std::unique_ptr<CompilerFlags> flag(CompilerFlags::New(args, "."));

  EXPECT_FALSE(CompileTaskTest::IsLocalCompilerPathValid(
      "dummy", req, flag.get()));
}

TEST_F(CompileTaskTest, IsLocalCompilerPathValidWithCommandSpecMismatch) {
  ExecReq req;
  req.mutable_command_spec()->set_name("clang");
  req.mutable_command_spec()->set_version("1.2.3");
  req.mutable_command_spec()->set_target("x86_64-linux-gnu");
  req.mutable_command_spec()->set_binary_hash("deadbeaf");
  req.set_cwd("/tmp");
  req.mutable_command_spec()->set_local_compiler_path("/usr/bin/gcc");

  std::vector<string> args = {"gcc", "-c", "dummy.cc"};
  std::unique_ptr<CompilerFlags> flag(CompilerFlags::New(args, "."));

  EXPECT_FALSE(CompileTaskTest::IsLocalCompilerPathValid(
      "dummy", req, flag.get()));
}

TEST_F(CompileTaskTest, IsLocalCompilerPathValidWithArgsMismatch) {
  ExecReq req;
  req.mutable_command_spec()->set_name("gcc");
  req.mutable_command_spec()->set_version("1.2.3");
  req.mutable_command_spec()->set_target("x86_64-linux-gnu");
  req.mutable_command_spec()->set_binary_hash("deadbeaf");
  req.set_cwd("/tmp");
  req.mutable_command_spec()->set_local_compiler_path("/usr/bin/gcc");

  std::vector<string> args = {"clang", "-c", "dummy.cc"};
  std::unique_ptr<CompilerFlags> flag(CompilerFlags::New(args, "."));

  EXPECT_FALSE(CompileTaskTest::IsLocalCompilerPathValid(
      "dummy", req, flag.get()));
}

TEST_F(CompileTaskTest, IsLocalCompilerPathValidWithSameCommandSpecClExe) {
  ExecReq req;
  req.mutable_command_spec()->set_name("cl.exe");
  req.mutable_command_spec()->set_version("1.2.3");
  req.mutable_command_spec()->set_target("x86");
  req.mutable_command_spec()->set_binary_hash("deadbeaf");
  req.set_cwd("/tmp");
  req.mutable_command_spec()->set_local_compiler_path("c:\\dummy\\cl.exe");

  std::vector<string> args = {"c:\\dummy\\cl.exe", "/c", "dummy.cc"};
  std::unique_ptr<CompilerFlags> flag(CompilerFlags::New(args, "."));

  EXPECT_TRUE(CompileTaskTest::IsLocalCompilerPathValid(
      "dummy", req, flag.get()));
}

TEST_F(CompileTaskTest, IsLocalCompilerPathValidWithOmittingExtension) {
  ExecReq req;
  req.mutable_command_spec()->set_name("cl.exe");
  req.mutable_command_spec()->set_version("1.2.3");
  req.mutable_command_spec()->set_target("x86");
  req.mutable_command_spec()->set_binary_hash("deadbeaf");
  req.set_cwd("/tmp");
  req.mutable_command_spec()->set_local_compiler_path("c:\\dummy\\cl");

  std::vector<string> args = {"cl", "/c", "dummy.cc"};
  std::unique_ptr<CompilerFlags> flag(CompilerFlags::New(args, "."));

  EXPECT_TRUE(CompileTaskTest::IsLocalCompilerPathValid(
      "dummy", req, flag.get()));
}

TEST_F(CompileTaskTest, IsLocalCompilerPathValidWithUnknownNameClExe) {
  ExecReq req;
  req.mutable_command_spec()->set_name("cl.exe");
  req.mutable_command_spec()->set_version("1.2.3");
  req.mutable_command_spec()->set_target("x86");
  req.mutable_command_spec()->set_binary_hash("deadbeaf");
  req.set_cwd("/tmp");
  req.mutable_command_spec()->set_local_compiler_path(
      "c:\\dummy\\shutdown.exe");

  std::vector<string> args = {"c:\\dummy\\cl.exe", "/c", "dummy.cc"};
  std::unique_ptr<CompilerFlags> flag(CompilerFlags::New(args, "."));

  EXPECT_FALSE(CompileTaskTest::IsLocalCompilerPathValid(
      "dummy", req, flag.get()));
}

TEST_F(CompileTaskTest, IsLocalCompilerPathValidWithCommandSpecMismatchClExe) {
  ExecReq req;
  req.mutable_command_spec()->set_name("clang-cl");
  req.mutable_command_spec()->set_version("1.2.3");
  req.mutable_command_spec()->set_target("x86");
  req.mutable_command_spec()->set_binary_hash("deadbeaf");
  req.set_cwd("/tmp");
  req.mutable_command_spec()->set_local_compiler_path("c:\\dummy\\cl.exe");

  std::vector<string> args = {"c:\\dummy\\cl.exe", "/c", "dummy.cc"};
  std::unique_ptr<CompilerFlags> flag(CompilerFlags::New(args, "."));

  EXPECT_FALSE(CompileTaskTest::IsLocalCompilerPathValid(
      "dummy", req, flag.get()));
}

TEST_F(CompileTaskTest, IsLocalCompilerPathValidWithArgsMismatchClExe) {
  ExecReq req;
  req.mutable_command_spec()->set_name("cl.exe");
  req.mutable_command_spec()->set_version("1.2.3");
  req.mutable_command_spec()->set_target("x86");
  req.mutable_command_spec()->set_binary_hash("deadbeaf");
  req.set_cwd("/tmp");
  req.mutable_command_spec()->set_local_compiler_path("c:\\dummy\\cl.exe");

  std::vector<string> args = {"c:\\dummy\\clang-cl.exe", "/c", "dummy.cc"};
  std::unique_ptr<CompilerFlags> flag(CompilerFlags::New(args, "."));

  EXPECT_FALSE(CompileTaskTest::IsLocalCompilerPathValid(
      "dummy", req, flag.get()));
}
// TODO: add other combinations if necessary.

TEST_F(CompileTaskTest, RemoveDuplicateFiles) {
  {
    // different filepath
    std::set<std::string> filenames {
      file::JoinPath(kRootDir, "foo", "bar.cc"),
      file::JoinPath(kRootDir, "foo", "baz.cc")
    };
    RemoveDuplicateFiles("", &filenames);

    std::set<std::string> expected {
      file::JoinPath(kRootDir, "foo", "bar.cc"),
      file::JoinPath(kRootDir, "foo", "baz.cc")
    };
    EXPECT_EQ(filenames, expected);
  }

  {
    // different filepath if case is not same.
    std::set<std::string> filenames {
      file::JoinPath(kRootDir, "Foo"),
      file::JoinPath(absl::AsciiStrToLower(kRootDir), "fOO"),
    };
    RemoveDuplicateFiles("", &filenames);

    std::set<std::string> expected {
      file::JoinPath(kRootDir, "Foo"),
      file::JoinPath(absl::AsciiStrToLower(kRootDir), "fOO"),
    };
    EXPECT_EQ(filenames, expected);
  }

  {
    // same filepath when JoinPathRespectAbsolute
    std::set<std::string> filenames {
      "bar.cc", file::JoinPath(kRootDir, "foo", "bar.cc")};
    RemoveDuplicateFiles(file::JoinPath(kRootDir, "foo"), &filenames);

    std::set<std::string> expected {"bar.cc"};;
    EXPECT_EQ(filenames, expected);
  }

  {
    // same filepath when JoinPathRespectAbsolute with ..
    std::set<std::string> filenames {
      file::JoinPath("..", "bar.cc"),
      file::JoinPath(kRootDir, "foo", "baz", "..", "bar.cc")
    };
    RemoveDuplicateFiles(file::JoinPath(kRootDir, "foo", "baz"),
                         &filenames);

    std::set<std::string> expected {file::JoinPath("..", "bar.cc")};
    EXPECT_EQ(filenames, expected);
  }

  {
    // different filepath when JoinPathRespectAbsolute
    std::set<std::string> filenames {
      file::JoinPath(kRootDir, "foo", "baz", "..", "bar.cc"),
      file::JoinPath(kRootDir, "foo", "bar.cc")
    };
    RemoveDuplicateFiles("", &filenames);

    std::set<std::string> expected {
      file::JoinPath(kRootDir, "foo", "baz", "..", "bar.cc"),
      file::JoinPath(kRootDir, "foo", "bar.cc")
    };
    EXPECT_EQ(filenames, expected);
  }

  {
    // different filepath when JoinPathRespectAbsolute
    std::set<std::string> filenames {
      file::JoinPath("baz", "..", "bar.cc"),
      file::JoinPath(kRootDir, "foo", "bar.cc")
    };
    RemoveDuplicateFiles(file::JoinPath(kRootDir, "foo"), &filenames);

    std::set<std::string> expected {
      file::JoinPath("baz", "..", "bar.cc"),
      file::JoinPath(kRootDir, "foo", "bar.cc")
    };
    EXPECT_EQ(filenames, expected);
  }

  {
    // different filepath when JoinPathRespectAbsolute
    std::set<std::string> filenames {
      file::JoinPath("..", "bar.cc"),
      file::JoinPath(kRootDir, "foo", "bar.cc")
    };
    RemoveDuplicateFiles(file::JoinPath(kRootDir, "foo", "baz"),
                         &filenames);

    std::set<std::string> expected {
      file::JoinPath("..", "bar.cc"),
      file::JoinPath(kRootDir, "foo", "bar.cc")
    };
    EXPECT_EQ(filenames, expected);
  }

  {
    // We want to test path X and path Y where
    // 1. X < Y (alphabetical order)
    // 2. JoinPathRespectAbsolute(cwd, X) == JoinPathRespectAbsolutePath(cwd, Y)
    // 3. len(X) > len(Y).
    //
    // Due to (1) and (2), If X is relative path, Y should be absolute path
    // or vice versa. Since we don't resolve path and due to (3),
    // Y must be relative path, and X must be absolute path.
    //
    // So, relative path should start with a character which is larger than
    // 'C' (on Win) or '/' (on non Win).
    std::set<std::string> filenames {
      file::JoinPath(kRootDir, "a", "a", "bar.cc"),
      file::JoinPath("a", "bar.cc"),
    };
    ASSERT_EQ(file::JoinPath(kRootDir, "a", "a", "bar.cc"), *filenames.begin());
    RemoveDuplicateFiles(file::JoinPath(kRootDir, "a"), &filenames);

    std::set<std::string> expected {
      file::JoinPath("a", "bar.cc"),
    };
    EXPECT_EQ(filenames, expected);
  }
}

}  // namespace devtools_goma
