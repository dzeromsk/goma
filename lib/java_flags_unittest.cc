// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "java_flags.h"

#include "absl/strings/str_cat.h"
#include "compiler_flags_parser.h"
#include "file.h"
#include "file_helper.h"
#include "filesystem.h"
#include "glog/logging.h"
#include "gtest/gtest.h"
#include "path.h"
#include "path_resolver.h"
using google::GetExistingTempDirectories;
using std::string;

#ifdef _WIN32
# include "config_win.h"
#endif  // _WIN32

namespace devtools_goma {

class JavacFlagsTest : public testing::Test {
 public:
  void SetUp() override {
    std::vector<string> tmp_dirs;
    GetExistingTempDirectories(&tmp_dirs);
    ASSERT_GT(tmp_dirs.size(), 0);

#ifndef _WIN32
    string pid = std::to_string(getpid());
#else
    string pid = std::to_string(GetCurrentProcessId());
#endif
    tmp_dir_ = file::JoinPath(
        tmp_dirs[0], absl::StrCat("compiler_flags_unittest_", pid));

    ASSERT_TRUE(file::CreateDir(tmp_dir_, file::CreationMode(0777)).ok());
  }

  void TearDown() override {
    util::Status status = file::RecursivelyDelete(tmp_dir_, file::Defaults());
    if (!status.ok()) {
      LOG(ERROR) << "failed to delete: " << tmp_dir_;
    }
  }

