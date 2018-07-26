// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "clang_compiler_info_builder_helper.h"

#include "absl/memory/memory.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "cmdline_parser.h"
#include "compiler_flag_type.h"
#include "compiler_flag_type_specific.h"
#include "compiler_info.h"
#include "compiler_info_builder.h"
#include "counterz.h"
#include "cxx/include_processor/predefined_macros.h"
#include "flag_parser.h"
#include "gcc_flags.h"
#include "glog/logging.h"
#include "glog/stl_logging.h"
#include "ioutil.h"
#include "path.h"
#include "path_resolver.h"
#include "scoped_tmp_file.h"
#include "util.h"
#include "vc_flags.h"

#ifdef _WIN32
#include "posix_helper_win.h"
#endif

namespace devtools_goma {

namespace {

#include "clang_features.cc"

bool AddResourceInfo(
    const string& cwd,
    const string& path,
    const CompilerInfoData::ResourceType type,
    google::protobuf::RepeatedPtrField<CompilerInfoData::ResourceInfo>* rr) {
  CompilerInfoData::ResourceInfo* r = rr->Add();
  if (!CompilerInfoBuilder::ResourceInfoFromPath(cwd, path, type, r)) {
    rr->RemoveLast();
    return false;
  }
  return true;
}

bool UpdateResourceInfo(
    const string& cwd,
    const std::vector<ClangCompilerInfoBuilderHelper::ResourceList>& resource,
    CompilerInfoData* data) {
  for (const auto& r : resource) {
    if (!AddResourceInfo(cwd, r.first, r.second, data->mutable_resource())) {
      LOG(ERROR) << "invalid resource file:"
                 << " cwd=" << cwd << " r=" << r;
      return false;
    }
  }
  return true;
}

bool ParseDriverArgs(absl::string_view display_output,
                     std::vector<string>* driver_args) {
  for (auto&& line : absl::StrSplit(display_output, absl::ByAnyChar("\r\n"),
                                    absl::SkipEmpty())) {
    if (absl::StartsWith(line, " ")) {
#ifdef _WIN32
      return ParseWinCommandLineToArgv(line, driver_args);
#else
      return ParsePosixCommandLineToArgv(line, driver_args);
#endif
    }
  }
  return false;
}

// Returns compiler-specific flag parser, or nullptr if not supported.
std::unique_ptr<FlagParser> GetFlagParser(absl::string_view argv0) {
  std::unique_ptr<FlagParser> flag_parser = absl::make_unique<FlagParser>();
  CompilerFlagType compiler_type =
      CompilerFlagTypeSpecific::FromArg(argv0).type();
  FlagParser::Options* opts = flag_parser->mutable_options();
  switch (compiler_type) {
    case CompilerFlagType::Gcc:
      opts->allows_equal_arg = true;
      GCCFlags::DefineFlags(flag_parser.get());
      return flag_parser;
    case CompilerFlagType::Clexe:
      opts->allows_equal_arg = true;
      VCFlags::DefineFlags(flag_parser.get());
      return flag_parser;
    default:
      LOG(ERROR) << "got unknown type."
                 << " argv0=" << argv0 << " type=" << compiler_type;
      return nullptr;
  }
}

string GccDisplayPrograms(const string& normal_compiler_path,
                          const std::vector<string>& compiler_info_flags,
                          const std::vector<string>& compiler_info_envs,
                          const string& lang_flag,
                          const string& option,
                          const string& cwd,
                          int32_t* status) {
  std::vector<string> argv;
  argv.push_back(normal_compiler_path);
  copy(compiler_info_flags.begin(), compiler_info_flags.end(),
       back_inserter(argv));
  argv.push_back(lang_flag);
  if (!option.empty()) {
    if (VCFlags::IsClangClCommand(normal_compiler_path)) {
      argv.push_back("-Xclang");
    }
    argv.push_back(option);
  }
#ifdef _WIN32
  // This code is used by NaCl gcc, PNaCl clang and clang-cl on Windows.
  // Former uses /dev/null as null device, and latter recently uses NUL as
  // null device.  To provide the same code to both, let me use temporary
  // file for that.
  ScopedTmpFile tmp("gcc_display_program");
  if (!tmp.valid()) {
    LOG(ERROR) << "cannot make an empty file";
    *status = -1;
    return "";
  }
  tmp.Close();
  const string& empty_file = tmp.filename();
  VLOG(2) << "empty_file=" << empty_file;
#else
  const string& empty_file = "/dev/null";
#endif
  argv.push_back("-v");
  argv.push_back("-E");
  argv.push_back(empty_file);
  argv.push_back("-o");
  argv.push_back(empty_file);

  std::vector<string> env;
  env.push_back("LC_ALL=C");
  copy(compiler_info_envs.begin(), compiler_info_envs.end(),
       back_inserter(env));

  {
    GOMA_COUNTERZ("ReadCommandOutput(-v)");
    return ReadCommandOutput(normal_compiler_path, argv, env, cwd,
                             MERGE_STDOUT_STDERR, status);
  }
}

string GccDisplayPredefinedMacros(
    const string& normal_compiler_path,
    const std::vector<string>& compiler_info_flags,
    const std::vector<string>& compiler_info_envs,
    const string& cwd,
    const string& lang_flag,
    int32_t* status) {
  std::vector<string> argv;
  argv.push_back(normal_compiler_path);
  copy(compiler_info_flags.begin(), compiler_info_flags.end(),
       back_inserter(argv));
#ifdef _WIN32
  // This code is used by NaCl gcc, PNaCl clang and clang-cl on Windows.
  // Former uses /dev/null as null device, and latter recently uses NUL as
  // null device.  To provide the same code to both, let me use temporary
  // file for that.
  ScopedTmpFile tmp("gcc_display_predefined_macro");
  if (!tmp.valid()) {
    LOG(ERROR) << "cannot make an empty file";
    *status = -1;
    return "";
  }
  tmp.Close();
  const string& empty_file = tmp.filename();
  VLOG(2) << "empty_file=" << empty_file;
#else
  const string& empty_file = "/dev/null";
#endif

  argv.push_back(lang_flag);
  argv.push_back("-E");
  argv.push_back(empty_file);
  if (VCFlags::IsClangClCommand(normal_compiler_path)) {
    argv.push_back("-Xclang");
  }
  argv.push_back("-dM");

  std::vector<string> env;
  env.push_back("LC_ALL=C");
  copy(compiler_info_envs.begin(), compiler_info_envs.end(),
       back_inserter(env));

  string macros;
  {
    GOMA_COUNTERZ("ReadCommandOutput(-E -dM)");
    macros = ReadCommandOutput(normal_compiler_path, argv, env, cwd,
                               MERGE_STDOUT_STDERR, status);
  }
  if (*status != 0) {
    LOG(ERROR) << "ReadCommandOutput exited with non zero status code."
               << " normal_compiler_path=" << normal_compiler_path
               << " status=" << status << " argv=" << argv << " env=" << env
               << " cwd=" << cwd << " macros=" << macros;
    return "";
  }
  return macros;
}

}  // anonymous namespace

/* static */
bool ClangCompilerInfoBuilderHelper::GetResourceDir(
    const string& c_display_output,
    CompilerInfoData* compiler_info) {
  std::vector<string> driver_args;
  if (!ParseDriverArgs(c_display_output, &driver_args)) {
    return false;
  }

  std::unique_ptr<FlagParser> flag_parser(GetFlagParser(driver_args[0]));
  if (!flag_parser) {
    return false;
  }
  FlagParser::Flag* resource_dir = flag_parser->AddFlag("resource-dir");
  flag_parser->Parse(driver_args);

  if (!resource_dir->seen()) {
    return false;
  }

  string dir = resource_dir->GetLastValue();
  if (dir.empty()) {
    return false;
  }

  compiler_info->mutable_cxx()->set_resource_dir(std::move(dir));
  return true;
}

/* static */
ClangCompilerInfoBuilderHelper::ParseStatus
ClangCompilerInfoBuilderHelper::ParseResourceOutput(
    const string& argv0,
    const string& cwd,
    const string& display_output,
    std::vector<ResourceList>* paths) {
  // We only detect resource for clang now.
  if (!GCCFlags::IsClangCommand(argv0) && !VCFlags::IsClangClCommand(argv0)) {
    return ParseStatus::kNotParsed;
  }

  for (absl::string_view line : absl::StrSplit(
           display_output, absl::ByAnyChar("\r\n"), absl::SkipEmpty())) {
    // Silently upload crtbegin.o.
    // clang uses crtbegin.o in GCC installation directory as a marker to
    // understand local gcc version etc.  We need to upload it to goma server
    // to make clang in server behave like local clang.
    // See Also:
    // https://github.com/llvm-mirror/clang/blob/69f63a0cc21da9f587125760f10610146c8c47c3/lib/Driver/ToolChains/Gnu.cpp#L1444
    if (absl::ConsumePrefix(&line, "Selected GCC installation: ")) {
      const auto gcc_install_path = line;
      // TODO: consider supporting IAMCU?
      string crtbegin_path = file::JoinPath(gcc_install_path, "crtbegin.o");
      const string abs_crtbegin_path =
          file::JoinPathRespectAbsolute(cwd, crtbegin_path);
      if (access(abs_crtbegin_path.c_str(), R_OK) == 0) {
        paths->emplace_back(std::move(crtbegin_path),
                            CompilerInfoData::CLANG_GCC_INSTALLATION_MARKER);
      } else {
        LOG(ERROR) << "specified crtbegin.o not found."
                   << " argv0=" << argv0 << " cwd=" << cwd
                   << " crtbegin_path=" << crtbegin_path;
      }
      continue;
    }
    if (!absl::StartsWith(line, " ")) {
      continue;
    }

    // The first command should be the "cc1" command.
    // We do not need to read anything else.
    std::vector<string> argv;
#ifdef _WIN32
    ParseWinCommandLineToArgv(line, &argv);
#else
    ParsePosixCommandLineToArgv(line, &argv);
#endif
    if (argv.empty()) {
      LOG(ERROR) << "command line for command is empty."
                 << " line=" << line << " display_output=" << display_output;
      return ParseStatus::kFail;
    }
    if (!GCCFlags::IsClangCommand(argv[0]) &&
        !VCFlags::IsClangClCommand(argv[0])) {
      LOG(ERROR) << "Expecting clang command but we got command for non-clang"
                 << " line=" << line << " display_output=" << display_output;
      return ParseStatus::kFail;
    }
    std::unique_ptr<FlagParser> flag_parser(GetFlagParser(argv[0]));
    if (!flag_parser) {
      return ParseStatus::kFail;
    }
    std::vector<string> blacklist_paths;
    flag_parser->AddFlag("fsanitize-blacklist")
        ->SetValueOutputWithCallback(nullptr, &blacklist_paths);
    flag_parser->Parse(argv);
    for (string& path : blacklist_paths) {
      paths->emplace_back(std::move(path), CompilerInfoData::CLANG_RESOURCE);
    }
    return ParseStatus::kSuccess;
  }
  LOG(ERROR) << "command output not found."
             << " argv0=" << argv0 << " cwd=" << cwd
             << " display_output=" << display_output;
  return ParseStatus::kFail;
}

/* static */
string ClangCompilerInfoBuilderHelper::ParseRealClangPath(
    absl::string_view v_out) {
  absl::string_view::size_type pos = v_out.find_first_of('"');
  if (pos == absl::string_view::npos)
    return "";
  v_out.remove_prefix(pos + 1);
  pos = v_out.find_first_of('"');
  if (pos == absl::string_view::npos)
    return "";
  v_out = v_out.substr(0, pos);
  if (!GCCFlags::IsClangCommand(v_out))
    return "";
  return string(v_out);
}

/* static */
bool ClangCompilerInfoBuilderHelper::ParseClangVersionTarget(
    const string& sharp_output,
    string* version,
    string* target) {
  static const char* kTarget = "Target: ";
  std::vector<string> lines = ToVector(
      absl::StrSplit(sharp_output, absl::ByAnyChar("\r\n"), absl::SkipEmpty()));
  if (lines.size() < 2) {
    LOG(ERROR) << "lines has less than 2 elements."
               << " sharp_output=" << sharp_output;
    return false;
  }
  if (!absl::StartsWith(lines[1], kTarget)) {
    LOG(ERROR) << "lines[1] does not have " << kTarget << " prefix."
               << " lines[1]=" << lines[1] << " sharp_output=" << sharp_output;
    return false;
  }
  version->assign(std::move(lines[0]));
  target->assign(lines[1].substr(strlen(kTarget)));
  return true;
}

// static
bool ClangCompilerInfoBuilderHelper::GetPredefinedMacros(
    const string& normal_compiler_path,
    const std::vector<string>& compiler_info_flags,
    const std::vector<string>& compiler_info_envs,
    const string& cwd,
    const string& lang_flag,
    CompilerInfoData* compiler_info) {
  int32_t status;
  const string& macros =
      GccDisplayPredefinedMacros(normal_compiler_path, compiler_info_flags,
                                 compiler_info_envs, cwd, lang_flag, &status);
  if (status != 0)
    return false;
  compiler_info->mutable_cxx()->set_predefined_macros(macros);
  return true;
}

/* static */
bool ClangCompilerInfoBuilderHelper::ParseFeatures(
    const string& feature_output,
    FeatureList object_macros,
    FeatureList function_macros,
    FeatureList features,
    FeatureList extensions,
    FeatureList attributes,
    FeatureList cpp_attributes,
    FeatureList declspec_attributes,
    FeatureList builtins,
    CompilerInfoData* compiler_info) {
  const size_t num_all_features =
      object_macros.second + function_macros.second + features.second +
      extensions.second + attributes.second + cpp_attributes.second +
      declspec_attributes.second + builtins.second;
  std::vector<string> lines =
      ToVector(absl::StrSplit(feature_output, '\n', absl::SkipEmpty()));
  size_t index = 0;
  int expected_index = -1;
  for (const auto& line : lines) {
    {
      absl::string_view line_view(line);
      if ((absl::ConsumePrefix(&line_view, "# ") ||
           absl::ConsumePrefix(&line_view, "#line ")) &&
          !line_view.empty()) {
        // expects:
        // # <number> "<filename>" or
        // #line <number> "<filename>"
        (void)absl::SimpleAtoi(line_view, &expected_index);
        --expected_index;
      }
    }

    if (line[0] == '#' || line[0] == '\0')
      continue;

    if (!(isalnum(line[0]) || line[0] == '_')) {
      LOG(ERROR) << "Ignoring expected line in clang's output: " << line;
      continue;
    }

    if (index >= num_all_features) {
      LOG(ERROR) << "The number of known extensions is strange:"
                 << " index=" << index << " feature_output=" << feature_output;
      CompilerInfoBuilder::AddErrorMessage(
          "goma error: unknown feature or extension detected.", compiler_info);
      return false;
    }

    size_t current_index = index++;
    LOG_IF(WARNING, expected_index < 0 ||
                        static_cast<size_t>(expected_index) != current_index)
        << "index seems to be wrong."
        << " current_index=" << current_index
        << " expected_index=" << expected_index
        << " feature_output=" << feature_output;

    // The result is 0 or 1 in most cases.
    // __has_cpp_attribute(xxx) can be 200809, 201309, though.
    // Anyway, we remember the value that is all digit.

    bool all_digit = true;
    for (char c : line) {
      if (!isdigit(c)) {
        all_digit = false;
        break;
      }
    }
    int value = all_digit ? std::atoi(line.c_str()) : 0;
    if (value == 0)
      continue;

    if (current_index < object_macros.second) {
      compiler_info->mutable_cxx()->add_supported_predefined_macros(
          object_macros.first[current_index]);
      continue;
    }
    current_index -= object_macros.second;
    if (current_index < function_macros.second) {
      compiler_info->mutable_cxx()->add_supported_predefined_macros(
          function_macros.first[current_index]);
      continue;
    }
    current_index -= function_macros.second;
    if (current_index < features.second) {
      CxxCompilerInfoData::MacroValue* m =
          compiler_info->mutable_cxx()->add_has_feature();
      m->set_key(features.first[current_index]);
      m->set_value(value);
      continue;
    }
    current_index -= features.second;
    if (current_index < extensions.second) {
      CxxCompilerInfoData::MacroValue* m =
          compiler_info->mutable_cxx()->add_has_extension();
      m->set_key(extensions.first[current_index]);
      m->set_value(value);
      continue;
    }
    current_index -= extensions.second;
    if (current_index < attributes.second) {
      CxxCompilerInfoData::MacroValue* m =
          compiler_info->mutable_cxx()->add_has_attribute();
      m->set_key(attributes.first[current_index]);
      m->set_value(value);
      continue;
    }
    current_index -= attributes.second;
    if (current_index < cpp_attributes.second) {
      CxxCompilerInfoData::MacroValue* m =
          compiler_info->mutable_cxx()->add_has_cpp_attribute();
      m->set_key(cpp_attributes.first[current_index]);
      m->set_value(value);
      continue;
    }
    current_index -= cpp_attributes.second;
    if (current_index < declspec_attributes.second) {
      CxxCompilerInfoData::MacroValue* m =
          compiler_info->mutable_cxx()->add_has_declspec_attribute();
      m->set_key(declspec_attributes.first[current_index]);
      m->set_value(value);
      continue;
    }
    current_index -= declspec_attributes.second;
    if (current_index < builtins.second) {
      CxxCompilerInfoData::MacroValue* m =
          compiler_info->mutable_cxx()->add_has_builtin();
      m->set_key(builtins.first[current_index]);
      m->set_value(value);
      continue;
    }

    // Since we've checked index range, must not reach here.
    LOG(FATAL) << "The number of features exceeds the expected number:"
               << " expected=" << num_all_features << " actual=" << (index - 1);
  }

  if (index != num_all_features) {
    LOG(ERROR) << "The number of features should be "
               << "the expected number:"
               << " expected=" << num_all_features << " actual=" << index
               << " feature_output=" << feature_output;
    CompilerInfoBuilder::AddErrorMessage(
        "goma error: failed to detect clang features.", compiler_info);
    return false;
  }
  return true;
}

/* static */
bool ClangCompilerInfoBuilderHelper::GetPredefinedFeaturesAndExtensions(
    const string& normal_compiler_path,
    const string& lang_flag,
    const std::vector<string>& compiler_info_flags,
    const std::vector<string>& compiler_info_envs,
    const string& cwd,
    CompilerInfoData* compiler_info) {
  std::ostringstream oss;

  int index = 0;

  // Check object-like predefined macros are supported.
  // For example, __FILE__, __LINE__, __COUNTER__, ...
  for (int i = 0; i < kPredefinedObjectMacroSize; ++i) {
    oss << "#ifdef " << kPredefinedObjectMacros[i] << "\n"
        << '#' << ++index << '\n'
        << "1\n"
        << "#else\n";
    oss << '#' << index << '\n'
        << "0\n"
        << "#endif\n";
  }

  // Check function-like predefined macros are supported.
  // __has_include(), __has_feature(), __has_extension(), ...
  for (int i = 0; i < kPredefinedFunctionMacroSize; ++i) {
    oss << "#ifdef " << kPredefinedFunctionMacros[i] << "\n"
        << '#' << ++index << '\n'
        << "1\n"
        << "#else\n";
    oss << '#' << index << '\n'
        << "0\n"
        << "#endif\n";
  }

  // Define predefined macros in case they are not defined.
  oss << "#ifndef __has_feature\n"
      << "# define __has_feature(x) 0\n"
      << "#endif\n"
      << "#ifndef __has_extension\n"
      << "# define __has_extension(x) 0\n"
      << "#endif\n"
      << "#ifndef __has_attribute\n"
      << "# define __has_attribute(x) 0\n"
      << "#endif\n"
      << "#ifndef __has_cpp_attribute\n"
      << "# define __has_cpp_attribute(x) 0\n"
      << "#endif\n"
      << "#ifndef __has_declspec_attribute\n"
      << "# define __has_declspec_attribute(x) 0\n"
      << "#endif\n"
      << "#ifndef __has_builtin\n"
      << "# define __has_builtin(x) 0\n"
      << "#endif\n";

  for (size_t i = 0; i < NUM_KNOWN_FEATURES; i++) {
    // Specify the line number to tell pre-processor to output newlines.
    oss << '#' << ++index << '\n';
    oss << "__has_feature(" << KNOWN_FEATURES[i] << ")\n";
  }
  for (size_t i = 0; i < NUM_KNOWN_EXTENSIONS; i++) {
    // Specify the line number to tell pre-processor to output newlines.
    oss << '#' << ++index << '\n';
    oss << string("__has_extension(") << KNOWN_EXTENSIONS[i] << ")\n";
  }
  for (size_t i = 0; i < NUM_KNOWN_ATTRIBUTES; i++) {
    // Specify the line number to tell pre-processor to output newlines.
    oss << '#' << ++index << '\n';
    oss << string("__has_attribute(") << KNOWN_ATTRIBUTES[i] << ")\n";
  }
  // If the attributes has "::", gcc fails in C-mode,
  // but works on C++ mode. So, when "::" is detected, we ignore it in C mode.
  // :: can be used like "clang::", "gsl::"
  for (size_t i = 0; i < NUM_KNOWN_CPP_ATTRIBUTES; i++) {
    // Specify the line number to tell pre-processor to output newlines.
    oss << '#' << ++index << '\n';
    if (lang_flag == "-xc++" ||
        strchr(KNOWN_CPP_ATTRIBUTES[i], ':') == nullptr) {
      oss << "__has_cpp_attribute(" << KNOWN_CPP_ATTRIBUTES[i] << ")\n";
    } else {
      oss << "0\n";
    }
  }
  for (size_t i = 0; i < NUM_KNOWN_DECLSPEC_ATTRIBUTES; i++) {
    oss << '#' << ++index << '\n';
    oss << "__has_declspec_attribute(" << KNOWN_DECLSPEC_ATTRIBUTES[i] << ")\n";
  }
  for (size_t i = 0; i < NUM_KNOWN_BUILTINS; i++) {
    oss << '#' << ++index << '\n';
    oss << "__has_builtin(" << KNOWN_BUILTINS[i] << ")\n";
  }

  const string& source = oss.str();
  VLOG(1) << "source=" << source;

  ScopedTmpFile tmp_file("goma_compiler_proxy_check_features_");
  if (!tmp_file.valid()) {
    PLOG(ERROR) << "failed to make temp file: " << tmp_file.filename();
    CompilerInfoBuilder::AddErrorMessage(
        "goma error: failed to create a temp. file.", compiler_info);
    return false;
  }

  ssize_t written = tmp_file.Write(source.data(), source.size());
  if (static_cast<ssize_t>(source.size()) != written) {
    PLOG(ERROR) << "Failed to write source into " << tmp_file.filename() << ": "
                << source.size() << " vs " << written;
    CompilerInfoBuilder::AddErrorMessage(
        "goma error: failed to write a temp file.", compiler_info);
    return false;
  }
  // We do not need to append data to |tmp_file|.
  // Keeping it opened may cause a trouble on Windows.
  // Note: |tmp_file.filename()| is kept until the end of the scope.
  if (!tmp_file.Close()) {
    PLOG(ERROR) << "failed to close temp file: " << tmp_file.filename();
    CompilerInfoBuilder::AddErrorMessage(
        "goma error: failed to close a temp. file.", compiler_info);
    return false;
  }

  std::vector<string> argv;
  argv.push_back(normal_compiler_path);
  copy(compiler_info_flags.begin(), compiler_info_flags.end(),
       back_inserter(argv));
  argv.push_back(lang_flag);
  argv.push_back("-E");
  argv.push_back(tmp_file.filename());

  std::vector<string> env;
  env.push_back("LC_ALL=C");
  copy(compiler_info_envs.begin(), compiler_info_envs.end(),
       back_inserter(env));

  int32_t status = 0;
  string out;
  {
    GOMA_COUNTERZ("ReadCommandOutput(predefined features)");
    out = ReadCommandOutput(normal_compiler_path, argv, env, cwd, STDOUT_ONLY,
                            &status);
  }
  VLOG(1) << "out=" << out;
  LOG_IF(ERROR, status != 0)
      << "Read of features and extensions did not ends with status 0."
      << " normal_compiler_path=" << normal_compiler_path
      << " status=" << status << " argv=" << argv << " env=" << env
      << " cwd=" << cwd << " out=" << out;
  if (status != 0) {
    return false;
  }

  FeatureList object_macros =
      std::make_pair(kPredefinedObjectMacros, kPredefinedObjectMacroSize);
  FeatureList function_macros =
      std::make_pair(kPredefinedFunctionMacros, kPredefinedFunctionMacroSize);
  FeatureList features = std::make_pair(KNOWN_FEATURES, NUM_KNOWN_FEATURES);
  FeatureList extensions =
      std::make_pair(KNOWN_EXTENSIONS, NUM_KNOWN_EXTENSIONS);
  FeatureList attributes =
      std::make_pair(KNOWN_ATTRIBUTES, NUM_KNOWN_ATTRIBUTES);
  FeatureList cpp_attributes =
      std::make_pair(KNOWN_CPP_ATTRIBUTES, NUM_KNOWN_CPP_ATTRIBUTES);
  FeatureList declspec_attributes =
      std::make_pair(KNOWN_DECLSPEC_ATTRIBUTES, NUM_KNOWN_DECLSPEC_ATTRIBUTES);
  FeatureList builtins = std::make_pair(KNOWN_BUILTINS, NUM_KNOWN_BUILTINS);

  return ParseFeatures(out, object_macros, function_macros, features,
                       extensions, attributes, cpp_attributes,
                       declspec_attributes, builtins, compiler_info);
}

// Return true if everything is fine, and all necessary information
// (system include paths, predefined macro, etc) are set to |compiler_info|.
// Otherwise false, and |compiler_info->error_message| is set.
//
// |local_compiler_path| is compiler path.
// |compiler_info_flags| is used as command line options to get info.
// |compiler_info_envs| is used as environment to get info.
// |cwd| is current working directory on getting info.
// |lang_flag| specifies the language to get predefined macros and features.
// e.g. clang -dM |lang_flag| -E /dev/null
// It is usually -xc and -xc++ on gcc variants, but there are also other
// languages such as c-header, cpp-output.  We should use it.
// Currently, Objective-C++ and C++ need to be treated as C++, and
// I believe CompilerFlags should be point of decision of is_cplusplus
// judgement.  For that reason, |is_cplusplus| is passed regardless
// of what lang_flag is used.
// |is_clang| indicates the compiler is clang, so need to get features,
// extensions etc.
// static
bool ClangCompilerInfoBuilderHelper::SetBasicCompilerInfo(
    const string& local_compiler_path,
    const std::vector<string>& compiler_info_flags,
    const std::vector<string>& compiler_info_envs,
    const string& cwd,
    const string& lang_flag,
    const string& resource_dir,
    bool is_cplusplus,
    bool has_nostdinc,
    CompilerInfoData* compiler_info) {
  // cxx_lang_flag, c_lang_flag for c++, c respectively.
  // For gcc and clang,
  // even when language is objective-c, objective-c++, c-header, cpp-output,
  // c++-header, c++-cpp-output, we'll use -xc++, -xc to get system include
  // paths.
  // For clang-cl.exe, we use /TP and /TC like we do for gcc and clang.
  string cxx_lang_flag;
  string c_lang_flag;
  if (VCFlags::IsClangClCommand(local_compiler_path)) {
    cxx_lang_flag = "/TP";
    c_lang_flag = "/TC";
  } else {
    cxx_lang_flag = "-xc++";
    c_lang_flag = "-xc";
  }

  // We assumes include system paths are same for given compiler_info_flags
  // and compiler_info_envs.
  //
  // We changes the way to get system include path whether it is compiling C++
  // source code or not.
  // C++:
  //   c++ system include path = [paths by -xc++]
  //   c   system include path = [paths by -xc++ -nostdinc++]
  // C:
  //   c   system include path = [paths by -xc]
  //   no need to check C++ system include path.
  //
  // Note that the way to get system include paths are still under discussion
  // in b/13178705.
  string c_output, cxx_output;
  if (is_cplusplus) {
    int32_t status;
    cxx_output =
        GccDisplayPrograms(local_compiler_path, compiler_info_flags,
                           compiler_info_envs, cxx_lang_flag, "", cwd, &status);
    if (status != 0) {
      CompilerInfoBuilder::AddErrorMessage(
          "Failed to execute compiler to get c++ system "
          "include paths for " +
              local_compiler_path,
          compiler_info);
      LOG(ERROR) << compiler_info->error_message() << " status=" << status
                 << " cxx_output=" << cxx_output;
      return false;
    }
    c_output = GccDisplayPrograms(local_compiler_path, compiler_info_flags,
                                  compiler_info_envs, cxx_lang_flag,
                                  "-nostdinc++", cwd, &status);
    if (status != 0) {
      CompilerInfoBuilder::AddErrorMessage(
          "Failed to execute compiler to get c system "
          "include paths for " +
              local_compiler_path,
          compiler_info);
      LOG(ERROR) << compiler_info->error_message() << " status=" << status
                 << " c_output=" << c_output;
      return false;
    }
  } else {
    int32_t status;
    c_output =
        GccDisplayPrograms(local_compiler_path, compiler_info_flags,
                           compiler_info_envs, c_lang_flag, "", cwd, &status);
    if (status != 0) {
      CompilerInfoBuilder::AddErrorMessage(
          "Failed to execute compiler to get c system "
          "include paths for " +
              local_compiler_path,
          compiler_info);
      LOG(ERROR) << compiler_info->error_message() << " status=" << status
                 << " c_output=" << c_output;
      return false;
    }
  }

  if (!GetSystemIncludePaths(local_compiler_path, compiler_info_flags,
                             compiler_info_envs, cxx_output, c_output,
                             is_cplusplus, has_nostdinc, compiler_info)) {
    CompilerInfoBuilder::AddErrorMessage(
        "Failed to get system include paths for " + local_compiler_path,
        compiler_info);
    LOG(ERROR) << compiler_info->error_message();
    return false;
  }
  if (!GetPredefinedMacros(local_compiler_path, compiler_info_flags,
                           compiler_info_envs, cwd, lang_flag, compiler_info)) {
    CompilerInfoBuilder::AddErrorMessage(
        "Failed to get predefined macros for " + local_compiler_path,
        compiler_info);
    LOG(ERROR) << compiler_info->error_message();
    return false;
  }

  if (!c_output.empty()) {
    std::vector<ClangCompilerInfoBuilderHelper::ResourceList> resource;
    if (ClangCompilerInfoBuilderHelper::ParseResourceOutput(
            local_compiler_path, cwd, c_output, &resource) ==
        ClangCompilerInfoBuilderHelper::ParseStatus::kFail) {
      CompilerInfoBuilder::AddErrorMessage(
          "Failed to get resource output for " + local_compiler_path,
          compiler_info);
      LOG(ERROR) << compiler_info->error_message();
      return false;
    }
    if (!UpdateResourceInfo(cwd, resource, compiler_info)) {
      CompilerInfoBuilder::AddErrorMessage(
          "Failed to set resource output for " + local_compiler_path,
          compiler_info);
      LOG(ERROR) << compiler_info->error_message();
      return false;
    }

    bool need_clang_resource = false;
    for (const auto& r : compiler_info->resource()) {
      if (r.type() == CompilerInfoData::CLANG_RESOURCE) {
        need_clang_resource = true;
        break;
      }
    }
    if (need_clang_resource) {
      if (!ClangCompilerInfoBuilderHelper::GetResourceDir(c_output,
                                                          compiler_info)) {
        CompilerInfoBuilder::AddErrorMessage(
            "Failed to get resource dir for " + local_compiler_path,
            compiler_info);
        LOG(ERROR) << compiler_info->error_message();
        return false;
      }
      if (resource_dir.empty()) {
        compiler_info->add_additional_flags(
            "-resource-dir=" + compiler_info->cxx().resource_dir());
      } else if (resource_dir != compiler_info->cxx().resource_dir()) {
        LOG(WARNING) << "user specified non default -resource-dir:"
                     << " default=" << compiler_info->cxx().resource_dir()
                     << " user=" << resource_dir;
      }
    }
  }

  if (!GetPredefinedFeaturesAndExtensions(
          local_compiler_path, lang_flag, compiler_info_flags,
          compiler_info_envs, cwd, compiler_info)) {
    CompilerInfoBuilder::AddErrorMessage(
        "failed to get predefined features and extensions for " +
            local_compiler_path,
        compiler_info);
    LOG(ERROR) << "Failed to get predefined features and extensions."
               << " local_compiler_path=" << local_compiler_path
               << " lang_flag=" << lang_flag;
    DCHECK(compiler_info->has_error_message());
    return false;
  }
  return true;
}

// static
bool ClangCompilerInfoBuilderHelper::GetSystemIncludePaths(
    const string& normal_compiler_path,
    const std::vector<string>& compiler_info_flags,
    const std::vector<string>& compiler_info_envs,
    const string& cxx_display_output,
    const string& c_display_output,
    bool is_cplusplus,
    bool has_nostdinc,
    CompilerInfoData* compiler_info) {
  compiler_info->mutable_cxx()->clear_quote_include_paths();
  compiler_info->mutable_cxx()->clear_cxx_system_include_paths();
  compiler_info->mutable_cxx()->clear_system_include_paths();
  compiler_info->mutable_cxx()->clear_system_framework_paths();

  std::vector<string> quote_include_paths;
  std::vector<string> cxx_system_include_paths;
  std::vector<string> system_framework_paths;
  if (cxx_display_output.empty() ||
      !SplitGccIncludeOutput(cxx_display_output, &quote_include_paths,
                             &cxx_system_include_paths,
                             &system_framework_paths)) {
    LOG_IF(WARNING, is_cplusplus)
        << "Cannot detect g++ system include paths:"
        << " normal_compiler_path=" << normal_compiler_path
        << " compiler_info_flags=" << compiler_info_flags
        << " compiler_info_envs=" << compiler_info_envs;
  }

  UpdateIncludePaths(
      quote_include_paths,
      compiler_info->mutable_cxx()->mutable_quote_include_paths());

  UpdateIncludePaths(
      cxx_system_include_paths,
      compiler_info->mutable_cxx()->mutable_cxx_system_include_paths());

  UpdateIncludePaths(
      system_framework_paths,
      compiler_info->mutable_cxx()->mutable_system_framework_paths());

  std::vector<string>* quote_include_paths_ptr = nullptr;
  // If quote_include_paths couldn't be obtained above,
  // we'll try to fetch them here.
  if (compiler_info->cxx().quote_include_paths_size() == 0) {
    DCHECK(quote_include_paths.empty());
    quote_include_paths_ptr = &quote_include_paths;
  }

  std::vector<string>* framework_paths_ptr = nullptr;
  // If system_framework_paths couldn't be obtained above,
  // we'll try to fetch them here.
  if (compiler_info->cxx().system_framework_paths_size() == 0) {
    DCHECK(system_framework_paths.empty());
    framework_paths_ptr = &system_framework_paths;
  }
  std::vector<string> system_include_paths;
  if (!SplitGccIncludeOutput(c_display_output, quote_include_paths_ptr,
                             &system_include_paths, framework_paths_ptr)) {
    LOG(WARNING) << "Cannot detect gcc system include paths:"
                 << " normal_compiler_path=" << normal_compiler_path
                 << " compiler_info_flags=" << compiler_info_flags
                 << " compiler_info_envs=" << compiler_info_envs;
  }
  if (quote_include_paths_ptr != nullptr) {
    UpdateIncludePaths(
        quote_include_paths,
        compiler_info->mutable_cxx()->mutable_quote_include_paths());
  }

  UpdateIncludePaths(
      system_include_paths,
      compiler_info->mutable_cxx()->mutable_system_include_paths());

  if (framework_paths_ptr != nullptr) {
    UpdateIncludePaths(
        system_framework_paths,
        compiler_info->mutable_cxx()->mutable_system_framework_paths());
  }

  if (compiler_info->cxx().cxx_system_include_paths_size() == 0 &&
      compiler_info->cxx().system_include_paths_size() == 0 && !has_nostdinc) {
    std::stringstream ss;
    ss << "Cannot detect system include paths:"
       << " normal_compiler_path=" << normal_compiler_path
       << " compiler_info_flags=" << compiler_info_flags
       << " compiler_info_envs=" << compiler_info_envs
       << " cxx_display_output=" << cxx_display_output
       << " c_display_output=" << c_display_output;
    CompilerInfoBuilder::AddErrorMessage(ss.str(), compiler_info);
    LOG(ERROR) << ss.str();
    return false;
  }

#ifdef _WIN32
  // In the (build: Windows, target: NaCl (not PNaCl)) compile,
  // include paths under toolchain root are shown as relative path from it.
  if (GCCFlags::IsNaClGCCCommand(normal_compiler_path)) {
    compiler_info->mutable_cxx()->set_toolchain_root(
        GetNaClToolchainRoot(normal_compiler_path));
  }
#endif

  return true;
}

/* static */
bool ClangCompilerInfoBuilderHelper::SplitGccIncludeOutput(
    const string& gcc_v_output,
    std::vector<string>* qpaths,
    std::vector<string>* paths,
    std::vector<string>* framework_paths) {
  // TODO: use absl::string_view for gcc_v_output etc.

  static const string kQStartMarker("#include \"...\" search starts here:");
  static const string kStartMarker("#include <...> search starts here:");
  static const string kEndMarker("End of search list.");
  size_t qstart_pos = gcc_v_output.find(kQStartMarker);
  size_t start_pos = gcc_v_output.find(kStartMarker);
  size_t end_pos = gcc_v_output.find(kEndMarker);
  if (qstart_pos == string::npos || start_pos == string::npos ||
      end_pos == string::npos) {
    // something is wrong with output from gcc.
    LOG(WARNING) << "gcc output is wrong. " << gcc_v_output;
    return false;
  }
  if (qpaths != nullptr) {
    string gcc_v_qsearch_paths(
        gcc_v_output.substr(qstart_pos + kQStartMarker.size(),
                            start_pos - qstart_pos - kQStartMarker.size()));
    VLOG(2) << "extracted qsearch paths [" << gcc_v_qsearch_paths << "]";
    qpaths->clear();
    for (auto&& split_qpath : absl::StrSplit(
             gcc_v_qsearch_paths, absl::ByAnyChar("\r\n"), absl::SkipEmpty())) {
      absl::string_view qpath = absl::StripAsciiWhitespace(split_qpath);
      if (!qpath.empty()) {
        qpaths->emplace_back(string(qpath));
      }
    }
  }

  string gcc_v_search_paths(
      gcc_v_output.substr(start_pos + kStartMarker.size(),
                          end_pos - start_pos - kStartMarker.size()));
  VLOG(2) << "extracted search paths [" << gcc_v_search_paths << "]";
  paths->clear();
  for (auto&& split_path : absl::StrSplit(
           gcc_v_search_paths, absl::ByAnyChar("\r\n"), absl::SkipEmpty())) {
    absl::string_view path = absl::StripAsciiWhitespace(split_path);
    if (!path.empty()) {
      static const char* kFrameworkMarker = "(framework directory)";
      if (absl::EndsWith(path, kFrameworkMarker)) {
        if (framework_paths) {
          path.remove_suffix(strlen(kFrameworkMarker));
          path = absl::StripAsciiWhitespace(path);
          framework_paths->emplace_back(path);
        }
      } else {
        paths->emplace_back(path);
      }
    }
  }
  return true;
}

// static
void ClangCompilerInfoBuilderHelper::UpdateIncludePaths(
    const std::vector<string>& paths,
    google::protobuf::RepeatedPtrField<string>* include_paths) {
  std::copy(paths.cbegin(), paths.cend(),
            google::protobuf::RepeatedFieldBackInserter(include_paths));
}

#ifdef _WIN32
// static
string ClangCompilerInfoBuilderHelper::GetNaClToolchainRoot(
    const string& normal_nacl_gcc_path) {
  return PathResolver::ResolvePath(
      file::JoinPath(file::Dirname(normal_nacl_gcc_path), ".."));
}
#endif

}  // namespace devtools_goma
