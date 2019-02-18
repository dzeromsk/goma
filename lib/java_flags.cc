// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/java_flags.h"

#include "absl/strings/str_split.h"
#include "glog/logging.h"
#include "glog/stl_logging.h"
#include "lib/path_util.h"

namespace devtools_goma {

void ParseJavaClassPaths(const std::vector<string>& class_paths,
                         std::vector<string>* jar_files) {
  for (const string& class_path : class_paths) {
    for (auto&& path : absl::StrSplit(class_path, ':')) {
      // TODO: We need to handle directories.
      absl::string_view ext = GetExtension(path);
      if (ext == "jar" || ext == "zip") {
        jar_files->push_back(string(path));
      }
    }
  }
}

JavacFlags::JavacFlags(const std::vector<string>& args, const string& cwd)
    : CompilerFlags(args, cwd) {
  if (!CompilerFlags::ExpandPosixArgs(cwd, args, &expanded_args_,
                                      &optional_input_filenames_)) {
    Fail("Unable to expand args");
    return;
  }
  bool has_at_file = !optional_input_filenames_.empty();

  is_successful_ = true;
  lang_ = "java";

  FlagParser parser;
  DefineFlags(&parser);
  std::vector<string> boot_class_paths;
  std::vector<string> class_paths;
  std::vector<string> remained_flags;
  // The destination directory for class files.
  FlagParser::Flag* flag_d = parser.AddFlag("d");
  flag_d->SetValueOutputWithCallback(nullptr, &output_dirs_);
  // The directory to place generated source files.
  parser.AddFlag("s")->SetValueOutputWithCallback(nullptr, &output_dirs_);
  // Maybe classpaths are loaded in following way:
  // 1. bootstrap classes
  // 2. extension classes
  // 3. user classes.
  // and we might need to search bootclasspath first, extdirs, and classpath
  // in this order.
  // https://docs.oracle.com/javase/8/docs/technotes/tools/findingclasses.html
  parser.AddFlag("bootclasspath")
      ->SetValueOutputWithCallback(nullptr, &boot_class_paths);
  // TODO: Support -Xbootclasspath if needed.
  parser.AddFlag("cp")->SetValueOutputWithCallback(nullptr, &class_paths);
  parser.AddFlag("classpath")
      ->SetValueOutputWithCallback(nullptr, &class_paths);
  // TODO: Handle CLASSPATH environment variables.
  // TODO: Handle -extdirs option.
  FlagParser::Flag* flag_processor = parser.AddFlag("processor");
  // TODO: Support -sourcepath.
  parser.AddNonFlag()->SetOutput(&remained_flags);

  parser.Parse(expanded_args_);
  unknown_flags_ = parser.unknown_flag_args();

  if (!has_at_file) {
    // no @file in args.
    CHECK_EQ(args_, expanded_args_);
    expanded_args_.clear();
  }

  for (const auto& arg : remained_flags) {
    if (absl::EndsWith(arg, ".java")) {
      input_filenames_.push_back(arg);
      const string& output_filename = arg.substr(0, arg.size() - 5) + ".class";
      if (!flag_d->seen()) {
        output_files_.push_back(output_filename);
      }
    }
  }

  ParseJavaClassPaths(boot_class_paths, &jar_files_);
  ParseJavaClassPaths(class_paths, &jar_files_);

  if (flag_processor->seen()) {
    for (const string& value : flag_processor->values()) {
      for (auto&& c : absl::StrSplit(value, ',')) {
        processors_.push_back(string(c));
      }
    }
  }
}

/* static */
void JavacFlags::DefineFlags(FlagParser* parser) {
  FlagParser::Options* opts = parser->mutable_options();
  opts->flag_prefix = '-';

  // https://docs.oracle.com/javase/8/docs/technotes/tools/windows/javac.html
  // -XD<foo>, -XD<foo>=<bar> is not documented, so let allow them one by one.

  static const struct {
    const char* name;
    FlagType flag_type;
  } kFlags[] = {
    { "J-Xmx", kPrefix },  // -J-Xmx2048M, -J-Xmx1024M; Specify max JVM memory
    { "Werror", kBool },  // Treat warning as error
    { "XDignore.symbol.file", kBool },  // to use JRE internal classes
    { "XDskipDuplicateBridges=", kPrefix },  //  See https://android.googlesource.com/platform/build/soong.git/+/master/java/config/config.go#60
    { "XDstringConcat=", kPrefix },  // Specifies how to concatenate strings
    { "Xdoclint:", kPrefix },  // -Xdoclint: lint for document.
    { "Xlint", kBool },  // -Xlint
    { "Xlint:", kPrefix },  // -Xlint:all, -Xlint:none, ...
    { "Xmaxerrs", kNormal },  // -Xmaxerrs <number>; Sets the maximum number of errors to print.
    { "Xmaxwarns", kNormal },  // -Xmaxwarns <number>; Sets the maximum number of warnings to print.
    { "bootclasspath", kNormal },  // Cross-compiles against the specified set of boot classes.
    { "classpath", kNormal },  // set classpath
    { "cp", kNormal },  // set classpath
    { "d", kNormal },  // Sets the destination directory for class files.
    { "encoding", kNormal },  // -encoding <encoding>; Specify encoding.
    { "g", kBool },  // -g; generate debug information
    { "g:", kPrefix },   // -g:foobar; generate debug information
    { "nowarn", kBool },  // -nowarn; the same effect of -Xlint:none.
    { "parameters", kBool },  // Stores formal parameter names of constructors and methods in the generated class file
    { "proc:none", kBool },  // Desable annotation processor.
    { "processor", kNormal },  // Names of the annotation processors to run.
    { "processorpath", kBool },  // -processorpath <path>;  // Specifies where to find annotation processors. If this option is not used, then the class path is searched for processors
    { "s", kNormal },  // Specifies the directory where to place the generated source files.
    { "source", kNormal },  // -source <version> e.g. -source 8; Specify java source version
    { "sourcepath", kNormal },  // -sourcepath <sourcepath>
    { "target", kNormal },  // -target <version> e.g. -target 8; Generates class files that target a specified release of the virtual machine.
  };

  for (const auto& f : kFlags) {
    switch (f.flag_type) {
      case kNormal:
        parser->AddFlag(f.name);
        break;
      case kPrefix:
        parser->AddPrefixFlag(f.name);
        break;
      case kBool:
        parser->AddBoolFlag(f.name);
        break;
    }
  }
}

/* static */
bool JavacFlags::IsJavacCommand(absl::string_view arg) {
  const absl::string_view basename = GetBasename(arg);
  return basename.find("javac") != absl::string_view::npos;
}

/* static */
string JavacFlags::GetCompilerName(absl::string_view /*arg*/) {
  return "javac";
}

// ----------------------------------------------------------------------

JavaFlags::JavaFlags(const std::vector<string>& args, const string& cwd)
    : CompilerFlags(args, cwd) {
  is_successful_ = true;
  lang_ = "java bytecode";

  FlagParser parser;
  DefineFlags(&parser);
  std::vector<string> class_paths;
  std::vector<string> system_properties;
  std::vector<string> remained_flags;
  parser.AddFlag("cp")->SetValueOutputWithCallback(nullptr, &class_paths);
  parser.AddFlag("classpath")
      ->SetValueOutputWithCallback(nullptr, &class_paths);
  parser.AddFlag("D")->SetValueOutputWithCallback(nullptr, &system_properties);
  parser.AddFlag("jar")->SetValueOutputWithCallback(nullptr, &input_filenames_);
  parser.AddNonFlag()->SetOutput(&remained_flags);
  parser.Parse(args_);
  unknown_flags_ = parser.unknown_flag_args();

  ParseJavaClassPaths(class_paths, &jar_files_);
}

/* static */
bool JavaFlags::IsJavaCommand(absl::string_view arg) {
  const absl::string_view stem = GetStem(arg);
  return stem == "java";
}

/* static */
void JavaFlags::DefineFlags(FlagParser* parser) {
  FlagParser::Options* opts = parser->mutable_options();
  opts->flag_prefix = '-';

  parser->AddFlag("D");
  parser->AddFlag("cp");
  parser->AddFlag("classpath");
  parser->AddFlag("jar");
}

}  // namespace devtools_goma