 protected:
  string tmp_dir_;
};

TEST_F(JavacFlagsTest, Basic) {
  std::vector<string> args;
  args.push_back("javac");
  args.push_back("-J-Xmx512M");
  args.push_back("-target");
  args.push_back("1.5");
  args.push_back("-d");
  args.push_back("dst");
  args.push_back("-s");
  args.push_back("src");
  args.push_back("-cp");
  args.push_back("/tmp:a.jar:b.jar");
  args.push_back("-classpath");
  args.push_back("c.jar");
  args.push_back("-bootclasspath");
  args.push_back("boot1.jar:boot2.jar");
  args.push_back("Hello.java");
  args.push_back("World.java");
  std::unique_ptr<CompilerFlags> flags(CompilerFlagsParser::MustNew(args, "."));
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ(CompilerFlagType::Javac, flags->type());
  JavacFlags* javac_flags = static_cast<JavacFlags*>(flags.get());
  EXPECT_EQ("javac", flags->compiler_name());
  ASSERT_EQ(2U, flags->input_filenames().size());
  EXPECT_EQ("Hello.java", flags->input_filenames()[0]);
  EXPECT_EQ("World.java", flags->input_filenames()[1]);
  std::vector<string> expected_jar_files = {
    "boot1.jar",
    "boot2.jar",
    "a.jar",
    "b.jar",
    "c.jar",
  };
  EXPECT_EQ(expected_jar_files, javac_flags->jar_files());
  EXPECT_EQ(0U, flags->output_files().size());
  ASSERT_EQ(2U, flags->output_dirs().size());
  EXPECT_EQ("dst", flags->output_dirs()[0]);
  EXPECT_EQ("src", flags->output_dirs()[1]);
}

TEST_F(JavacFlagsTest, AtFile) {
  std::vector<string> args;
  args.push_back("javac");
  const string& at_file = file::JoinPath(tmp_dir_, "at_file");
  args.push_back("@" + at_file);

  // The at-file doesn't exist.
  std::unique_ptr<CompilerFlags> flags(CompilerFlagsParser::MustNew(args, "."));
  EXPECT_FALSE(flags->is_successful());

  ASSERT_TRUE(
      WriteStringToFile("Hello.java World.java\r\n\t-d dst\r\n-s src",
                        at_file));
  flags = CompilerFlagsParser::MustNew(args, ".");
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ(CompilerFlagType::Javac, flags->type());
  EXPECT_EQ("javac", flags->compiler_name());
  EXPECT_EQ(7U, flags->expanded_args().size());
  EXPECT_EQ("javac", flags->expanded_args()[0]);
  EXPECT_EQ("Hello.java", flags->expanded_args()[1]);
  EXPECT_EQ("World.java", flags->expanded_args()[2]);
  EXPECT_EQ("-d", flags->expanded_args()[3]);
  EXPECT_EQ("dst", flags->expanded_args()[4]);
  EXPECT_EQ("-s", flags->expanded_args()[5]);
  EXPECT_EQ("src", flags->expanded_args()[6]);
  ASSERT_EQ(2U, flags->input_filenames().size());
  EXPECT_EQ("Hello.java", flags->input_filenames()[0]);
  EXPECT_EQ("World.java", flags->input_filenames()[1]);
  ASSERT_EQ(1U, flags->optional_input_filenames().size());
  EXPECT_EQ(PathResolver::PlatformConvert(at_file),
            flags->optional_input_filenames()[0]);
  EXPECT_EQ(0U, flags->output_files().size());
  ASSERT_EQ(2U, flags->output_dirs().size());
  EXPECT_EQ("dst", flags->output_dirs()[0]);
  EXPECT_EQ("src", flags->output_dirs()[1]);
}

TEST_F(JavacFlagsTest, NoDestination) {
  std::vector<string> args;
  args.push_back("javac");
  args.push_back("Hello.java");
  args.push_back("World.java");
  std::unique_ptr<CompilerFlags> flags(CompilerFlagsParser::MustNew(args, "."));
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ(CompilerFlagType::Javac, flags->type());
  EXPECT_EQ("javac", flags->compiler_name());
  ASSERT_EQ(2U, flags->input_filenames().size());
  EXPECT_EQ("Hello.java", flags->input_filenames()[0]);
  EXPECT_EQ("World.java", flags->input_filenames()[1]);
  ASSERT_EQ(2U, flags->output_files().size());
  EXPECT_EQ("Hello.class", flags->output_files()[0]);
  EXPECT_EQ("World.class", flags->output_files()[1]);
}

TEST_F(JavacFlagsTest, Processor) {
  const std::vector<string> args {
    "javac", "-processorpath", "classes.jar",
    "-processor", "dagger.internal.codegen.ComponentProcessor",
    "All.java"
  };
  const std::vector<string> expected_processors {
    "dagger.internal.codegen.ComponentProcessor",
  };

  std::unique_ptr<CompilerFlags> flags(CompilerFlagsParser::MustNew(args, "."));
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ(CompilerFlagType::Javac, flags->type());

  JavacFlags* javac_flags = static_cast<JavacFlags*>(flags.get());
  EXPECT_EQ(expected_processors, javac_flags->processors());
}

TEST_F(JavacFlagsTest, MultipleProcessorArgs) {
  const std::vector<string> args {
    "javac", "-processorpath", "classes.jar",
    "-processor", "dagger.internal.codegen.ComponentProcessor",
    "-processor", "com.google.auto.value.processor.AutoValueProcessor",
    "All.java"
  };
  const std::vector<string> expected_processors {
    "dagger.internal.codegen.ComponentProcessor",
    "com.google.auto.value.processor.AutoValueProcessor",
  };

  std::unique_ptr<CompilerFlags> flags(CompilerFlagsParser::MustNew(args, "."));
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ(CompilerFlagType::Javac, flags->type());

  JavacFlags* javac_flags = static_cast<JavacFlags*>(flags.get());
  EXPECT_EQ(expected_processors, javac_flags->processors());
}

TEST_F(JavacFlagsTest, MultipleProcessorsInArg) {
  const std::vector<string> args {
    "javac", "-processorpath", "classes.jar",
    "-processor",
    "dagger.internal.codegen.ComponentProcessor,"
        "com.google.auto.value.processor.AutoValueProcessor",
    "All.java"
  };
  const std::vector<string> expected_processors {
    "dagger.internal.codegen.ComponentProcessor",
    "com.google.auto.value.processor.AutoValueProcessor",
  };

  std::unique_ptr<CompilerFlags> flags(CompilerFlagsParser::MustNew(args, "."));
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ(CompilerFlagType::Javac, flags->type());

  JavacFlags* javac_flags = static_cast<JavacFlags*>(flags.get());
  EXPECT_EQ(expected_processors, javac_flags->processors());
}

TEST_F(JavacFlagsTest, ParseJavaClassPaths) {
  std::vector<string> input = {
    "a.jar:b.zip:c.class",
    "d.jar",
    "e",
  };
  std::vector<string> output;
  ParseJavaClassPaths(input, &output);
  std::vector<string> expected = {
    "a.jar", "b.zip", "d.jar",
  };
  EXPECT_EQ(expected, output);
}

TEST_F(JavacFlagsTest, UnknownFlags) {
  const std::vector<string> args {
    "javac", "-unknown1", "--unknown2",
    "All.java"
  };
  const std::vector<string> expected {
    "-unknown1", "--unknown2",
  };

  std::unique_ptr<CompilerFlags> flags(CompilerFlagsParser::MustNew(args, "."));
  EXPECT_EQ(expected, flags->unknown_flags());
}

class JavaFlagsTest : public testing::Test {
 public:
  void SetUp() override {
    std::vector<string> tmp_dirs;
    GetExistingTempDirectories(&tmp_dirs);
    CHECK_GT(tmp_dirs.size(), 0);

#ifndef _WIN32
    string pid = std::to_string(getpid());
#else
    string pid = std::to_string(GetCurrentProcessId());
#endif
    tmp_dir_ = file::JoinPath(
        tmp_dirs[0], absl::StrCat("compiler_flags_unittest_", pid));

    ASSERT_TRUE(file::CreateDir(tmp_dir_, file::CreationMode(0777)).ok());
  }

