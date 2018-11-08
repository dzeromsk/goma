// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "parser.h"

#include "gtest/gtest.h"
#include "lexer.h"

namespace devtools_goma {
namespace modulemap {

namespace {

bool Parse(const string& str, ModuleMap* module_map) {
  std::unique_ptr<Content> content = Content::CreateFromString(str);

  std::vector<Token> tokens;
  if (!Lexer::Run(*content, &tokens)) {
    return false;
  }
  if (!Parser::Run(tokens, module_map)) {
    return false;
  }
  return true;
}

}  // anonymous namespace

TEST(ParserTest, Example) {
  static const char kData[] = R"(
module std [system] [extern_c] {
  module assert {
    textual header "assert.h"
    header "bits/assert-decls.h"
    export *
  }

  module complex {
    header "complex.h"
    export *
  }

  module ctype {
    header "ctype.h"
    export *
  }

  module errno {
    header "errno.h"
    header "sys/errno.h"
    export *
  }

  module fenv {
    header "fenv.h"
    export *
  }
})";

  ModuleMap module_map;
  EXPECT_TRUE(Parse(kData, &module_map));

  // modulemap has module std.
  ASSERT_EQ(1U, module_map.modules().size());
  const Module& module_std = module_map.modules()[0];

  EXPECT_EQ("std", module_std.module_id());
  EXPECT_EQ(2U, module_std.attributes().size());
  EXPECT_TRUE(module_std.HasAttribute("system"));
  EXPECT_TRUE(module_std.HasAttribute("extern_c"));
  // module std has 5 submodules.
  EXPECT_EQ(5U, module_std.submodules().size());
}

TEST(ParserTest, AllExamples) {
  // parse all examples in clang modules document.
  static const char kData[] = R"(
module std {
  module vector {
    requires cplusplus
    header "vector"
  }

  module type_traits {
    requires cplusplus11
    header "type_traits"
  }
}

module std [system] {
  textual header "assert.h"
}

module MyLib {
  umbrella "MyLib"
  explicit module * {
    export *
  }
}

module MyLib {
  explicit module A {
    header "A.h"
    export *
  }

  explicit module B {
    header "B.h"
    export *
  }
}

module MyLib {
  module Base {
    header "Base.h"
  }

  module Derived {
    header "Derived.h"
    export Base
  }
}

module MyLib {
  module Base {
    header "Base.h"
  }

  module Derived {
    header "Derived.h"
    export *
  }
}

module MyFrameworkCore {
  export_as MyFramework
}

module A {
  header "a.h"
}

module B {
  header "b.h"
}

module C {
  header "c.h"
  use B
}

module MyLogger {
  umbrella header "MyLogger.h"
  config_macros [exhaustive] NDEBUG
}

module Conflicts {
  explicit module A {
    header "conflict_a.h"
    conflict B, "we just don't like B"
  }

  module B {
    header "conflict_b.h"
  }
}

module Foo {
  header "Foo.h"
}

module Foo_Private {
  header "Foo_Private.h"
}
)";

  ModuleMap module_map;
  EXPECT_TRUE(Parse(kData, &module_map));
}

TEST(ParserTest, MemberRequires) {
  static const char kData[] = R"(
module foo {
  requires foo, !bar
}
)";

  ModuleMap module_map;
  EXPECT_TRUE(Parse(kData, &module_map));

  ASSERT_EQ(1U, module_map.modules().size());
  const Module& module_foo = module_map.modules()[0];

  ASSERT_EQ(2U, module_foo.requires().size());
  EXPECT_EQ(Feature("foo", true), module_foo.requires()[0]);
  EXPECT_EQ(Feature("bar", false), module_foo.requires()[1]);
}

// header-declaration:
//   privateopt textualopt header string-literal header-attrsopt
//   umbrella header string-literal header-attrsopt
//   exclude header string-literal header-attrsopt
TEST(ParserTest, MemberHeader) {
  static const char kData[] = R"(
module foo {
  header "a.h"
  umbrella header "b.h"
  private textual header "c.h"
  exclude header "d.h"
}
)";

  ModuleMap module_map;
  EXPECT_TRUE(Parse(kData, &module_map));

  ASSERT_EQ(1U, module_map.modules().size());
  const Module& module_foo = module_map.modules()[0];

  ASSERT_EQ(4U, module_foo.headers().size());
  EXPECT_EQ("a.h", module_foo.headers()[0].name());
  EXPECT_EQ("b.h", module_foo.headers()[1].name());
  EXPECT_EQ("c.h", module_foo.headers()[2].name());
  EXPECT_EQ("d.h", module_foo.headers()[3].name());
}

