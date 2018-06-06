// Copyright 2013 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include <list>
#include <memory>
#include <string>
#include <vector>

#include <glog/stl_logging.h>
#include <gtest/gtest.h>

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "compiler_specific.h"
#include "cpp_directive_parser.h"
#include "cpp_parser.h"
#include "cpp_tokenizer.h"
#include "timestamp.h"
#include "include_guard_detector.h"

namespace devtools_goma {

class CppIncludeObserver : public CppParser::IncludeObserver {
 public:
  explicit CppIncludeObserver(CppParser* parser)
      : parser_(parser) {
  }
  ~CppIncludeObserver() override {}
  bool HandleInclude(
      const string& path,
      const string& current_directory ALLOW_UNUSED,
      const string& current_filepath ALLOW_UNUSED,
      char quote_char ALLOW_UNUSED,
      int include_dir_index ALLOW_UNUSED) override {
    if (parser_->IsProcessedFile(path, include_dir_index)) {
      ++skipped_[path];
      return true;
    }
    std::map<string, string>::const_iterator p = includes_.find(path);
    if (p == includes_.end()) {
      return false;
    }

    ++included_[path];

    SharedCppDirectives directives =
        CppDirectiveParser().ParseFromString(p->second);
    string include_guard_ident = IncludeGuardDetector::Detect(*directives);

    parser_->AddFileInput(IncludeItem(directives, include_guard_ident),
                          p->first,
                          ".",
                          CppParser::kCurrentDirIncludeDirIndex);
    return true;
  }

  bool HasInclude(
      const string& path,
      const string& current_directory ALLOW_UNUSED,
      const string& current_filepath ALLOW_UNUSED,
      char quote_char ALLOW_UNUSED,
      int include_dir_index ALLOW_UNUSED) override {
    return includes_.find(path) != includes_.end();
  }

  void SetInclude(const string& filepath, const string& content) {
    includes_.insert(make_pair(filepath, content));
  }

  int SkipCount(const string& filepath) const {
    const auto& it = skipped_.find(filepath);
    if (it == skipped_.end())
      return 0;
    return it->second;
  }

  int IncludedCount(const string& filepath) const {
    const auto& it = included_.find(filepath);
    if (it == included_.end())
      return 0;
    return it->second;
  }

 private:
  CppParser* parser_;
  std::map<string, string> includes_;
  std::map<string, int> skipped_;
  std::map<string, int> included_;
  DISALLOW_COPY_AND_ASSIGN(CppIncludeObserver);
};

class CppErrorObserver : public CppParser::ErrorObserver {
 public:
  CppErrorObserver() {}
  ~CppErrorObserver() override {}
  void HandleError(const string& error) override {
    errors_.push_back(error);
  }
  const std::vector<string>& errors() const {
    return errors_;
  }

