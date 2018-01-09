// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include <limits.h>

#include <memory>
#include <set>
#include <string>
#include <vector>

#include <glog/logging.h>
#include <gtest/gtest.h>

#include "file.h"
#include "ioutil.h"
#include "jar_parser.h"
#include "mypath.h"
#include "path.h"
#include "unittest_util.h"
#include "util.h"

using std::string;

namespace devtools_goma {

// Note: Do not assume JDK is installed on Windows.  For Windows build, we use
//       prebuilt JAR files for testing.

class JarParserTest : public testing::Test {
 public:
  void SetUp() override {
    tmpdir_util_.reset(new TmpdirUtil("jar_parser_unittest"));
    tmpdir_util_->SetCwd("");
  }

 protected:
  string CopyArchiveIntoTestDir(const string& test_name,
                                const string& archive) {
    // This module is build\Release\jar_parser_unittest.exe (msvs) or
    // out\Release\jar_parser_unittest.exe (ninja).
    const string parent_dir = file::JoinPath(GetMyDirectory(), "..");
    const string top_dir = file::JoinPath(parent_dir, "..");
    const string test_dir = file::JoinPath(top_dir, "test");
    const string source_file = file::JoinPath(test_dir, test_name + ".jar");
    const string output_file = tmpdir_util_->FullPath(archive);
    CHECK(File::Copy(source_file.c_str(), output_file.c_str(), false));
    return output_file;
  }

  std::unique_ptr<TmpdirUtil> tmpdir_util_;
};

TEST_F(JarParserTest, Basic) {
  std::vector<string> input_jar_files;

  const string& jar = CopyArchiveIntoTestDir("Basic", "foo.jar");
  input_jar_files.push_back(jar);

  JarParser parser;
  std::set<string> jar_files;
  parser.GetJarFiles(input_jar_files, tmpdir_util_->tmpdir(), &jar_files);
  ASSERT_EQ(1U, jar_files.size());
  EXPECT_EQ(jar, *jar_files.begin());
}

TEST_F(JarParserTest, ReadManifest) {
  std::vector<string> input_jar_files;

  std::vector<string> files;
  files.push_back("bar.class");
  const string& foo_jar = CopyArchiveIntoTestDir("Basic", "foo.jar");
  const string& bar_jar = CopyArchiveIntoTestDir("ReadManifest", "bar.jar");

  // Dup should be ignored.
  input_jar_files.push_back(bar_jar);
  input_jar_files.push_back(bar_jar);

  JarParser parser;
  std::set<string> jar_files_set;
  parser.GetJarFiles(input_jar_files, tmpdir_util_->tmpdir(), &jar_files_set);
  std::vector<string> jar_files(jar_files_set.begin(), jar_files_set.end());
  ASSERT_EQ(2U, jar_files.size());
  EXPECT_EQ(bar_jar, jar_files[0]);
  EXPECT_EQ(foo_jar, jar_files[1]);
}

}  // namespace devtools_goma