// umbrella-dir-declaration:
//   umbrella string-literal
TEST(ParserTest, MemberUmbrellaDir) {
  static const char kData[] = R"(
module foo {
  umbrella "foo"
}
)";

  ModuleMap module_map;
  EXPECT_TRUE(Parse(kData, &module_map));

  ASSERT_EQ(1U, module_map.modules().size());
  const Module& module_foo = module_map.modules()[0];

  ASSERT_EQ(1U, module_foo.umbrella_dirs().size());
  EXPECT_EQ("foo", module_foo.umbrella_dirs()[0]);
}

// submodule-declaration:
//   module-declaration
//   inferred-submodule-declaration
TEST(ParserTest, MemberSubmodule) {
  static const char kData[] = R"(
module foo {
  explicit module * {
    export *
  }
}
)";

  ModuleMap module_map;
  EXPECT_TRUE(Parse(kData, &module_map));

  ASSERT_EQ(1U, module_map.modules().size());
  const Module& module_foo = module_map.modules()[0];

  ASSERT_EQ(1U, module_foo.submodules().size());
  const Module& submodule = module_foo.submodules()[0];

  EXPECT_EQ("*", submodule.module_id());
}

TEST(ParserTest, MemberExport) {
  static const char kData[] = R"(
module foo {
  export bar
}
)";

  ModuleMap module_map;
  EXPECT_TRUE(Parse(kData, &module_map));

  ASSERT_EQ(1U, module_map.modules().size());
  const Module& module_foo = module_map.modules()[0];

  ASSERT_EQ(1U, module_foo.exports().size());
  EXPECT_EQ("bar", module_foo.exports()[0]);
}

TEST(ParserTest, MemberExportAs) {
  static const char kData[] = R"(
module foo {
  export_as bar
}
)";

  ModuleMap module_map;
  EXPECT_TRUE(Parse(kData, &module_map));

  ASSERT_EQ(1U, module_map.modules().size());
  const Module& module_foo = module_map.modules()[0];

  ASSERT_EQ(1U, module_foo.export_as().size());
  EXPECT_EQ("bar", module_foo.export_as()[0]);
}

TEST(ParserTest, MemberUse) {
  static const char kData[] = R"(
module foo {
  use bar
}
)";

  ModuleMap module_map;
  EXPECT_TRUE(Parse(kData, &module_map));

  ASSERT_EQ(1U, module_map.modules().size());
  const Module& module_foo = module_map.modules()[0];

  ASSERT_EQ(1U, module_foo.uses().size());
  EXPECT_EQ("bar", module_foo.uses()[0]);
}

TEST(ParserTest, MemberLink) {
  static const char kData[] = R"(
module foo {
  link "bar"
}
)";

  ModuleMap module_map;
  EXPECT_TRUE(Parse(kData, &module_map));

  ASSERT_EQ(1U, module_map.modules().size());
  const Module& module_foo = module_map.modules()[0];

  ASSERT_EQ(1U, module_foo.links().size());
  EXPECT_EQ("bar", module_foo.links()[0].name());
}

TEST(ParserTest, MemberConfigMacros) {
  static const char kData[] = R"(
module foo {
  config_macros [exhaustive] NDEBUG
}
)";

  ModuleMap module_map;
  EXPECT_TRUE(Parse(kData, &module_map));

  ASSERT_EQ(1U, module_map.modules().size());
  const Module& module_foo = module_map.modules()[0];

  ASSERT_EQ(1U, module_foo.config_macros().size());
  EXPECT_EQ(std::vector<string>{"NDEBUG"},
            module_foo.config_macros()[0].macros());
  EXPECT_EQ(std::vector<string>{"exhaustive"},
            module_foo.config_macros()[0].attributes());
}

TEST(ParserTest, MemberConflict) {
  static const char kData[] = R"(
module foo {
  conflict bar.baz, "prohibited"
}
)";

  ModuleMap module_map;
  EXPECT_TRUE(Parse(kData, &module_map));

  ASSERT_EQ(1U, module_map.modules().size());
  const Module& module_foo = module_map.modules()[0];

  ASSERT_EQ(1U, module_foo.conflicts().size());
  EXPECT_EQ("bar.baz", module_foo.conflicts()[0].module_id());
  EXPECT_EQ("prohibited", module_foo.conflicts()[0].reason());
}

TEST(ParserTest, ExternModule) {
  static const char kData[] = R"(
extern module foo.bar "foo.modulemap"
)";

  ModuleMap module_map;
  EXPECT_TRUE(Parse(kData, &module_map));

  // modulemap has module foo.bar.
  ASSERT_EQ(1U, module_map.modules().size());
  const Module& foobar = module_map.modules()[0];

  EXPECT_EQ("foo.bar", foobar.module_id());
  EXPECT_EQ("foo.modulemap", foobar.extern_filename());
}

}  // namespace modulemap
}  // namespace devtools_goma