 private:
  std::vector<string> errors_;
  DISALLOW_COPY_AND_ASSIGN(CppErrorObserver);
};

TEST(CppParserTest, DontCrashWithEmptyInclude) {
  CppParser cpp_parser;
  cpp_parser.AddStringInput("#include\n", "(string)");
  CppErrorObserver err_observer;
  cpp_parser.set_error_observer(&err_observer);
  cpp_parser.ProcessDirectives();
  ASSERT_EQ(1U, err_observer.errors().size());
  EXPECT_EQ("CppParser((string):2) "
            "#include expects \"filename\" or <filename>",
            err_observer.errors()[0]);
}

TEST(CppParserTest, DontCrashWithEmptyHasInclude) {
  CppParser cpp_parser;
  ASSERT_TRUE(cpp_parser.EnablePredefinedMacro("__has_include", false));
  cpp_parser.AddStringInput(
      "#if __has_include()\n#endif\n"
      "#if __has_include(\n#endif\n"
      "#if __has_include\n",
      "(string)");
  CppErrorObserver err_observer;
  cpp_parser.set_error_observer(&err_observer);
  cpp_parser.ProcessDirectives();
  ASSERT_EQ(3U, err_observer.errors().size());
  EXPECT_TRUE(
      absl::StartsWith(err_observer.errors()[0], "CppParser((string):2) "));
  EXPECT_TRUE(
      absl::StartsWith(err_observer.errors()[1], "CppParser((string):4) "));
  EXPECT_TRUE(
      absl::StartsWith(err_observer.errors()[2], "CppParser((string):6) "));
}

TEST(CppParserTest, HasFeatureResultValue) {
  std::unique_ptr<CompilerInfoData> info_data(new CompilerInfoData);
  info_data->add_supported_predefined_macros("__has_feature");
  info_data->add_supported_predefined_macros("__has_extension");
  info_data->add_supported_predefined_macros("__has_attribute");
  info_data->add_supported_predefined_macros("__has_cpp_attribute");
  info_data->add_supported_predefined_macros("__has_declspec_attribute");
  info_data->add_supported_predefined_macros("__has_builtin");
  CompilerInfoData::MacroValue* m;
  m = info_data->add_has_feature();
  m->set_key("feature");
  m->set_value(2);
  m = info_data->add_has_extension();
  m->set_key("extension");
  m->set_value(3);
  m = info_data->add_has_attribute();
  m->set_key("attribute");
  m->set_value(4);
  m = info_data->add_has_cpp_attribute();
  m->set_key("cpp_attribute");
  m->set_value(5);
  m = info_data->add_has_declspec_attribute();
  m->set_key("declspec_attribute");
  m->set_value(6);
  m = info_data->add_has_builtin();
  m->set_key("builtin");
  m->set_value(7);

  CompilerInfo info(std::move(info_data));

  CppParser cpp_parser;
  cpp_parser.SetCompilerInfo(&info);

  cpp_parser.AddStringInput(
    "#if __has_feature(feature) == 2\n"
    "# define FEATURE_FEATURE_OK\n"
    "#endif\n"
    "#if __has_feature( feature ) == 2\n"
    "# define FEATURE_FEATURE_SPACE_OK\n"
    "#endif\n"
    "#if __has_feature(extension) == 0\n"
    "# define FEATURE_EXTENSION_OK\n"
    "#endif\n"
    "#if __has_feature(attribute) == 0\n"
    "# define FEATURE_ATTRIBUTE_OK\n"
    "#endif\n"
    "#if __has_feature(cpp_attribute) == 0\n"
    "# define FEATURE_CPP_ATTRIBUTE_OK\n"
    "#endif\n"
    "#if __has_feature(declspec_attribute) == 0\n"
    "# define FEATURE_DECLSPEC_ATTRIBUTE_OK\n"
    "#endif\n"
    "#if __has_feature(builtin) == 0\n"
    "# define FEATURE_BUILTIN_OK\n"
    "#endif\n"
    "#if __has_extension(feature) == 0\n"
    "# define EXTENSION_FEATURE_OK\n"
    "#endif\n"
    "#if __has_extension(extension) == 3\n"
    "# define EXTENSION_EXTENSION_OK\n"
    "#endif\n"
    "#if __has_extension( extension ) == 3\n"
    "# define EXTENSION_EXTENSION_SPACE_OK\n"
    "#endif\n"
    "#if __has_extension(attribute) == 0\n"
    "# define EXTENSION_ATTRIBUTE_OK\n"
    "#endif\n"
    "#if __has_extension(cpp_attribute) == 0\n"
    "# define EXTENSION_CPP_ATTRIBUTE_OK\n"
    "#endif\n"
    "#if __has_extension(declspec_attribute) == 0\n"
    "# define EXTENSION_DECLSPEC_ATTRIBUTE_OK\n"
    "#endif\n"
    "#if __has_extension(builtin) == 0\n"
    "# define EXTENSION_BUILTIN_OK\n"
    "#endif\n"
    "#if __has_attribute(feature) == 0\n"
    "# define ATTRIBUTE_FEATURE_OK\n"
    "#endif\n"
    "#if __has_attribute(extension) == 0\n"
    "# define ATTRIBUTE_EXTENSION_OK\n"
    "#endif\n"
    "#if __has_attribute(attribute) == 4\n"
    "# define ATTRIBUTE_ATTRIBUTE_OK\n"
    "#endif\n"
    "#if __has_attribute( attribute ) == 4\n"
    "# define ATTRIBUTE_ATTRIBUTE_SPACE_OK\n"
    "#endif\n"
    "#if __has_attribute(cpp_attribute) == 0\n"
    "# define ATTRIBUTE_CPP_ATTRIBUTE_OK\n"
    "#endif\n"
    "#if __has_attribute(declspec_attribute) == 0\n"
    "# define ATTRIBUTE_DECLSPEC_ATTRIBUTE_OK\n"
    "#endif\n"
    "#if __has_attribute(builtin) == 0\n"
    "# define ATTRIBUTE_BUILTIN_OK\n"
    "#endif\n"
    "#if __has_cpp_attribute(feature) == 0\n"
    "# define CPP_ATTRIBUTE_FEATURE_OK\n"
    "#endif\n"
    "#if __has_cpp_attribute(extension) == 0\n"
    "# define CPP_ATTRIBUTE_EXTENSION_OK\n"
    "#endif\n"
    "#if __has_cpp_attribute(attribute) == 0\n"
    "# define CPP_ATTRIBUTE_ATTRIBUTE_OK\n"
    "#endif\n"
    "#if __has_cpp_attribute(cpp_attribute) == 5\n"
    "# define CPP_ATTRIBUTE_CPP_ATTRIBUTE_OK\n"
    "#endif\n"
    "#if __has_cpp_attribute( cpp_attribute ) == 5\n"
    "# define CPP_ATTRIBUTE_CPP_ATTRIBUTE_SPACE_OK\n"
    "#endif\n"
    "#if __has_cpp_attribute(declspec_attribute) == 0\n"
    "# define CPP_ATTRIBUTE_DECLSPEC_ATTRIBUTE_OK\n"
    "#endif\n"
    "#if __has_cpp_attribute(builtin) == 0\n"
    "# define CPP_ATTRIBUTE_BUILTIN_OK\n"
    "#endif\n"
    "#if __has_declspec_attribute(feature) == 0\n"
    "# define DECLSPEC_ATTRIBUTE_FEATURE_OK\n"
    "#endif\n"
    "#if __has_declspec_attribute(extension) == 0\n"
    "# define DECLSPEC_ATTRIBUTE_EXTENSION_OK\n"
    "#endif\n"
    "#if __has_declspec_attribute(attribute) == 0\n"
    "# define DECLSPEC_ATTRIBUTE_ATTRIBUTE_OK\n"
    "#endif\n"
    "#if __has_declspec_attribute(cpp_attribute) == 0\n"
    "# define DECLSPEC_ATTRIBUTE_CPP_ATTRIBUTE_OK\n"
    "#endif\n"
    "#if __has_declspec_attribute(declspec_attribute) == 6\n"
    "# define DECLSPEC_ATTRIBUTE_DECLSPEC_ATTRIBUTE_OK\n"
    "#endif\n"
    "#if __has_declspec_attribute( declspec_attribute ) == 6\n"
    "# define DECLSPEC_ATTRIBUTE_DECLSPEC_ATTRIBUTE_SPACE_OK\n"
    "#endif\n"
    "#if __has_declspec_attribute(builtin) == 0\n"
    "# define DECLSPEC_ATTRIBUTE_BUILTIN_OK\n"
    "#endif\n"
    "#if __has_builtin(feature) == 0\n"
    "# define BUILTIN_FEATURE_OK\n"
    "#endif\n"
    "#if __has_builtin(extension) == 0\n"
    "# define BUILTIN_EXTENSION_OK\n"
    "#endif\n"
    "#if __has_builtin(attribute) == 0\n"
    "# define BUILTIN_ATTRIBUTE_OK\n"
    "#endif\n"
    "#if __has_builtin(cpp_attribute) == 0\n"
    "# define BUILTIN_CPP_ATTRIBUTE_OK\n"
    "#endif\n"
    "#if __has_builtin(declspec_attribute) == 0\n"
    "# define BUILTIN_DECLSPEC_ATTRIBUTE_OK\n"
    "#endif\n"
    "#if __has_builtin(builtin) == 7\n"
    "# define BUILTIN_BUILTIN_OK\n"
    "#endif\n"
    "#if __has_builtin( builtin ) == 7\n"
    "# define BUILTIN_BUILTIN_SPACE_OK\n"
    "#endif\n", "(string)");
  cpp_parser.ProcessDirectives();

  EXPECT_TRUE(cpp_parser.IsMacroDefined("FEATURE_FEATURE_OK"));
  EXPECT_TRUE(cpp_parser.IsMacroDefined("FEATURE_FEATURE_SPACE_OK"));
  EXPECT_TRUE(cpp_parser.IsMacroDefined("FEATURE_EXTENSION_OK"));
  EXPECT_TRUE(cpp_parser.IsMacroDefined("FEATURE_ATTRIBUTE_OK"));
  EXPECT_TRUE(cpp_parser.IsMacroDefined("FEATURE_CPP_ATTRIBUTE_OK"));
  EXPECT_TRUE(cpp_parser.IsMacroDefined("FEATURE_DECLSPEC_ATTRIBUTE_OK"));
  EXPECT_TRUE(cpp_parser.IsMacroDefined("FEATURE_BUILTIN_OK"));
  EXPECT_TRUE(cpp_parser.IsMacroDefined("EXTENSION_FEATURE_OK"));
  EXPECT_TRUE(cpp_parser.IsMacroDefined("EXTENSION_EXTENSION_OK"));
  EXPECT_TRUE(cpp_parser.IsMacroDefined("EXTENSION_EXTENSION_SPACE_OK"));
  EXPECT_TRUE(cpp_parser.IsMacroDefined("EXTENSION_ATTRIBUTE_OK"));
  EXPECT_TRUE(cpp_parser.IsMacroDefined("EXTENSION_CPP_ATTRIBUTE_OK"));
  EXPECT_TRUE(cpp_parser.IsMacroDefined("EXTENSION_DECLSPEC_ATTRIBUTE_OK"));
  EXPECT_TRUE(cpp_parser.IsMacroDefined("EXTENSION_BUILTIN_OK"));
  EXPECT_TRUE(cpp_parser.IsMacroDefined("ATTRIBUTE_FEATURE_OK"));
  EXPECT_TRUE(cpp_parser.IsMacroDefined("ATTRIBUTE_EXTENSION_OK"));
  EXPECT_TRUE(cpp_parser.IsMacroDefined("ATTRIBUTE_ATTRIBUTE_OK"));
  EXPECT_TRUE(cpp_parser.IsMacroDefined("ATTRIBUTE_ATTRIBUTE_SPACE_OK"));
  EXPECT_TRUE(cpp_parser.IsMacroDefined("ATTRIBUTE_CPP_ATTRIBUTE_OK"));
  EXPECT_TRUE(cpp_parser.IsMacroDefined("ATTRIBUTE_DECLSPEC_ATTRIBUTE_OK"));
  EXPECT_TRUE(cpp_parser.IsMacroDefined("ATTRIBUTE_BUILTIN_OK"));
  EXPECT_TRUE(cpp_parser.IsMacroDefined("CPP_ATTRIBUTE_FEATURE_OK"));
  EXPECT_TRUE(cpp_parser.IsMacroDefined("CPP_ATTRIBUTE_EXTENSION_OK"));
  EXPECT_TRUE(cpp_parser.IsMacroDefined("CPP_ATTRIBUTE_ATTRIBUTE_OK"));
  EXPECT_TRUE(cpp_parser.IsMacroDefined("CPP_ATTRIBUTE_CPP_ATTRIBUTE_OK"));
  EXPECT_TRUE(cpp_parser.IsMacroDefined(
      "CPP_ATTRIBUTE_CPP_ATTRIBUTE_SPACE_OK"));
  EXPECT_TRUE(cpp_parser.IsMacroDefined("CPP_ATTRIBUTE_DECLSPEC_ATTRIBUTE_OK"));
  EXPECT_TRUE(cpp_parser.IsMacroDefined("CPP_ATTRIBUTE_BUILTIN_OK"));
  EXPECT_TRUE(cpp_parser.IsMacroDefined("DECLSPEC_ATTRIBUTE_FEATURE_OK"));
  EXPECT_TRUE(cpp_parser.IsMacroDefined("DECLSPEC_ATTRIBUTE_EXTENSION_OK"));
  EXPECT_TRUE(cpp_parser.IsMacroDefined("DECLSPEC_ATTRIBUTE_ATTRIBUTE_OK"));
  EXPECT_TRUE(cpp_parser.IsMacroDefined("DECLSPEC_ATTRIBUTE_CPP_ATTRIBUTE_OK"));
  EXPECT_TRUE(cpp_parser.IsMacroDefined(
                  "DECLSPEC_ATTRIBUTE_DECLSPEC_ATTRIBUTE_OK"));
  EXPECT_TRUE(cpp_parser.IsMacroDefined(
                  "DECLSPEC_ATTRIBUTE_DECLSPEC_ATTRIBUTE_SPACE_OK"));
  EXPECT_TRUE(cpp_parser.IsMacroDefined("DECLSPEC_ATTRIBUTE_BUILTIN_OK"));
  EXPECT_TRUE(cpp_parser.IsMacroDefined("BUILTIN_FEATURE_OK"));
  EXPECT_TRUE(cpp_parser.IsMacroDefined("BUILTIN_EXTENSION_OK"));
  EXPECT_TRUE(cpp_parser.IsMacroDefined("BUILTIN_ATTRIBUTE_OK"));
  EXPECT_TRUE(cpp_parser.IsMacroDefined("BUILTIN_CPP_ATTRIBUTE_OK"));
  EXPECT_TRUE(cpp_parser.IsMacroDefined("BUILTIN_DECLSPEC_ATTRIBUTE_OK"));
  EXPECT_TRUE(cpp_parser.IsMacroDefined("BUILTIN_BUILTIN_OK"));
  EXPECT_TRUE(cpp_parser.IsMacroDefined("BUILTIN_BUILTIN_SPACE_OK"));
}

TEST(CppParserTest, ClangExtendedCheckMacro) {
  std::unique_ptr<CompilerInfoData> info_data(new CompilerInfoData);
  info_data->add_supported_predefined_macros("__has_cpp_attribute");

  CompilerInfoData::MacroValue* m;
  m = info_data->add_has_cpp_attribute();
  m->set_key("clang::fallthrough");
  m->set_value(1);

  CompilerInfo info(std::move(info_data));

  CppParser cpp_parser;
  cpp_parser.SetCompilerInfo(&info);

  // clang::fallthrough must be allowed.
  cpp_parser.AddStringInput("#if __has_cpp_attribute(clang::fallthrough)\n"
                            "# define FOO\n"
                            "#endif\n"
                            "#if __has_cpp_attribute(clang@@fallthrough)\n"
                            "# define BAR\n"
                            "#endif\n"
                            "#if __has_cpp_attribute(clang::fallthrough)\n"
                            "# define BAZ\n"
                            "#endif\n",
                            "(string)");

  CppErrorObserver err_observer;
  cpp_parser.set_error_observer(&err_observer);
  cpp_parser.ProcessDirectives();

  EXPECT_TRUE(cpp_parser.IsMacroDefined("FOO"));
  EXPECT_FALSE(cpp_parser.IsMacroDefined("BAR"));
  EXPECT_TRUE(cpp_parser.IsMacroDefined("BAZ"));

  // TODO: I feel this is a change detection test...
  ASSERT_EQ(1U, err_observer.errors().size()) << err_observer.errors();
  EXPECT_EQ("CppParser((string):5) "
            "__has_cpp_attribute expects an identifier",
            err_observer.errors()[0]);
}

TEST(CppParserTest, DontCrashWithEmptyTokenInCheckMacro) {
  std::unique_ptr<CompilerInfoData> info_data(new CompilerInfoData);
  info_data->add_supported_predefined_macros("__has_feature");
  info_data->add_supported_predefined_macros("__has_extension");
  info_data->add_supported_predefined_macros("__has_attribute");
  info_data->add_supported_predefined_macros("__has_cpp_attribute");
  info_data->add_supported_predefined_macros("__has_declspec_attribute");
  info_data->add_supported_predefined_macros("__has_builtin");
  CompilerInfoData::MacroValue* m;
  m = info_data->add_has_feature();
  m->set_key("foo");
  m->set_value(1);
  m = info_data->add_has_extension();
  m->set_key("foo");
  m->set_value(1);
  m = info_data->add_has_attribute();
  m->set_key("foo");
  m->set_value(1);
  m = info_data->add_has_cpp_attribute();
  m->set_key("foo");
  m->set_value(1);
  m = info_data->add_has_declspec_attribute();
  m->set_key("foo");
  m->set_value(1);
  m = info_data->add_has_builtin();
  m->set_key("foo");
  m->set_value(1);

  CompilerInfo info(std::move(info_data));

  CppParser cpp_parser;
  cpp_parser.SetCompilerInfo(&info);

  cpp_parser.AddStringInput("#if __has_feature()\n#endif\n"
                            "#if __has_feature(\n#endif\n"
                            "#if __has_feature\n#endif\n"
                            "#if __has_extension()\n#endif\n"
                            "#if __has_extension(\n#endif\n"
                            "#if __has_extension\n#endif\n"
                            "#if __has_attribute()\n#endif\n"
                            "#if __has_attribute(\n#endif\n"
                            "#if __has_attribute\n#endif\n"
                            "#if __has_cpp_attribute()\n#endif\n"
                            "#if __has_cpp_attribute(\n#endif\n"
                            "#if __has_cpp_attribute\n#endif\n"
                            "#if __has_declspec_attribute()\n#endif\n"
                            "#if __has_declspec_attribute(\n#endif\n"
                            "#if __has_declspec_attribute\n#endif\n"
                            "#if __has_builtin()\n#endif\n"
                            "#if __has_builtin(\n#endif\n"
                            "#if __has_builtin\n#endif\n",
                            "(string)");
  CppErrorObserver err_observer;
  cpp_parser.set_error_observer(&err_observer);
  cpp_parser.ProcessDirectives();
  ASSERT_EQ(18U, err_observer.errors().size()) << err_observer.errors();
  for (int i = 0; i < 18; ++i) {
    string error_prefix(absl::StrCat("CppParser((string):", (i + 1) * 2, ") "));
    EXPECT_TRUE(absl::StartsWith(err_observer.errors()[i], error_prefix));
  }
}

TEST(CppParserTest, ExpandMacro) {
  CppParser cpp_parser;
  cpp_parser.AddStringInput("#define M() 1\n"
                            "#if M()\n"
                            "#endif\n"
                            "#if M(x)\n"
                            "#endif\n"
                            "#define M1(x) x\n"
                            "#if M1()\n"
                            "#endif\n"
                            "#if M1(1)\n"
                            "#endif\n"
                            "#define M2(x,y) x+y\n"
                            "#if M2(1,1)\n"
                            "#endif\n"
                            "#if M2(,1)\n"
                            "#endif\n"
                            "#if M2(1,)\n"
                            "#endif\n"
                            "#if M2()\n"
                            "#endif\n"
                            "#if M2(1)\n"
                            "#endif\n"
                            "#if M2(1,,1)\n"
                            "#endif\n",
                            "(string)");
  CppErrorObserver err_observer;
  cpp_parser.set_error_observer(&err_observer);
  cpp_parser.ProcessDirectives();
  ASSERT_EQ(4U, err_observer.errors().size())
      << absl::StrJoin(err_observer.errors(), "\n");
  // TODO: line number is #endif line that just after #if that
  // error happened?
  EXPECT_EQ("CppParser((string):5) "  // M(x)
            "macro argument number mismatching with the parameter list",
            err_observer.errors()[0]);
  EXPECT_EQ("CppParser((string):19) "  // M2()
            "macro argument number mismatching with the parameter list",
            err_observer.errors()[1]);
  EXPECT_EQ("CppParser((string):21) "  // M2(1)
            "macro argument number mismatching with the parameter list",
            err_observer.errors()[2]);
  EXPECT_EQ("CppParser((string):23) "  // M2(1,,1)
            "macro argument number mismatching with the parameter list",
            err_observer.errors()[3]);
}

TEST(CppParserTest, IncludeMoreThanOnce) {
  CppParser cpp_parser;
  CppIncludeObserver include_observer(&cpp_parser);
  include_observer.SetInclude("foo.h",
                              "#ifdef hoge\n"
                              "#endif\n");
  cpp_parser.set_include_observer(&include_observer);
  cpp_parser.AddStringInput("#define hoge\n"
                            "#include <foo.h>\n"
                            "#undef hoge\n"
                            "#include <foo.h>\n",
                            "foo.cc");
  cpp_parser.ProcessDirectives();
  EXPECT_EQ(2, cpp_parser.total_files());
  EXPECT_EQ(0, cpp_parser.skipped_files());
}

TEST(CppParserTest, ImportOnlyOnce) {
  CppParser cpp_parser;
  CppIncludeObserver include_observer(&cpp_parser);
  include_observer.SetInclude("foo.h",
                              "#ifdef hoge\n"
                              "#endif\n");
  cpp_parser.set_include_observer(&include_observer);
  cpp_parser.AddStringInput("#define hoge\n"
                            "#import <foo.h>\n"
                            "#undef hoge\n"
                            "#import <foo.h>\n",
                            "foo.cc");
  cpp_parser.ProcessDirectives();
  EXPECT_EQ(2, cpp_parser.total_files());
  EXPECT_EQ(1, cpp_parser.skipped_files());
}

TEST(CppParserTest, BoolShouldBeTreatedAsBoolOnCplusplus) {
  CppParser cpp_parser;
  cpp_parser.set_is_cplusplus(true);
  cpp_parser.AddStringInput("#if true\n"
                            "#define foo\n"
                            "#endif\n"
                            "#if false\n"
                            "#define bar\n"
                            "#endif\n",
                            "baz.cc");
  cpp_parser.ProcessDirectives();
  EXPECT_TRUE(cpp_parser.IsMacroDefined("foo"));
  EXPECT_FALSE(cpp_parser.IsMacroDefined("bar"));
}

TEST(CppParserTest, BoolShouldNotBeTreatedAsBoolOnNonCplusplus) {
  CppParser cpp_parser;
  cpp_parser.AddStringInput("#if true\n"
                            "#define foo\n"
                            "#endif\n"
                            "#if false\n"
                            "#define bar\n"
                            "#endif\n",
                            "baz.cc");
  cpp_parser.ProcessDirectives();
  EXPECT_FALSE(cpp_parser.IsMacroDefined("foo"));
  EXPECT_FALSE(cpp_parser.IsMacroDefined("bar"));
}

TEST(CppParserTest, BoolShouldNotBeTreatedAsDefined) {
  CppParser cpp_parser;
  cpp_parser.set_is_cplusplus(true);
  cpp_parser.AddStringInput("#if true\n"
                            "#define foo\n"
                            "#endif\n"
                            "#if defined(true)\n"
                            "#define bar\n"
                            "#endif\n"
                            "#if false\n"
                            "#define baz\n"
                            "#endif\n"
                            "#if defined(false)\n"
                            "#define qux\n"
                            "#endif\n",
                            "baz.cc");
  cpp_parser.ProcessDirectives();
  EXPECT_TRUE(cpp_parser.IsMacroDefined("foo"));
  EXPECT_FALSE(cpp_parser.IsMacroDefined("bar"));
  EXPECT_FALSE(cpp_parser.IsMacroDefined("baz"));
  EXPECT_FALSE(cpp_parser.IsMacroDefined("qux"));
}

TEST(CppParserTest, BoolShouldBeOverriddenByMacroInTrueToTrueCase) {
  CppParser cpp_parser;
  cpp_parser.set_is_cplusplus(true);
  cpp_parser.AddStringInput("#define true true\n"
                            "#if true\n"
                            "#define foo\n"
                            "#endif\n"
                            "#if defined(true)\n"
                            "#define bar\n"
                            "#endif\n",
                            "baz.cc");
  cpp_parser.ProcessDirectives();
  EXPECT_TRUE(cpp_parser.IsMacroDefined("foo"));
  EXPECT_TRUE(cpp_parser.IsMacroDefined("bar"));
}

TEST(CppParserTest, BoolShouldBeOverriddenByMacroInTrueToFalseCase) {
  CppParser cpp_parser;
  cpp_parser.set_is_cplusplus(true);
  cpp_parser.AddStringInput("#define true false\n"
                            "#if true\n"
                            "#define foo\n"
                            "#endif\n"
                            "#if defined(true)\n"
                            "#define bar\n"
                            "#endif\n",
                            "baz.cc");
  cpp_parser.ProcessDirectives();
  EXPECT_FALSE(cpp_parser.IsMacroDefined("foo"));
  EXPECT_TRUE(cpp_parser.IsMacroDefined("bar"));
}

TEST(CppParserTest, BoolShouldBeOverriddenByMacroInFalseToTrueCase) {
  CppParser cpp_parser;
  cpp_parser.set_is_cplusplus(true);
  cpp_parser.AddStringInput("#define false true\n"
                            "#if false\n"
                            "#define foo\n"
                            "#endif\n"
                            "#if defined(false)\n"
                            "#define bar\n"
                            "#endif\n",
                            "baz.cc");
  cpp_parser.ProcessDirectives();
  EXPECT_TRUE(cpp_parser.IsMacroDefined("foo"));
  EXPECT_TRUE(cpp_parser.IsMacroDefined("bar"));
}

TEST(CppParserTest, BoolShouldBeOverriddenByMacroInFalseToFalseCase) {
  CppParser cpp_parser;
  cpp_parser.set_is_cplusplus(true);
  cpp_parser.AddStringInput("#define false false\n"
                            "#if false\n"
                            "#define foo\n"
                            "#endif\n"
                            "#if defined(false)\n"
                            "#define bar\n"
                            "#endif\n",
                            "baz.cc");
  cpp_parser.ProcessDirectives();
  EXPECT_FALSE(cpp_parser.IsMacroDefined("foo"));
  EXPECT_TRUE(cpp_parser.IsMacroDefined("bar"));
}

TEST(CppParserTest, BoolShouldBeOverriddenAndPossibleToUndefOnTrueCase) {
  CppParser cpp_parser;
  cpp_parser.set_is_cplusplus(true);
  cpp_parser.AddStringInput("#define true false\n"
                            "#if true\n"
                            "#define foo\n"
                            "#endif\n"
                            "#undef true\n"
                            "#if true\n"
                            "#define bar\n"
                            "#endif\n",
                            "baz.cc");
  cpp_parser.ProcessDirectives();
  EXPECT_FALSE(cpp_parser.IsMacroDefined("foo"));
  EXPECT_TRUE(cpp_parser.IsMacroDefined("bar"));
}

TEST(CppParserTest, BoolShouldBeOverriddenAndPossibleToUndefOnFalseCase) {
  CppParser cpp_parser;
  cpp_parser.set_is_cplusplus(true);
  cpp_parser.AddStringInput("#define false true\n"
                            "#if false\n"
                            "#define foo\n"
                            "#endif\n"
                            "#undef false\n"
                            "#if false\n"
                            "#define bar\n"
                            "#endif\n",
                            "baz.cc");
  cpp_parser.ProcessDirectives();
  EXPECT_TRUE(cpp_parser.IsMacroDefined("foo"));
  EXPECT_FALSE(cpp_parser.IsMacroDefined("bar"));
}

TEST(CppParserTest, CharToken) {
  CppParser cpp_parser;

  // non-ASCII system is not supported.
  cpp_parser.AddStringInput("#if 'A' == 65\n"
                            "#define foo_true\n"
                            "#else\n"
                            "#define foo_false\n"
                            "#endif\n"

                            "#if 39 == '\\''\n"
                            "#define bar_true\n"
                            "#else\n"
                            "#define bar_false\n"
                            "#endif\n"

                            "#if '*' == 42\n"
                            "#define OPERATOR_OK\n"
                            "#endif\n"

                            "#if '0' == 48\n"
                            "#define DIGIT_OK\n"
                            "#endif\n"

                            "#if ' ' == 32 && ' ' == 0x20 && ' ' == 040\n"
                            "#define SPACE_OK\n"
                            "#endif\n"

                            "#if '\\0' == 0\n"
                            "#define ZERO_OK\n"
                            "#endif\n"

                            "#if '\\n' == 10\n"
                            "#define LF_OK\n"
                            "#endif\n"

                            // macro in lua's lctype.h
                            "#if 'A' == 65 && '0' == 48\n"
                            "#define LUA_USE_CTYPE 0\n"
                            "#else\n"
                            "#define LUA_USE_CTYPE 1\n"
                            "#endif\n"

                            "#if !LUA_USE_CTYPE\n"
                            "#define INCLUDE_LLIMITS\n"
                            "#endif\n",

                            "baz.cc");
  cpp_parser.ProcessDirectives();

  EXPECT_TRUE(cpp_parser.IsMacroDefined("foo_true"));
  EXPECT_FALSE(cpp_parser.IsMacroDefined("foo_false"));
  EXPECT_TRUE(cpp_parser.IsMacroDefined("bar_true"));
  EXPECT_FALSE(cpp_parser.IsMacroDefined("bar_false"));

  EXPECT_TRUE(cpp_parser.IsMacroDefined("OPERATOR_OK"));
  EXPECT_TRUE(cpp_parser.IsMacroDefined("DIGIT_OK"));
  EXPECT_TRUE(cpp_parser.IsMacroDefined("SPACE_OK"));
  EXPECT_TRUE(cpp_parser.IsMacroDefined("ZERO_OK"));
  EXPECT_TRUE(cpp_parser.IsMacroDefined("LF_OK"));

  EXPECT_TRUE(cpp_parser.IsMacroDefined("INCLUDE_LLIMITS"));
}

TEST(CppParserTest, MacroSetChanged) {
  CppParser cpp_parser;
  CppIncludeObserver include_observer(&cpp_parser);
  include_observer.SetInclude("a.h",
                              "#ifndef A_H\n"
                              "#define A_H\n"
                              "#endif\n"
                              "#undef X\n");

  include_observer.SetInclude("b.h",
                              "#ifndef B_H\n"
                              "#define B_H\n"
                              "#define X 1\n"
                              "#include \"a.h\"\n"
                              "#define Y 1\n"
                              "#endif\n");

  cpp_parser.set_include_observer(&include_observer);
  cpp_parser.AddStringInput("#include \"a.h\"\n"
                            "#include \"b.h\"\n",
                            "a.cc");
  cpp_parser.ProcessDirectives();

  // After #include "a.h" in b.h, X must be undefined.
  // Including a.h should not be skipped.
  EXPECT_FALSE(cpp_parser.IsMacroDefined("X"));
  EXPECT_TRUE(cpp_parser.IsMacroDefined("Y"));
}

TEST(CppParserTest, TopFileMacroDefinitionUpdate) {
  CppParser cpp_parser;
  CppIncludeObserver include_observer(&cpp_parser);
  include_observer.SetInclude("a.h",
                              "#ifdef INCLUDE_B\n"
                              "#include \"b.h\"\n"
                              "#endif\n");
  include_observer.SetInclude("b.h",
                              "#define B\n");
  include_observer.SetInclude("c.h",
                              "#include \"a.h\"\n");

  cpp_parser.set_include_observer(&include_observer);
  cpp_parser.AddStringInput("#include \"a.h\"\n"
                            "#define INCLUDE_B\n"
                            "#include \"c.h\"\n",
                            "a.cc");
  cpp_parser.ProcessDirectives();

  // After #define INCLUDE_B in a.cc, the result of
  // #ifdef INCLUDE_B in a.h should be changed.
  EXPECT_TRUE(cpp_parser.IsMacroDefined("B"));
}

TEST(CppParserTest, SkippedByIncludeGuard) {
  CppParser cpp_parser;
  CppIncludeObserver include_observer(&cpp_parser);

  include_observer.SetInclude("a.h",
                              "#ifndef A_H\n"
                              "#define A_H\n"
                              "#endif");
  include_observer.SetInclude("b.h",
                              "#ifndef B_H\n"
                              "#define B_H\n"
                              "#include \"a.h\"\n"
                              "#endif");
  include_observer.SetInclude("c.h",
                              "#ifndef C_H\n"
                              "#define C_H\n"
                              "#include \"b.h\"\n"
                              "#endif");

  cpp_parser.set_include_observer(&include_observer);

  cpp_parser.AddStringInput(
    "#include \"c.h\"\n"
    "#include \"b.h\"\n"
    "#include \"a.h\"\n", "(string)");
  cpp_parser.ProcessDirectives();

  EXPECT_EQ(1, include_observer.IncludedCount("a.h"));
  EXPECT_EQ(1, include_observer.IncludedCount("b.h"));
  EXPECT_EQ(1, include_observer.IncludedCount("c.h"));

  EXPECT_EQ(1, include_observer.SkipCount("a.h"));
  EXPECT_EQ(1, include_observer.SkipCount("b.h"));
  EXPECT_EQ(0, include_observer.SkipCount("c.h"));
}

TEST(CppParserTest, SkippedByIncludeGuardIfDefinedCase) {
  CppParser cpp_parser;
  CppIncludeObserver include_observer(&cpp_parser);

  include_observer.SetInclude("a.h",
                              "#if !defined(A_H)\n"
                              "#define A_H\n"
                              "#endif");
  include_observer.SetInclude("b.h",
                              "#if !defined(B_H)\n"
                              "#define B_H\n"
                              "#include \"a.h\"\n"
                              "#endif");
  include_observer.SetInclude("c.h",
                              "#if !defined(C_H)\n"
                              "#define C_H\n"
                              "#include \"b.h\"\n"
                              "#endif");

  cpp_parser.set_include_observer(&include_observer);

  cpp_parser.AddStringInput(
    "#include \"c.h\"\n"
    "#include \"b.h\"\n"
    "#include \"a.h\"\n", "(string)");
  cpp_parser.ProcessDirectives();

  EXPECT_EQ(1, include_observer.IncludedCount("a.h"));
  EXPECT_EQ(1, include_observer.IncludedCount("b.h"));
  EXPECT_EQ(1, include_observer.IncludedCount("c.h"));

  EXPECT_EQ(1, include_observer.SkipCount("a.h"));
  EXPECT_EQ(1, include_observer.SkipCount("b.h"));
  EXPECT_EQ(0, include_observer.SkipCount("c.h"));
}

TEST(CppParserTest, SkippedByIncludeGuardIfDefinedInvalidCase) {
  CppParser cpp_parser;
  CppIncludeObserver include_observer(&cpp_parser);

  // Only a.h is the correct include guard.
  // So, we won't skip the other header files.

  include_observer.SetInclude("a.h",
                              "#if !defined(A_H)\n"
                              "#define A_H\n"
                              "#endif");

  include_observer.SetInclude("b.h",
                              "#if !defined(B_H) || 1\n"
                              "#define B_H\n"
                              "#endif");

  include_observer.SetInclude("c.h",
                              "#if 1 || !defined(C_H)\n"
                              "#define C_H\n"
                              "#endif");

  include_observer.SetInclude("d.h",
                              "#if ID(!defined(D_H))\n"
                              "#define D_H\n"
                              "#endif");

  include_observer.SetInclude("e.h",
                              "#if defined(E_H)\n"
                              "#define E_H\n"
                              "#endif");

  cpp_parser.set_include_observer(&include_observer);

  cpp_parser.AddStringInput(
    "#define ID(X) X\n"
    "#include \"a.h\"\n"
    "#include \"a.h\"\n"
    "#include \"b.h\"\n"
    "#include \"b.h\"\n"
    "#include \"c.h\"\n"
    "#include \"c.h\"\n"
    "#include \"d.h\"\n"
    "#include \"d.h\"\n"
    "#include \"e.h\"\n"
    "#include \"e.h\"\n", "(string)");
  cpp_parser.ProcessDirectives();

  EXPECT_EQ(1, include_observer.IncludedCount("a.h"));
  EXPECT_EQ(2, include_observer.IncludedCount("b.h"));
  EXPECT_EQ(2, include_observer.IncludedCount("c.h"));
  EXPECT_EQ(2, include_observer.IncludedCount("d.h"));
  EXPECT_EQ(2, include_observer.IncludedCount("e.h"));

  EXPECT_EQ(1, include_observer.SkipCount("a.h"));
  EXPECT_EQ(0, include_observer.SkipCount("b.h"));
  EXPECT_EQ(0, include_observer.SkipCount("c.h"));
  EXPECT_EQ(0, include_observer.SkipCount("d.h"));
  EXPECT_EQ(0, include_observer.SkipCount("e.h"));
}

TEST(CppParserTest, DontSkipdByIncludeGuardIfndefButNotDefined) {
  CppParser cpp_parser;
  CppIncludeObserver include_observer(&cpp_parser);

  include_observer.SetInclude("a.h",
                              "#ifndef FOO\n"
                              "# include \"b.h\"\n"
                              "#else\n"
                              "# include \"c.h\"\n"
                              "#endif\n");
  include_observer.SetInclude("b.h",
                              "#define B_H");
  include_observer.SetInclude("c.h",
                              "#define C_H");

  cpp_parser.set_include_observer(&include_observer);

  cpp_parser.AddStringInput(
      "#include \"a.h\"\n"
      "#define FOO\n"
      "#include \"a.h\"\n", "(string)");
  cpp_parser.ProcessDirectives();

  EXPECT_EQ(2, include_observer.IncludedCount("a.h"));
  EXPECT_EQ(1, include_observer.IncludedCount("b.h"));
  EXPECT_EQ(1, include_observer.IncludedCount("c.h"));

  EXPECT_EQ(0, include_observer.SkipCount("a.h"));
  EXPECT_EQ(0, include_observer.SkipCount("b.h"));
  EXPECT_EQ(0, include_observer.SkipCount("c.h"));
}

TEST(CppParserTest, DontSkipdIncludeGuardAndUndefined) {
  CppParser cpp_parser;
  CppIncludeObserver include_observer(&cpp_parser);

  include_observer.SetInclude("a.h",
                              "#ifndef FOO\n"
                              "#define FOO\n"
                              "#endif\n");

  cpp_parser.set_include_observer(&include_observer);

  cpp_parser.AddStringInput(
      "#include \"a.h\"\n"
      "#undef FOO\n"
      "#include \"a.h\"\n", "(string)");
  cpp_parser.ProcessDirectives();

  EXPECT_EQ(2, include_observer.IncludedCount("a.h"));

  EXPECT_EQ(0, include_observer.SkipCount("a.h"));
}

TEST(CppParserTest, ColonPercentShouldBeTreatedAsSharp) {
  CppParser cpp_parser;
  cpp_parser.AddStringInput("#define  a  b  %:%: c \n"
                            "#define bc 1\n"
                            "#if a == bc\n"
                            "#define correct\n"
                            "#else\n"
                            "#define wrong\n"
                            "#endif\n",
                            "(string)");
  cpp_parser.ProcessDirectives();
  EXPECT_TRUE(cpp_parser.IsMacroDefined("a"));
  EXPECT_TRUE(cpp_parser.IsMacroDefined("correct"));
  EXPECT_FALSE(cpp_parser.IsMacroDefined("wrong"));
}

TEST(CppParserTest, SpaceInMacroShouldBeTreatedAsIs) {
  CppParser cpp_parser;
  CppIncludeObserver include_observer(&cpp_parser);

  include_observer.SetInclude("foobar",
                              "");
  include_observer.SetInclude("foo bar",
                              "");
  include_observer.SetInclude("foo  bar",
                              "");

  cpp_parser.set_include_observer(&include_observer);

  // FOO2 is expanded to <foo_bar>, not <foo__bar> (underscore means a space)
  cpp_parser.AddStringInput("#define FOO1 <foo bar>\n"
                            "#define FOO2 <foo  bar>\n"
                            "#include FOO1\n"
                            "#include FOO2\n",
                            "foo.cc");
  cpp_parser.ProcessDirectives();

  EXPECT_EQ(0, include_observer.IncludedCount("foobar"));
  EXPECT_EQ(2, include_observer.IncludedCount("foo bar"));
  EXPECT_EQ(0, include_observer.IncludedCount("foo  bar"));
}

TEST(CppParserTest, SpaceNearDoubleSharpShouldBeTreatedCorrectly) {
  CppParser cpp_parser;
  CppIncludeObserver include_observer(&cpp_parser);

  include_observer.SetInclude("hogefuga",
                              "");

  cpp_parser.set_include_observer(&include_observer);

  cpp_parser.AddStringInput("#define cut(x, y) <x   ##   y>\n"
                            "#include cut(hoge, fuga)\n",
                            "foo.cc");
  cpp_parser.ProcessDirectives();

  EXPECT_EQ(1, include_observer.IncludedCount("hogefuga"));
}

TEST(CppParserTest, DirectiveWithSpaces) {
  CppParser cpp_parser;
  CppIncludeObserver include_observer(&cpp_parser);

  include_observer.SetInclude("x.h", "");
  include_observer.SetInclude("y.h", "");

  cpp_parser.set_include_observer(&include_observer);

  cpp_parser.AddStringInput("\n"
                            " # define foo\n"
                            "  #   define bar\n"
                            " # ifdef foo\n"
                            "  #  include \"x.h\"\n"
                            " # endif\n"
                            "# ifdef bar\n"
                            "# include \"y.h\"\n",
                            "# endif\n"
                            "foo.cc");
  cpp_parser.ProcessDirectives();

  EXPECT_EQ(1, include_observer.IncludedCount("x.h"));
  EXPECT_EQ(1, include_observer.IncludedCount("y.h"));
}

TEST(CppParserTest, MultiAddMacroByString) {
  CppParser cpp_parser;
  CppIncludeObserver include_observer(&cpp_parser);

  include_observer.SetInclude("x.h", "");
  include_observer.SetInclude("y.h", "");

  cpp_parser.set_include_observer(&include_observer);

  cpp_parser.AddMacroByString("macro1", "");
  cpp_parser.AddMacroByString("macro2", "");
  cpp_parser.AddStringInput("#ifdef macro1\n"
                            "#include \"x.h\"\n"
                            "#endif\n"
                            "#ifdef macro2\n"
                            "#include \"y.h\"\n"
                            "#endif\n",
                            "foo.cc");
  cpp_parser.ProcessDirectives();

  EXPECT_TRUE(cpp_parser.IsMacroDefined("macro1"));
  EXPECT_TRUE(cpp_parser.IsMacroDefined("macro2"));

  EXPECT_EQ(1, include_observer.IncludedCount("x.h"));
  EXPECT_EQ(1, include_observer.IncludedCount("y.h"));
}

TEST(CppParserTest, LimitIncludeDepth) {
  CppParser cpp_parser;
  CppIncludeObserver include_observer(&cpp_parser);

  include_observer.SetInclude("bar.h",
                              "#include \"bar.h\"\n");

  cpp_parser.set_include_observer(&include_observer);

  cpp_parser.AddStringInput("#include \"bar.h\"\n",
                            "foo.cc");
  EXPECT_FALSE(cpp_parser.ProcessDirectives());
  EXPECT_EQ(1024, include_observer.IncludedCount("bar.h"));
}

// Regression test from android source (b/78436008)
TEST(CppParserTest, GluingInteger) {
  CppParser cpp_parser;

  cpp_parser.AddStringInput(
      "#define _WIN32_WINNT 0x0600\n"
      "#define NV_FROM_WIN32_WINNT2(V) V##0000\n"
      "#define NV_FROM_WIN32_WINNT(V) NV_FROM_WIN32_WINNT2(V)\n"
      "#define NV NV_FROM_WIN32_WINNT(_WIN32_WINNT)\n"
      "#if NV >= 0x06000000\n"
      "# define OK 1\n"
      "#endif\n",
      "foo.cc");

  EXPECT_TRUE(cpp_parser.ProcessDirectives());
  EXPECT_TRUE(cpp_parser.IsMacroDefined("OK"));
}

}  // namespace devtools_goma