  void TearDown() override {
    util::Status status = file::RecursivelyDelete(tmp_dir_, file::Defaults());
    if (!status.ok()) {
      LOG(ERROR) << "failed to delete: " << tmp_dir_;
    }
  }

 protected:
  string tmp_dir_;
};

TEST_F(JavaFlagsTest, Basic) {
  std::vector<string> args = {
    "prebuilts/jdk/jdk8/linux-x86/bin/java",
    "-Djdk.internal.lambda.dumpProxyClasses="
        "JAVA_LIBRARIES/apache-xml_intermediates/desugar_dumped_classes",
    "-jar",
    "out/host/linux-x86/framework/desugar.jar",
    "--classpath_entry",
    "JAVA_LIBRARIES/core-libart_intermediates/classes-header.jar",
    "--classpath_entry",
    "JAVA_LIBRARIES/core-oj_intermediates/classes-header.jar",
    "--min_sdk_version",
    "10000",
    "--allow_empty_bootclasspath",
    "-i",
    "JAVA_LIBRARIES/apache-xml_intermediates/classes.jar",
    "-o",
    "JAVA_LIBRARIES/apache-xml_intermediates/classes-desugar.jar.tmp",
    "-cp","/tmp:a.jar:b.jar",
    "-classpath", "c.jar",
  };
  std::unique_ptr<CompilerFlags> flags(CompilerFlagsParser::MustNew(args, "."));
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ(CompilerFlagType::Java, flags->type());
  EXPECT_EQ("java", flags->compiler_name());
  ASSERT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("out/host/linux-x86/framework/desugar.jar",
            flags->input_filenames()[0]);
  EXPECT_EQ(0U, flags->output_files().size());

  JavaFlags* java_flags = static_cast<JavaFlags*>(flags.get());
  std::vector<string> expected_jar_files = {
    "a.jar",
    "b.jar",
    "c.jar",
  };
  EXPECT_EQ(expected_jar_files, java_flags->jar_files());
}

}  // namespace devtools_goma
