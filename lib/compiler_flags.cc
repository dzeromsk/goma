// Copyright 2010 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.



#include "compiler_flags.h"

#include <ctype.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <sstream>

#include "absl/strings/match.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "cmdline_parser.h"
#include "file.h"
#include "file_helper.h"
#include "flag_parser.h"
#include "glog/logging.h"
#include "glog/stl_logging.h"
#include "known_warning_options.h"
#include "path.h"
#include "path_resolver.h"
#include "path_util.h"
using std::string;

namespace devtools_goma {

namespace {

enum FlagType {
  kNormal,  // A flag will be added with AddFlag
  kPrefix,  // A flag will be added with AddPrefixFlag
  kBool,    // A flag will be added with AddBoolFlag
};

// Normalize paths surrounded by '"' to paths without it.
// e.g. "c:\Windows\Program Files" -> c:\Windows\Program Files.
string NormalizeWin32Path(absl::string_view path) {
  // TODO: omit orphan '"' at the end of path?
  if (absl::StartsWith(path, "\"")) {
    if (absl::EndsWith(path, "\"")) {
      path = path.substr(1, path.length() - 2);
    } else {
      path = path.substr(1);
    }
  }
  return string(path);
}

string ToNormalizedBasename(absl::string_view in) {
  // Manual file::Basename.
  // Note file::Basename does not understand "\\" as a path delimiter
  // on non-Windows.
  absl::string_view::size_type last_sep = in.find_last_of("/\\");
  if (last_sep != absl::string_view::npos) {
    in.remove_prefix(last_sep + 1);
  }
  string out = string(in);
  std::transform(out.begin(), out.end(), out.begin(), ::tolower);
  return out;
}

}  // namespace

/* static */
std::unique_ptr<CompilerFlags> CompilerFlags::New(
    const std::vector<string>& args, const string& cwd) {
  if (args.empty()) {
    LOG(ERROR) << "Empty args";
    return nullptr;
  }
  if (IsGCCCommand(args[0])) {
    return std::unique_ptr<CompilerFlags>(new GCCFlags(args, cwd));
  } else if (IsVCCommand(args[0]) || IsClangClCommand(args[0])) {
    // clang-cl gets compatible options with cl.exe.
    // See Also: http://clang.llvm.org/docs/UsersManual.html#clang-cl
    return std::unique_ptr<CompilerFlags>(new VCFlags(args, cwd));
  } else if (IsJavacCommand(args[0])) {
    return std::unique_ptr<CompilerFlags>(new JavacFlags(args, cwd));
  } else if (IsClangTidyCommand(args[0])) {
    return std::unique_ptr<CompilerFlags>(new ClangTidyFlags(args, cwd));
  } else if (IsJavaCommand(args[0])) {
    return std::unique_ptr<CompilerFlags>(new JavaFlags(args, cwd));
  }

  LOG(WARNING) << "Unknown command: " << args[0];
  return nullptr;
}

/* static */
std::unique_ptr<CompilerFlags> CompilerFlags::MustNew(
    const std::vector<string>& args, const string& cwd) {
  std::unique_ptr<CompilerFlags> flags = CompilerFlags::New(args, cwd);
  LOG_IF(FATAL, !flags) << "unsupported command line:" << args;
  return flags;
}

CompilerFlags::CompilerFlags(const std::vector<string>& args, const string& cwd)
    : args_(args), cwd_(cwd), is_successful_(false) {
  CHECK(!args.empty());
  compiler_name_ = args[0];
}

// TODO: wtf
void CompilerFlags::Fail(const string& msg, const std::vector<string>& args) {
  fail_message_ = "Flag parsing failed: " + msg + "\n";
  fail_message_ += "ARGS:\n";
  for (const auto& arg : args) {
    fail_message_ += " " + arg;
  }
  fail_message_ += "\n";
  is_successful_ = false;
}

// static
bool CompilerFlags::ExpandPosixArgs(
    const string& cwd, const std::vector<string>& args,
    std::vector<string>* expanded_args,
    std::vector<string>* optional_input_filenames) {
  for (size_t i = 0; i < args.size(); ++i) {
    const string& arg = args[i];
    bool need_expand = false;
    if (absl::StartsWith(arg, "@")) {
      need_expand = true;

      // MacOSX uses @executable_path, @loader_path or @rpath as prefix
      // of install_name (b/6845420).
      // It could also be a linker rpath (b/31920050).
      bool is_linker_magic_token = false;
      if (absl::StartsWith(arg, "@executable_path/") ||
           absl::StartsWith(arg, "@loader_path/") ||
           absl::StartsWith(arg, "@rpath/")) {
        is_linker_magic_token = true;
      }
      if (is_linker_magic_token &&
          i > 0 &&
          (args[i - 1] == "-rpath" || args[i - 1] == "-install_name")) {
          need_expand = false;
      }
      if (is_linker_magic_token &&
          i > 2 &&
          args[i - 3] == "-Xlinker" &&
          (args[i - 2] == "-rpath" || args[i - 2] == "-install_name") &&
          args[i - 1] == "-Xlinker") {
          need_expand = false;
      }
    }
    if (!need_expand) {
      expanded_args->push_back(arg);
      continue;
    }
    const string& source_list_filename =
        PathResolver::PlatformConvert(arg.substr(1));
    string source_list;
    if (!ReadFileToString(
            file::JoinPathRespectAbsolute(cwd, source_list_filename),
            &source_list)) {
      LOG(WARNING) << "failed to read: " << source_list_filename
                   << " at " << cwd;
      return false;
    }
    if (optional_input_filenames) {
      optional_input_filenames->push_back(source_list_filename);
    }

    if (!ParsePosixCommandLineToArgv(source_list, expanded_args)) {
      LOG(WARNING) << "failed to parse command line: " << source_list;
      return false;
    }
    VLOG(1) << "expanded_args:" << *expanded_args;
  }
  return true;
}

// Return the base name of compiler, such as 'x86_64-linux-gcc-4.3',
// 'g++', derived from compiler_name.
string CompilerFlags::compiler_base_name() const {
  string compiler_base_name = compiler_name_;
  size_t found_slash = compiler_base_name.rfind('/');
  if (found_slash != string::npos) {
    compiler_base_name = compiler_base_name.substr(found_slash + 1);
  }
  return compiler_base_name;
}

/* static */
bool CompilerFlags::IsGCCCommand(absl::string_view arg) {
  const absl::string_view stem = file::Stem(arg);
  if (stem.find("gcc") != absl::string_view::npos ||
      stem.find("g++") != absl::string_view::npos)
    return true;
  // As a substring "cc" would be found even in other commands such
  // as "distcc", we check if the name is "cc" or "*-cc"
  // (e.g., "i586-mingw32msvc-cc").
  if (stem == "c++" ||
      stem == "cc" || absl::EndsWith(arg, "-cc"))
    return true;
  if (IsClangCommand(arg))
    return true;
  return false;
}

/* static */
bool CompilerFlags::IsClangCommand(absl::string_view arg) {
  const absl::string_view stem = file::Stem(arg);
  // allow pnacl-clang etc.
  // However, don't allow clang-tidy.
  if (stem == "clang" || stem == "clang++" ||
      absl::EndsWith(stem, "-clang") ||
      absl::EndsWith(stem, "-clang++"))
    return true;

  // For b/25937763 but we should not consider the followings as clang:
  // clang-cl, clang-check, clang-tblgen, clang-format, clang-tidy-diff, etc.
  static const char kClang[] = "clang-";
  static const char kClangxx[] = "clang++-";
  absl::string_view version = stem;
  if (absl::StartsWith(stem, kClang))
    version.remove_prefix(sizeof(kClang) - 1);
  else if (absl::StartsWith(stem, kClangxx))
    version.remove_prefix(sizeof(kClangxx) - 1);
  if (stem == version)
    return false;
  // version should only have digits and '.'.
  return version.find_first_not_of("0123456789.") == absl::string_view::npos;
}

/* static */
bool CompilerFlags::IsNaClGCCCommand(absl::string_view arg) {
  const absl::string_view basename = file::Basename(arg);
  return basename.find("nacl-gcc") != absl::string_view::npos ||
         basename.find("nacl-g++") != absl::string_view::npos;
}

/* static */
bool CompilerFlags::IsVCCommand(absl::string_view arg) {
  // As a substring "cl" would be found in other commands like "clang" or
  // "nacl-gcc".  Also, "cl" is case-insensitive on Windows and can be postfixed
  // with ".exe".
  const string& s = ToNormalizedBasename(arg);
  return s == "cl.exe" || s == "cl";
}

/* static */
bool CompilerFlags::IsClangClCommand(absl::string_view arg) {
  const string& s = ToNormalizedBasename(arg);
  return s == "clang-cl.exe" || s == "clang-cl";
}

/* static */
bool CompilerFlags::IsPNaClClangCommand(absl::string_view arg) {
  const absl::string_view stem = file::Stem(arg);
  return stem == "pnacl-clang" || stem == "pnacl-clang++";
}

/* static */
bool CompilerFlags::IsJavacCommand(absl::string_view arg) {
  const absl::string_view basename = file::Basename(arg);
  return basename.find("javac") != absl::string_view::npos;
}

/* static */
bool CompilerFlags::IsClangTidyCommand(absl::string_view arg) {
  const string& s = ToNormalizedBasename(arg);
  return s == "clang-tidy";
}

/* static */
bool CompilerFlags::IsJavaCommand(absl::string_view arg) {
  const absl::string_view stem = file::Stem(arg);
  return stem == "java";
}

/* static */
string CompilerFlags::GetCompilerName(absl::string_view arg) {
  if (IsGCCCommand(arg)) {
    return GCCFlags::GetCompilerName(arg);
  } else if (IsVCCommand(arg) || IsClangClCommand(arg)) {
    return VCFlags::GetCompilerName(arg);
  } else if (IsJavacCommand(arg)) {
    return JavacFlags::GetCompilerName(arg);
  } else if (IsClangTidyCommand(arg)) {
    return ClangTidyFlags::GetCompilerName(arg);
  } else if (IsJavaCommand(arg)) {
    return JavaFlags::GetCompilerName(arg);
  }
  return "";
}

string CompilerFlags::DebugString() const {
  std::stringstream ss;
  for (const auto& arg : args_) {
    ss << arg << " ";
  }
  if (!expanded_args_.empty() && args_ != expanded_args_) {
    ss << " -> ";
    for (const auto& arg : expanded_args_) {
      ss << arg << " ";
    }
  }
  return ss.str();
}

void CompilerFlags::GetClientImportantEnvs(
    const char** envp, std::vector<string>* out_envs) const {
  for (const char** e = envp; *e; e++) {
    if (IsClientImportantEnv(*e)) {
      out_envs->push_back(*e);
    }
  }
}

void CompilerFlags::GetServerImportantEnvs(
    const char** envp, std::vector<string>* out_envs) const {
  for (const char** e = envp; *e; e++) {
    if (IsServerImportantEnv(*e)) {
      out_envs->push_back(*e);
    }
  }
}

template <bool is_defined>
class MacroStore : public FlagParser::Callback {
 public:
  explicit MacroStore(std::vector<std::pair<string, bool> >* macros)
      : macros_(macros) {}

  // Returns parsed flag value of value for flag.
  string ParseFlagValue(const FlagParser::Flag& /* flag */,
                        const string& value) override {
    macros_->push_back(std::make_pair(value, is_defined));
    return value;
  }

 private:
  std::vector<std::pair<string, bool> >* macros_;
};

/* static */
string GCCFlags::GetCompilerName(absl::string_view arg) {
  absl::string_view name = file::Basename(arg);
  if (name.find("clang++") != string::npos) {
    return "clang++";
  } else if (name.find("clang") != string::npos) {
    return "clang";
  } else if (name.find("g++") != string::npos || name == "c++") {
    return "g++";
  } else {
    return "gcc";
  }
}

// Return the key 'gcc' or 'g++' with architecture and version
// stripped from compiler_name.
string GCCFlags::compiler_name() const {
  return GetCompilerName(compiler_name_);
}

GCCFlags::GCCFlags(const std::vector<string>& args, const string& cwd)
    : CompilerFlags(args, cwd),
      is_cplusplus_(false),
      has_nostdinc_(false),
      has_no_integrated_as_(false),
      has_pipe_(false),
      has_ffreestanding_(false),
      has_fno_hosted_(false),
      has_fno_sanitize_blacklist_(false),
      has_fsyntax_only_(false),
      has_wrapper_(false),
      has_fplugin_(false),
      is_precompiling_header_(false),
      is_stdin_input_(false) {
  if (!CompilerFlags::ExpandPosixArgs(cwd, args,
                                      &expanded_args_,
                                      &optional_input_filenames_)) {
    Fail("Unable to expand args", args);
    return;
  }
  bool has_at_file = !optional_input_filenames_.empty();
  bool no_integrated_as = false;
  bool fno_integrated_as = false;
  bool ffreestanding = false;
  bool fno_hosted = false;
  bool fsyntax_only = false;
  bool print_file_name = false;

  FlagParser parser;
  DefineFlags(&parser);

  FlagParser::Flag* flag_c = parser.AddBoolFlag("c");
  FlagParser::Flag* flag_S = parser.AddBoolFlag("S");
  FlagParser::Flag* flag_E = parser.AddBoolFlag("E");
  FlagParser::Flag* flag_M = parser.AddBoolFlag("M");
  FlagParser::Flag* flag_MD = parser.AddBoolFlag("MD");
  FlagParser::Flag* flag_MMD = parser.AddBoolFlag("MMD");
  FlagParser::Flag* flag_g = parser.AddPrefixFlag("g");
  parser.AddBoolFlag("nostdinc")->SetSeenOutput(&has_nostdinc_);
  parser.AddBoolFlag("nostdinc++")->SetOutput(&compiler_info_flags_);
  parser.AddBoolFlag("nostdlibinc")->SetOutput(&compiler_info_flags_);
  parser.AddBoolFlag("integrated-as")->SetOutput(&compiler_info_flags_);
  parser.AddBoolFlag("no-integrated-as")->SetSeenOutput(
      &no_integrated_as);
  parser.AddBoolFlag("fno-integrated-as")->SetSeenOutput(
      &fno_integrated_as);
  parser.AddBoolFlag("pipe")->SetSeenOutput(&has_pipe_);
  parser.AddBoolFlag("-pipe")->SetSeenOutput(&has_pipe_);
  parser.AddBoolFlag("ffreestanding")->SetSeenOutput(&ffreestanding);
  parser.AddBoolFlag("fno-hosted")->SetSeenOutput(&fno_hosted);
  parser.AddBoolFlag("fsyntax-only")->SetSeenOutput(&fsyntax_only);
  parser.AddBoolFlag("print-file-name")->SetSeenOutput(&print_file_name);
  parser.AddBoolFlag("-print-file-name")->SetSeenOutput(&print_file_name);
  FlagParser::Flag* flag_x = parser.AddFlag("x");
  FlagParser::Flag* flag_o = parser.AddFlag("o");
  FlagParser::Flag* flag_MF = parser.AddFlag("MF");
  FlagParser::Flag* flag_isysroot = parser.AddFlag("isysroot");
  // TODO: Consider split -fprofile-* flags? Some options take an extra
  // arguement, other do not. Merging such kind of flags do not look good.
  FlagParser::Flag* flag_fprofile = parser.AddPrefixFlag("fprofile-");
  FlagParser::Flag* flag_fprofile_sample_use =
      parser.AddFlag("fprofile-sample-use");
  FlagParser::Flag* flag_fthinlto_index =
      parser.AddPrefixFlag("fthinlto-index=");

  parser.AddFlag("wrapper")->SetSeenOutput(&has_wrapper_);
  parser.AddPrefixFlag("fplugin=")->SetSeenOutput(&has_fplugin_);

  // -mllvm takes extra arg.
  // ASAN uses -mllvm -asan-blacklist=$FILE
  // TSAN uses -mllvm -tsan-blacklist=$FILE
  std::vector<string> llvm_options;
  parser.AddFlag("mllvm")->SetOutput(&llvm_options);
  FlagParser::Flag* flag_fsanitize_blacklist =
      parser.AddFlag("fsanitize-blacklist");
  FlagParser::Flag* flag_fsanitize = parser.AddFlag("fsanitize");
  flag_fsanitize->SetOutput(&compiler_info_flags_);

  // TODO: follow -fno-sanitize-blacklist spec.
  // http://clang.llvm.org/docs/UsersManual.html:
  // > -fno-sanitize-blacklist: don't use blacklist file,
  // > if it was specified *earlier in the command line*.
  parser.AddFlag("fno_sanitize_blacklist")->SetSeenOutput(
      &has_fno_sanitize_blacklist_);

  FlagParser::Flag* flag_resource_dir = parser.AddFlag("resource-dir");
  flag_resource_dir->SetOutput(&compiler_info_flags_);

  FlagParser::Flag* flag_fdebug_prefix_map =
      parser.AddFlag("fdebug-prefix-map");
  FlagParser::Flag* flag_gsplit_dwarf = parser.AddBoolFlag("gsplit-dwarf");
  flag_gsplit_dwarf->SetOutput(&compiler_info_flags_);

  parser.AddFlag("m")->SetOutput(&compiler_info_flags_);
  parser.AddFlag("arch")->SetOutput(&compiler_info_flags_);
  parser.AddFlag("target")->SetOutput(&compiler_info_flags_);
  parser.AddFlag("-target")->SetOutput(&compiler_info_flags_);
  parser.AddFlag("gcc-toolchain")->SetOutput(&compiler_info_flags_);
  parser.AddFlag("-gcc-toolchain")->SetOutput(&compiler_info_flags_);
  // TODO: Uncomment this and remove isysroot_ once we stop
  //               supporting API version 0.
  // parser.AddFlag("isysroot")->SetOutput(&compiler_info_flags_);
  parser.AddFlag("imultilib")->SetOutput(&compiler_info_flags_);
  parser.AddFlag("isystem")->SetOutput(&compiler_info_flags_);
  parser.AddFlag("iquote")->SetOutput(&compiler_info_flags_);
  parser.AddFlag("idirafter")->SetOutput(&compiler_info_flags_);
  parser.AddFlag("-sysroot")->SetOutput(&compiler_info_flags_);
  parser.AddFlag("B")->SetOutput(&compiler_info_flags_);
  parser.AddFlag("iframework")->SetOutput(&compiler_info_flags_);
  parser.AddPrefixFlag("O")->SetOutput(&compiler_info_flags_);
  parser.AddFlag("b")->SetOutput(&compiler_info_flags_);
  parser.AddFlag("V")->SetOutput(&compiler_info_flags_);
  parser.AddFlag("specs")->SetOutput(&compiler_info_flags_);
  parser.AddFlag("-specs")->SetOutput(&compiler_info_flags_);
  parser.AddFlag("std")->SetOutput(&compiler_info_flags_);
  parser.AddFlag("-std")->SetOutput(&compiler_info_flags_);
  parser.AddPrefixFlag("f")->SetOutput(&compiler_info_flags_);
  parser.AddBoolFlag("pthread")->SetOutput(&compiler_info_flags_);
  parser.AddBoolFlag("undef")->SetOutput(&compiler_info_flags_);
  // If pnacl-clang, it need to support --pnacl-bias and --pnacl-*-bias.
  // See: b/17982273
  if (IsPNaClClangCommand(compiler_base_name())) {
    parser.AddPrefixFlag("-pnacl-bias=")->SetOutput(&compiler_info_flags_);
    parser.AddBoolFlag("-pnacl-arm-bias")->SetOutput(&compiler_info_flags_);
    parser.AddBoolFlag("-pnacl-mips-bias")->SetOutput(&compiler_info_flags_);
    parser.AddBoolFlag("-pnacl-i686-bias")->SetOutput(&compiler_info_flags_);
    parser.AddBoolFlag("-pnacl-x86_64-bias")->SetOutput(&compiler_info_flags_);
    parser.AddBoolFlag("-pnacl-allow-translate")->SetOutput(
        &compiler_info_flags_);
  }
  parser.AddBoolFlag("no-canonical-prefixes")->SetOutput(&compiler_info_flags_);
  parser.AddFlag("Xclang")->SetOutput(&compiler_info_flags_);
  parser.AddFlag("I")->SetValueOutputWithCallback(
      nullptr, &non_system_include_dirs_);
  // We should allow both -imacro and --imacro, -include and --include.
  // See: b/10020850.
  std::vector<string> includes, imacros;
  parser.AddFlag("imacros")->SetValueOutputWithCallback(nullptr, &imacros);
  parser.AddFlag("-imacros")->SetValueOutputWithCallback(nullptr, &imacros);
  parser.AddFlag("include")->SetValueOutputWithCallback(nullptr, &includes);
  parser.AddFlag("-include")->SetValueOutputWithCallback(nullptr, &includes);
  // TODO: We need to consider the order of -I and -F.
  parser.AddFlag("F")->SetValueOutputWithCallback(nullptr, &framework_dirs_);
  // TODO: Support -iprefix, -I-, and etc.
  MacroStore<true> defined_macro_store(&commandline_macros_);
  MacroStore<false> undefined_macro_store(&commandline_macros_);
  parser.AddFlag("D")->SetCallbackForParsedArgs(&defined_macro_store);
  parser.AddFlag("U")->SetCallbackForParsedArgs(&undefined_macro_store);

  // Special handle for "-W", "-Wa,", "-Wl,", "-Wp,".
  // We want to parse "-Wa,", "-Wp,"
  // We want to mark "-Wl," unknown.
  // However, we want to parse -Wsomething.
  FlagParser::Flag* flag_W = parser.AddPrefixFlag("W");
  FlagParser::Flag* flag_Wa = parser.AddPrefixFlag("Wa,");
  FlagParser::Flag* flag_Wl = parser.AddPrefixFlag("Wl,");
  FlagParser::Flag* flag_Wp = parser.AddPrefixFlag("Wp,");
  std::vector<string> assembler_flags;
  std::vector<string> preprocessor_flags;
  flag_Wa->SetValueOutputWithCallback(nullptr, &assembler_flags);
  flag_Wp->SetValueOutputWithCallback(nullptr, &preprocessor_flags);

  parser.AddNonFlag()->SetOutput(&input_filenames_);

  parser.Parse(expanded_args_);
  unknown_flags_ = parser.unknown_flag_args();

  // -Wa, is a flag for assembler.
  // -Wa,--noexecstack is often used.
  if (!assembler_flags.empty()) {
    std::vector<string> subflags;
    for (const auto& fs : assembler_flags) {
      for (auto&& f : absl::StrSplit(fs, ',')) {
        subflags.emplace_back(f);
      }
    }

    FlagParser pp;
    FlagParser::Options* opts = pp.mutable_options();
    opts->flag_prefix = '-';
    opts->allows_equal_arg = true;
    opts->allows_nonspace_arg = true;
    opts->has_command_name = false;

    pp.AddBoolFlag("-noexecstack");  // --noexecstack to make stack unexecutable
    pp.AddFlag("-defsym");  // --defsym,SYM=VALUE to defin symbol SYM.
    pp.AddPrefixFlag("I");  // -Iout/somewhere; add include path
    pp.AddBoolFlag("gdwarf-2");  // -gdwarf-2; debug info
    pp.AddFlag("march");  // -march=foo; set architecture
    pp.AddFlag("mfpu");  // -mfpu=foo; set cpu

    pp.Parse(subflags);
    for (const auto& unknown : pp.unknown_flag_args()) {
      unknown_flags_.push_back("-Wa," + unknown);
    }
  }

  if (flag_Wl->seen()) {
    // For "-Wl,", Mark the whole flag as unknown.
    // We won't support linker flags.
    for (const auto& v : flag_Wl->values()) {
      unknown_flags_.push_back("-Wl," + v);
    }
  }

  // Note: -Wp,-D -Wp,FOOBAR can be considered as -Wp,-D,FOOBAR
  if (!preprocessor_flags.empty()) {
    std::vector<string> subflags;
    for (const auto& fs : preprocessor_flags) {
      for (auto&& f : absl::StrSplit(fs, ',')) {
        subflags.emplace_back(f);
      }
    }

    FlagParser pp;
    FlagParser::Options* opts = pp.mutable_options();
    opts->flag_prefix = '-';
    opts->allows_equal_arg = true;
    opts->allows_nonspace_arg = true;
    opts->has_command_name = false;

    pp.AddFlag("D")->SetCallbackForParsedArgs(&defined_macro_store);
    pp.AddFlag("U")->SetCallbackForParsedArgs(&undefined_macro_store);
    FlagParser::Flag* flag_MD_pp = pp.AddFlag("MD");

    pp.Parse(subflags);

    if (flag_MD_pp->seen()) {
      output_files_.push_back(flag_MD_pp->GetLastValue());
    }
    for (const auto& unknown : pp.unknown_flag_args()) {
      unknown_flags_.push_back("-Wp," + unknown);
    }
  }

  // Check -W flag.
  for (const auto& value : flag_W->values()) {
    if (!IsKnownWarningOption(value)) {
      unknown_flags_.push_back("-W" + value);
    }
  }

  // Check debug flags. We match -g with prefix flag. It covers too much.
  // If the value is not known, we'd like to mark it as unknown option.
  for (const auto& value : flag_g->values()) {
    if (!IsKnownDebugOption(value)) {
      unknown_flags_.push_back("-g" + value);
    }
  }

  if (!has_at_file) {
    // no @file in args.
    CHECK_EQ(args_, expanded_args_);
    expanded_args_.clear();
  }

  if (flag_isysroot->seen())
    isysroot_ = flag_isysroot->GetLastValue();
  if (flag_resource_dir->seen())
    resource_dir_ = flag_resource_dir->GetLastValue();
  if (flag_fsanitize->seen()) {
    for (const auto& value : flag_fsanitize->values()) {
      for (auto&& v : absl::StrSplit(value, ',')) {
        fsanitize_.insert(string(v));
      }
    }
  }
  if (flag_fdebug_prefix_map->seen()) {
    for (const auto& value : flag_fdebug_prefix_map->values()) {
      size_t pos = value.find("=");
      if (pos == string::npos) {
        LOG(ERROR) << "invalid argument is given to -fdebug-prefix-map:"
                   << value;
        return;
      }
      bool inserted = fdebug_prefix_map_.insert(
          std::make_pair(value.substr(0, pos), value.substr(pos + 1))).second;
      LOG_IF(INFO, !inserted) << "-fdebug-prefix-map has duplicated entry."
                              << " ignored: " << value;
    }
    // -fdebug-prefix-map does not affect system include dirs or
    // predefined macros.  We do not include it in compiler_info_flags_.
    // Especially for clang, it is only used in lib/CodeGen/CGDebugInfo.cpp,
    // which is code to generate debug info.
  }

  string output = "a.out";
  is_successful_ = true;

  mode_ = COMPILE;
  if (flag_E->seen() || flag_M->seen()) {
    mode_ = PREPROCESS;
    output = "";
  } else if (!flag_c->seen() && !flag_S->seen()) {
    mode_ = LINK;
  }

  if (input_filenames_.size() == 1) {
    if (input_filenames_[0] == "-" || input_filenames_[0] == "/dev/stdin") {
      is_stdin_input_ = true;
    }
  } else if (mode_ != LINK && input_filenames_.size() > 1) {
    string buf = absl::StrJoin(input_filenames_, ", ");
    Fail("multiple input file names: " + buf, args);
  }

  if (!llvm_options.empty()) {
    // TODO: no need to set -*-blacklist options in compiler_info_flags_?
    std::copy(llvm_options.begin(), llvm_options.end(),
              back_inserter(compiler_info_flags_));

    FlagParser llvm_parser;
    FlagParser::Options* opts = llvm_parser.mutable_options();
    opts->flag_prefix = '-';
    opts->allows_equal_arg = true;
    opts->has_command_name = false;

    llvm_parser.AddFlag("asan-blacklist")->SetValueOutputWithCallback(
        nullptr, &optional_input_filenames_);
    llvm_parser.AddFlag("tsan-blacklist")->SetValueOutputWithCallback(
        nullptr, &optional_input_filenames_);
    llvm_parser.Parse(llvm_options);
  }
  if (flag_fsanitize_blacklist->seen()) {
    const std::vector<string>& values = flag_fsanitize_blacklist->values();
    for (const auto& value : values) {
      // -fsanitize-blacklist doesn't affect system include dirs or
      // predefined macros, so don't include it in compiler_info_flags_.
      optional_input_filenames_.push_back(value);
    }
  }

  if (flag_x->seen()) {
    compiler_info_flags_.push_back("-x");
    compiler_info_flags_.push_back(flag_x->GetLastValue());
  }
  if (has_nostdinc_) {
    compiler_info_flags_.push_back("-nostdinc");
  }
  if (no_integrated_as) {
    compiler_info_flags_.push_back("-no-integrated-as");
    has_no_integrated_as_ = true;
  }
  if (fno_integrated_as) {
    compiler_info_flags_.push_back("-fno-integrated-as");
    has_no_integrated_as_ = true;
  }
  if (ffreestanding) {
    compiler_info_flags_.push_back("-ffreestanding");
    has_ffreestanding_ = true;
  }
  if (fno_hosted) {
    compiler_info_flags_.push_back("-fno-hosted");
    has_fno_hosted_ = true;
  }
  if (fsyntax_only) {
    compiler_info_flags_.push_back("-fsyntax-only");
    has_fsyntax_only_ = true;
  }

  if (!isysroot_.empty()) {
    compiler_info_flags_.push_back("-isysroot");
    compiler_info_flags_.push_back(isysroot_);
  }

  // Workaround for ChromeOS.
  // https://code.google.com/p/chromium/issues/detail?id=338646
  //
  // TODO: remove this when we drop chromeos wrapper support.
  // In https://code.google.com/p/chromium/issues/detail?id=316963,
  // we are discussing about the drop of chromeos wrapper support.
  // In other words, goma is called by the wrapper, and we do not have
  // the wrapper installed in the goma server.
  for (const auto& it : commandline_macros_) {
    if (it.first == "__KERNEL__" && it.second) {
      compiler_info_flags_.push_back("-D__KERNEL__");
      break;
    }
  }

  // All files specified by -imacros are processed before all files
  // specified by -include.
  std::copy(imacros.begin(), imacros.end(), back_inserter(root_includes_));
  std::copy(includes.begin(), includes.end(), back_inserter(root_includes_));

  if (flag_o->seen()) {
    output_files_.push_back(flag_o->GetLastValue());
    output = flag_o->GetLastValue();
  }

  if (flag_MF->seen()) {
    output_files_.push_back(flag_MF->GetLastValue());
  }

  if (print_file_name) {
    Fail("not supported on remote", args);
  }

  if (flag_x->seen()) {
    lang_ = flag_x->GetLastValue();
  } else {
    lang_ = GetLanguage(compiler_name_,
                        (!input_filenames_.empty() ? input_filenames_[0] : ""));
  }
  is_cplusplus_ = (lang_.find("c++") != string::npos);
  if (mode_ == COMPILE)
    is_precompiling_header_ = absl::EndsWith(lang_, "-header");

  // Create a default output flag. FIXME: is this necessary?
  if (output_files_.empty() && !input_filenames_.empty()) {
    size_t ext_start = input_filenames_[0].rfind('.');
    if (flag_E->seen() || flag_M->seen()) {
      // output will be stdout.
      return;
    } else if (flag_S->seen()) {
      if (string::npos != ext_start)
        output = input_filenames_[0].substr(0, ext_start) + ".s";
      else
        return;
    } else if (is_precompiling_header_) {
      output =  input_filenames_[0] + ".gch";
    } else if (flag_c->seen()) {
      if (string::npos != ext_start)
        output = input_filenames_[0].substr(0, ext_start) + ".o";
      else
        return;
    }
    output_files_.push_back(output);
  }

  // if -MD or -MMD flag was specified, and -MF flag was not specified, assume
  // .d file output.
  if ((flag_MD->seen() || flag_MMD->seen()) && !flag_MF->seen()) {
    size_t ext_start = output.rfind('.');
    if (string::npos != ext_start) {
      output_files_.push_back(output.substr(0, ext_start) + ".d");
    }
  }

  if (flag_gsplit_dwarf->seen()) {
    if (mode_ == COMPILE) {
      output_files_.push_back(
          file::JoinPath(file::Dirname(output), file::Stem(output)) + ".dwo");
    }

    const string& input0 = input_filenames_[0];
    if (mode_ == LINK && file::Extension(input0) != "o") {
      output_files_.push_back(
          file::JoinPath(file::Dirname(input0), file::Stem(input0)) + ".dwo");
    }
  }

  bool use_profile_input = false;
  string profile_input_dir = ".";

  for (const auto& flag : flag_fprofile->values()) {
    compiler_info_flags_.emplace_back("-fprofile-" + flag);

    // Pick the last profile dir, this is how GCC works.
    if (absl::StartsWith(flag, "dir=")) {
      profile_input_dir = flag.substr(strlen("dir="));
    } else if (absl::StartsWith(flag, "generate=")) {
      profile_input_dir = flag.substr(strlen("generate="));
    }
  }

  for (const auto& flag : flag_fprofile->values()) {
    use_profile_input |= absl::StartsWith(flag, "use");

    if (absl::StartsWith(flag, "use=")) {
      const string& use_path = flag.substr(strlen("use="));

      // https://clang.llvm.org/docs/ClangCommandLineReference.html#cmdoption-clang1-fprofile-use
      if (IsClangCommand(compiler_name_) &&
          file::IsDirectory(
              file::JoinPathRespectAbsolute(cwd, profile_input_dir, use_path),
              file::Defaults()).ok()) {
        optional_input_filenames_.push_back(
            file::JoinPathRespectAbsolute(
                profile_input_dir, use_path, "default.profdata"));
      } else {
        optional_input_filenames_.push_back(
            file::JoinPathRespectAbsolute(profile_input_dir, use_path));
      }
    }
  }

  if (!IsClangCommand(compiler_name_) && use_profile_input &&
      !is_precompiling_header_) {
    for (const auto& filename : input_filenames_) {
      size_t ext_start = filename.rfind('.');
      if (ext_start == string::npos)
        continue;
      size_t last_dir = filename.rfind('/');
      if (last_dir == string::npos)
        last_dir = 0;
      else
        last_dir++;
      optional_input_filenames_.push_back(file::JoinPath(
          profile_input_dir,
          filename.substr(last_dir, ext_start - last_dir) + ".gcda"));
    }
  }
  if (flag_fprofile_sample_use->seen()) {
    optional_input_filenames_.push_back(
        flag_fprofile_sample_use->GetLastValue());
  }
  if (flag_fthinlto_index->seen()) {
    optional_input_filenames_.push_back(flag_fthinlto_index->GetLastValue());
  }
}

const std::vector<string> GCCFlags::include_dirs() const {
  std::vector<string> dirs(non_system_include_dirs_);
  std::copy(framework_dirs_.begin(), framework_dirs_.end(),
            back_inserter(dirs));
  return dirs;
}

bool GCCFlags::IsClientImportantEnv(const char* env) const {
  if (IsServerImportantEnv(env)) {
    return true;
  }

  // Allow WINEDEBUG= only in client.
  if (absl::StartsWith(env, "WINEDEBUG=")) {
    return true;
  }

  // These are used for nacl on Win.
  // Don't send this to server.
  if ((absl::StartsWithIgnoreCase(env, "PATHEXT=")) ||
      (absl::StartsWithIgnoreCase(env, "SystemRoot="))) {
    return true;
  }

  return false;
}

bool GCCFlags::IsServerImportantEnv(const char* env) const {
  // http://gcc.gnu.org/onlinedocs/gcc/Environment-Variables.html
  // SYSROOT is not mentioned in the above but it seems this changes
  // the behavior of GCC.
  //
  // Although ld(1) manual mentions following variables, they are not added
  // without actual needs. That is because it may lead security risks and
  // gold (linker used by chromium) seems not use them.
  // - LD_RUN_PATH
  // - LD_LIBRARY_PATH
  //
  // PWD is used for current working directory. b/27487704

  static const char* kCheckEnvs[] = {
    "SYSROOT=",
    "LIBRARY_PATH=",
    "CPATH=",
    "C_INCLUDE_PATH=",
    "CPLUS_INCLUDE_PATH=",
    "OBJC_INCLUDE_PATH=",
    "DEPENDENCIES_OUTPUT=",
    "SUNPRO_DEPENDENCIES=",
    "MACOSX_DEPLOYMENT_TARGET=",
    "SDKROOT=",
    "PWD=",
    "DEVELOPER_DIR=",
  };

  for (const char* check_env : kCheckEnvs) {
    if (absl::StartsWith(env, check_env)) {
      return true;
    }
  }

  return false;
}

/* static */
void GCCFlags::DefineFlags(FlagParser* parser) {
  FlagParser::Options* opts = parser->mutable_options();
  opts->flag_prefix = '-';
  opts->allows_equal_arg = true;
  opts->allows_nonspace_arg = true;

  // clang options can be taken from:
  // https://github.com/llvm-mirror/clang/blob/master/include/clang/Driver/Options.td
  // gcc options
  // https://gcc.gnu.org/onlinedocs/gcc-6.4.0/gcc/Option-Summary.html#Option-Summary

  static const struct {
    const char* name;
    FlagType flag_type;
  } kFlags[] = {
    // gcc/clang flags
    { "-C", kBool },  // preprocessor option; don't remove comment
    { "-P", kBool },  // preprocessor option; Disable linemarker output in -E mode
    { "-include", kNormal },  // preprocess <file> first
    { "-macros", kNormal }, // preprocess <file> first
    { "-param", kNormal },
    { "-sysroot", kNormal },
    { "-version", kBool },  // --version
    { "B", kNormal },  // add dir to compiler's search paths
    { "D", kNormal },  // preprocessor defines
    { "F", kNormal },
    { "I", kNormal },  // add dir to header search paths
    { "L", kNormal },  // add dir to linker search paths
    { "MF", kNormal },  // specify dependency output
    { "MP", kBool },  // Create phony target for each dependency (other than main file)
    { "MQ", kBool },  // Specify name of main file output to quote in depfile
    { "MT", kNormal },
    { "Qunused-arguments", kBool },  // Don't emit warning for unused driver arguments
    { "V", kNormal },  // specify target version
    { "W", kPrefix },  // -Wsomething; disable/disable warnings
    { "Wa,", kPrefix }, // Options to assembly
    { "Wl,", kPrefix }, // Options to linker
    { "Wp,", kPrefix }, // Options to proprocessor
    { "Xassembler", kNormal },
    { "Xlinker", kNormal },
    { "Xpreprocessor", kNormal },
    { "ansi", kBool },  // -ansi. choose c dialect
    { "arch", kNormal },  // processor type
    { "b", kNormal },  // specify target machine
    { "dA", kBool } ,  // Annotate the assembler output with miscellaneous debugging information.
    { "dD", kBool },  // Like '-dM', without predefined macros etc.
    { "dM", kBool },  // Generate a list of ‘#define’ directiv.
    { "fplugin=", kPrefix },  // -fplugin=<dsopath>; gcc plugin
    { "g", kPrefix },  // debug information. NOTE: Needs special treatment.
    { "gsplit-dwarf", kBool },  // to enable the generation of split DWARF.
    { "idirafter", kNormal },
    { "iframework", kNormal },
    { "imacros", kNormal },   // preprocess <file> first
    { "imultilib", kNormal },
    { "include", kNormal },   // preprocess <file> first
    { "iquote", kNormal },
    { "isysroot", kNormal },
    { "isystem", kNormal },
    { "m", kNormal },  // machine dependent options
    { "o", kNormal },  // specify output
    { "pedantic", kBool },  // old form of -Wpedantic (older gcc has this)
    { "pg", kBool },  // Generate extra code for gprof
    { "specs", kNormal },
    { "std", kNormal },
    { "target", kNormal },
    { "v", kBool },  // Show commands to run and use verbose output
    { "w", kBool },  // Inhibit all warning messages.
    { "x", kNormal },  // specify language

    // darwin options
    { "-serialize-diagnostics", kNormal },
    { "allowable_client", kNormal },
    { "client_name", kNormal },
    { "compatibility_version", kNormal },
    { "current_version", kNormal },
    { "dylib_file", kNormal },
    { "dylinker_install_name", kNormal },
    { "exported_symbols_list", kNormal },
    { "filelist", kNormal },
    { "framework", kNormal },
    { "image_base", kNormal },
    { "init", kNormal },
    { "install_name", kNormal },
    { "multiply_defined", kNormal },
    { "multiply_defined_unused", kNormal },
    { "no-canonical-prefixes", kBool },
    { "pagezero_size", kNormal },
    { "read_only_relocs", kNormal },
    { "seg_addr_table", kNormal },
    { "seg_addr_table_filename", kNormal },
    { "segs_read_only_addr", kNormal },
    { "segs_read_write_addr", kNormal },
    { "sub_library", kNormal },
    { "sub_umbrella", kNormal },
    { "umbrella", kNormal },
    { "undefined", kNormal },
    { "unexported_symbols_list", kNormal },
    { "weak_reference_mismatches", kNormal },
    // TODO: -segproto takes 3 arguments (segname, max_prot and init_prot)
    // TODO: -segaddr takes 2 arguments (name and address)
    // TODO: -sectobjectsymbols takes 2 arguments (segname and sectname)
    // TODO: -sectorder takes 3 arguments (segname, sectname and orderfile)

    // for clang
    { "-coverage", kBool },  // take code coverage
    { "-no-system-header-prefix=", kPrefix }, // Specify header is not a system header --no-system-header-prefix=<prefix>
    { "-system-header-prefix", kNormal },  // Specify header is a system header (for diagnosis) --system-header-prefix=<prefix> or --sytem-header-prefix=<arg>
    { "Xanalyzer", kNormal },
    { "Xclang", kNormal },
    { "gcc-toolchain", kNormal },
    { "nostdlibinc", kBool },  // Do not search the standard system directories for include files, but do search compiler builtin include directories.
    { "print-libgcc-file-name", kBool },  // Print the library path for the currently used compiler runtime library
    { "print-prog-name=", kPrefix },  // Print the full program path of <name> -print-prog-name=<name>

    // linker flags
    // https://gcc.gnu.org/onlinedocs/gcc/Link-Options.html
    { "nodefaultlibs", kBool  }, // Do not use the standard system libraries
    { "nostdlib", kBool },  // Do not use the standard system startup files or libraries
    { "nostdlib++", kBool },  // Don't use the ld_stdlib++ section
    { "pie", kBool },  // Produce a dynamically linked position independent executable
    { "rdynamic", kBool },  // Pass the flag -export-dynamic to the ELF linker
    { "static", kBool },  // this overrides -pie and prevents linking with the shared libraries.
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

// static
bool GCCFlags::IsKnownWarningOption(absl::string_view option) {
  // TODO: If we can have constexpr version of is_sorted,
  // we can check this in compile time.
  DCHECK(std::is_sorted(std::begin(kKnownWarningOptions),
                        std::end(kKnownWarningOptions),
                        [](absl::string_view lhs, absl::string_view rhs) {
                          return lhs < rhs;
                        }))
      << "kKnownWarningOptions must be sorted";

  // for "foo=x", take "foo=" only.
  string::size_type p = option.find('=');
  if (p != string::npos) {
    option = option.substr(0, p + 1);  // Keep '='.
  }

  // Remove "no-"
  if (absl::StartsWith(option, "no-")) {
    option = option.substr(strlen("no-"));
  }

  return std::binary_search(std::begin(kKnownWarningOptions),
                            std::end(kKnownWarningOptions),
                            option);
}

// static
bool GCCFlags::IsKnownDebugOption(absl::string_view v) {
  // See https://gcc.gnu.org/onlinedocs/gcc/Debugging-Options.html
  // -gz is not handled here, since it's used like -gz=<type>.
  // It's not suitable to handle it here.
  static const char* const kKnownDebugOptions[] {
    "",
    "0", "1", "2", "3",
    "column-info",
    "dw",
    "dwarf", "dwarf-2", "dwarf-3", "dwarf-4", "dwarf-5",
    "gdb", "gdb1", "gdb2", "gdb3",
    "gnu-pubnames",
    "line-tables-only",
    "no-column-info",
    "no-record-gcc-switches",
    "no-strict-dwarf",
    "pubnames",
    "record-gcc-switches",
    "split-dwarf",
    "stabs", "stabs+", "stabs0", "stabs1", "stabs2", "stabs3",
    "strict-dwarf",
    "vms", "vms0", "vms1", "vms2", "vms3",
    "xcoff", "xcoff+", "xcoff0", "xcoff1", "xcoff2", "xcoff3",
  };

  DCHECK(std::is_sorted(std::begin(kKnownDebugOptions),
                        std::end(kKnownDebugOptions),
                        [](absl::string_view lhs, absl::string_view rhs) {
                          return lhs < rhs;
                        }))
      << "kKnownDebugOptions must be sorted";

  return std::binary_search(std::begin(kKnownDebugOptions),
                            std::end(kKnownDebugOptions),
                            v);
}

/* static */
string GCCFlags::GetLanguage(const string& compiler_name,
                             const string& input_filename) {
  // Decision based on a compiler name.
  bool is_cplusplus = false;
  if (compiler_name.find("g++") != string::npos) {
    is_cplusplus = true;
  }
  if (input_filename.empty())
    return is_cplusplus ? "c++" : "c";

  // Decision based on a file extension.
  string suffix = GetFileNameExtension(input_filename);
  if (!is_cplusplus && suffix != "c") {
    // GCC may change the language by suffix of input files.
    // See gcc/gcc.c and gcc/cp/lang-specs.h .
    // Note that slow operation is OK because we've checked .c first
    // so we come here rarely.
    if (suffix == "cc" ||
        suffix == "cxx" ||
        suffix == "cpp" ||
        suffix == "cp" ||
        suffix == "c++" ||
        suffix == "C" ||
        suffix == "CPP" ||
        suffix == "ii" ||
        suffix == "H" ||
        suffix == "hpp" ||
        suffix == "hp" ||
        suffix == "hxx" ||
        suffix == "h++" ||
        suffix == "HPP" ||
        suffix == "tcc" ||
        suffix == "hh" ||
        suffix == "mm" ||
        suffix == "M" ||
        suffix == "mii") {
      is_cplusplus = true;
    }
  }
  if (is_cplusplus && suffix == "m") {
    // g++ and clang++ think .m as objc not objc++. (b/11521718)
    is_cplusplus = false;
  }

  const string lang = is_cplusplus ? "c++" : "c";
  if (!suffix.empty()) {
    if (suffix[0] == 'm' || suffix[0] == 'M')
      return string("objective-") + lang;

    if (suffix[0] == 'h' || suffix[0] == 'H' || suffix == "tcc")
      return lang + "-header";
  }
  return lang;
}

/* static */
string GCCFlags::GetFileNameExtension(const string& filepath) {
  return string(::devtools_goma::GetFileNameExtension(filepath));
}

void ParseJavaClassPaths(
    const std::vector<string>& class_paths,
    std::vector<string>* jar_files) {
  for (const string& class_path : class_paths) {
    for (auto&& path : absl::StrSplit(class_path, ':')) {
      // TODO: We need to handle directories.
      absl::string_view ext = ::devtools_goma::GetFileNameExtension(path);
      if (ext == "jar" || ext == "zip") {
        jar_files->push_back(string(path));
      }
    }
  }
}

JavacFlags::JavacFlags(const std::vector<string>& args, const string& cwd)
    : CompilerFlags(args, cwd) {
  if (!CompilerFlags::ExpandPosixArgs(cwd, args,
                                      &expanded_args_,
                                      &optional_input_filenames_)) {
    Fail("Unable to expand args", args);
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
  parser.AddFlag("bootclasspath")->SetValueOutputWithCallback(
      nullptr, &boot_class_paths);
  // TODO: Support -Xbootclasspath if needed.
  parser.AddFlag("cp")->SetValueOutputWithCallback(nullptr, &class_paths);
  parser.AddFlag("classpath")->SetValueOutputWithCallback(
      nullptr, &class_paths);
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
string JavacFlags::GetCompilerName(absl::string_view /*arg*/) {
  return "javac";
}

class Win32PathNormalizer : public FlagParser::Callback {
 public:
  // Returns parsed flag value of value for flag.
  string ParseFlagValue(const FlagParser::Flag& flag,
                        const string& value) override;
};

string Win32PathNormalizer::ParseFlagValue(
    const FlagParser::Flag& /* flag */, const string& value) {
  return NormalizeWin32Path(value);
}

/* static */
string VCFlags::GetCompilerName(absl::string_view arg) {
  if (IsClangClCommand(arg)) {
    return "clang-cl";
  }
  return "cl.exe";
}

string VCFlags::compiler_name() const {
  return GetCompilerName(compiler_name_);
}

VCFlags::VCFlags(const std::vector<string>& args, const string& cwd)
    : CompilerFlags(args, cwd),
      is_cplusplus_(true),
      ignore_stdinc_(false),
      has_Brepro_(false),
      require_mspdbserv_(false) {
  bool result = ExpandArgs(cwd, args, &expanded_args_,
                           &optional_input_filenames_);
  if (!result) {
    Fail("Unable to expand args", args);
    return;
  }

  FlagParser parser;
  DefineFlags(&parser);
  Win32PathNormalizer normalizer;

  // Compile only, no link
  FlagParser::Flag* flag_c = parser.AddBoolFlag("c");

  // Preprocess only, do not compile
  FlagParser::Flag* flag_E = parser.AddBoolFlag("E");
  FlagParser::Flag* flag_EP = parser.AddBoolFlag("EP");
  FlagParser::Flag* flag_P = parser.AddBoolFlag("P");

  // Ignore "standard places".
  FlagParser::Flag* flag_X = parser.AddBoolFlag("X");

  // Compile file as .c
  FlagParser::Flag* flag_Tc = parser.AddFlag("Tc");

  // Compile all files as .c
  FlagParser::Flag* flag_TC = parser.AddBoolFlag("TC");

  // Compile file as .cpp
  FlagParser::Flag* flag_Tp = parser.AddFlag("Tp");

  // Compile all files as .cpp
  FlagParser::Flag* flag_TP = parser.AddBoolFlag("TP");

  // Specify output.
  FlagParser::Flag* flag_o = parser.AddFlag("o");  // obsoleted but always there
  FlagParser::Flag* flag_Fo = parser.AddPrefixFlag("Fo");  // obj file path
  FlagParser::Flag* flag_Fe = parser.AddPrefixFlag("Fe");  // exe file path

  // Optimization prefix
  parser.AddPrefixFlag("O")->SetOutput(&compiler_info_flags_);

  // M[DT]d? define _DEBUG, _MT, and _DLL.
  parser.AddPrefixFlag("MD")->SetOutput(&compiler_info_flags_);
  parser.AddPrefixFlag("MT")->SetOutput(&compiler_info_flags_);

  // standard
  parser.AddBoolFlag("permissive-")->SetOutput(&compiler_info_flags_);
  parser.AddPrefixFlag("std:")->SetOutput(&compiler_info_flags_);

  // Additional include path.
  parser.AddFlag("I")->SetValueOutputWithCallback(&normalizer, &include_dirs_);

  MacroStore<true> defined_macro_store(&commandline_macros_);
  MacroStore<false> undefined_macro_store(&commandline_macros_);
  parser.AddFlag("D")->SetCallbackForParsedArgs(&defined_macro_store);
  parser.AddFlag("U")->SetCallbackForParsedArgs(&undefined_macro_store);

  // specifies the architecture for code generation.
  // It is passed to compiler_info_flags_ to get macros.
  parser.AddFlag("arch")->SetOutput(&compiler_info_flags_);

  // Flags that affects predefined macros
  FlagParser::Flag* flag_ZI = parser.AddBoolFlag("ZI");
  FlagParser::Flag* flag_RTC = parser.AddPrefixFlag("RTC");
  FlagParser::Flag* flag_Zc_wchar_t = parser.AddBoolFlag("Zc:wchar_t");

  FlagParser::Flag* flag_Zi = parser.AddBoolFlag("Zi");

  parser.AddFlag("FI")->SetValueOutputWithCallback(nullptr, &root_includes_);

  FlagParser::Flag* flag_Yc = parser.AddPrefixFlag("Yc");
  FlagParser::Flag* flag_Yu = parser.AddPrefixFlag("Yu");
  FlagParser::Flag* flag_Fp = parser.AddPrefixFlag("Fp");

  // Machine options used by clang-cl.
  FlagParser::Flag* flag_m = parser.AddFlag("m");
  FlagParser::Flag* flag_fmsc_version = parser.AddPrefixFlag("fmsc-version=");
  FlagParser::Flag* flag_fms_compatibility_version =
      parser.AddPrefixFlag("fms-compatibility-version=");
  FlagParser::Flag* flag_fsanitize = parser.AddFlag("fsanitize");
  FlagParser::Flag* flag_fno_sanitize_blacklist = nullptr;
  FlagParser::Flag* flag_fsanitize_blacklist = nullptr;
  FlagParser::Flag* flag_mllvm = parser.AddFlag("mllvm");
  FlagParser::Flag* flag_isystem = parser.AddFlag("isystem");
  // TODO: check -iquote?
  // http://clang.llvm.org/docs/UsersManual.html#id8
  FlagParser::Flag* flag_imsvc = parser.AddFlag("imsvc");
  FlagParser::Flag* flag_std = parser.AddFlag("std");  // e.g. -std=c11
  std::vector<string> incremental_linker_flags;
  parser.AddBoolFlag("Brepro")->SetOutput(&incremental_linker_flags);
  parser.AddBoolFlag("Brepro-")->SetOutput(&incremental_linker_flags);
  if (compiler_name() == "clang-cl") {
    flag_m->SetOutput(&compiler_info_flags_);
    flag_fmsc_version->SetOutput(&compiler_info_flags_);
    flag_fms_compatibility_version->SetOutput(&compiler_info_flags_);
    flag_fsanitize->SetOutput(&compiler_info_flags_);
    // TODO: do we need to support more sanitize options?
    flag_fno_sanitize_blacklist =
        parser.AddBoolFlag("fno-sanitize-blacklist");
    flag_fsanitize_blacklist = parser.AddFlag("fsanitize-blacklist=");
    flag_mllvm->SetOutput(&compiler_info_flags_);
    flag_isystem->SetOutput(&compiler_info_flags_);
    flag_imsvc->SetOutput(&compiler_info_flags_);
    flag_std->SetOutput(&compiler_info_flags_);

    // Make these unserstood.
    parser.AddBoolFlag("fansi-escape-codes");  // Use ANSI escape codes for diagnostics
    parser.AddBoolFlag("fdiagnostics-absolute-paths");  // Print absolute paths in diagnostics

    // Make it understand Xclang.
    parser.AddFlag("Xclang")->SetOutput(&compiler_info_flags_);

    parser.AddBoolFlag("mincremental-linker-compatible")->SetOutput(
        &incremental_linker_flags);
    parser.AddBoolFlag("mno-incremental-linker-compatible")->SetOutput(
        &incremental_linker_flags);
  }

  parser.AddNonFlag()->SetOutput(&input_filenames_);

  parser.Parse(expanded_args_);
  unknown_flags_ = parser.unknown_flag_args();

  is_successful_ = true;

  lang_ = "c++";
  // CL.exe default to C++ unless /Tc /TC specified,
  // or the file is named .c and /Tp /TP are not specified.
  if (flag_Tc->seen() || flag_TC->seen() ||
      ((!input_filenames_.empty() &&
        GetFileNameExtension(input_filenames_[0]) == "c") &&
        !flag_TP->seen() && !flag_Tp->seen())) {
    is_cplusplus_ = false;
    lang_ = "c";
  }

  // Handle implicit macros, lang_ must not change after this.
  // See http://msdn.microsoft.com/en-us/library/b0084kay(v=vs.90).aspx
  if (lang_ == "c++") {
    implicit_macros_.append("#define __cplusplus\n");
  }
  if (flag_ZI->seen()) {
    implicit_macros_.append("#define _VC_NODEFAULTLIB\n");
  }
  if (flag_RTC->seen()) {
    implicit_macros_.append("#define __MSVC_RUNTIME_CHECKS\n");
  }
  if (flag_Zc_wchar_t->seen()) {
    implicit_macros_.append("#define _NATIVE_WCHAR_T_DEFINED\n");
    implicit_macros_.append("#define _WCHAR_T_DEFINED\n");
  }

  // Debug information format.
  // http://msdn.microsoft.com/en-us/library/958x11bc.aspx
  // For VC, /Zi and /ZI generated PDB.
  // For clang-cl, /Zi is alias to /Z7. /ZI is not supported.
  // Probably OK to deal them as the same?
  // See https://msdn.microsoft.com/en-us/library/958x11bc.aspx,
  // and http://clang.llvm.org/docs/UsersManual.html
  if (compiler_name() != "clang-cl" && (flag_Zi->seen() || flag_ZI->seen())) {
    require_mspdbserv_ = true;
  }

  if (flag_fsanitize_blacklist && flag_fsanitize_blacklist->seen() &&
      !flag_fno_sanitize_blacklist->seen()) {
    // TODO: follow -fno-sanitize-blacklist spec.
    // http://clang.llvm.org/docs/UsersManual.html:
    // > -fno-sanitize-blacklist: don't use blacklist file,
    // > if it was specified *earlier in the command line*.
    const std::vector<string>& values = flag_fsanitize_blacklist->values();
    std::copy(values.begin(), values.end(),
              back_inserter(optional_input_filenames_));
  }

  if (flag_X->seen()) {
    ignore_stdinc_ = true;
    compiler_info_flags_.push_back("/X");
  }

  if (flag_EP->seen() || flag_E->seen()) {
    return;  // output to stdout
  }

  if (flag_Yc->seen()) {
    creating_pch_ = flag_Yc->GetLastValue();
  }
  if (flag_Yu->seen()) {
    using_pch_ = flag_Yu->GetLastValue();
  }
  if (flag_Fp->seen()) {
    using_pch_filename_ = flag_Fp->GetLastValue();
  }

  if (!incremental_linker_flags.empty()) {
    const string& last = incremental_linker_flags.back();
    if (last == "-mno-incremental-linker-compatible" ||
        last == "/Brepro" || last == "-Brepro") {
      has_Brepro_ = true;
    }
  }

  string new_extension = ".obj";
  string force_output;
  if (flag_Fo->seen())
    force_output = flag_Fo->GetLastValue();

  if (flag_P->seen()) {
    new_extension = ".i";
    // any option to control output filename?
    force_output = "";
  } else if (!flag_c->seen()) {
    new_extension = ".exe";
    if (flag_Fe->seen()) {
      force_output = flag_Fe->GetLastValue();
    } else {
      force_output = "";
    }
  }

  // Single file with designated destination
  if (input_filenames_.size() == 1) {
    if (force_output.empty() && flag_o->seen()) {
      force_output = flag_o->GetLastValue();
    }

    if (!force_output.empty()) {
      output_files_.push_back(ComposeOutputFilePath(input_filenames_[0],
          force_output, new_extension));
    }
    if (!output_files_.empty()) {
      return;
    }
  }

  for (const auto& input_filename : input_filenames_) {
    output_files_.push_back(
        ComposeOutputFilePath(input_filename, force_output, new_extension));
  }
}

bool VCFlags::IsClientImportantEnv(const char* env) const {
  if (IsServerImportantEnv(env)) {
    return true;
  }

  // We don't override these variables in goma server.
  // So, these are client important, but don't send to server.
  static const char* kCheckEnvs[] = {
    "PATHEXT=",
    "SystemDrive=",
    "SystemRoot=",
  };

  for (const char* check_env : kCheckEnvs) {
    if (absl::StartsWithIgnoreCase(env, check_env)) {
      return true;
    }
  }

  return false;
}

bool VCFlags::IsServerImportantEnv(const char* env) const {
  static const char* kCheckEnvs[] = {
    "INCLUDE=",
    "LIB=",
    "MSC_CMD_FLAGS=",
    "VCINSTALLDIR=",
    "VSINSTALLDIR=",
    "WindowsSdkDir=",
  };

  for (const char* check_env : kCheckEnvs) {
    if (absl::StartsWithIgnoreCase(env, check_env)) {
      return true;
    }
  }

  return false;
}

// static
void VCFlags::DefineFlags(FlagParser* parser) {
  FlagParser::Options* opts = parser->mutable_options();
  // define all known flags of cl.exe here.
  // undefined flag here would be treated as non flag arg
  // if the arg begins with alt_flag_prefix.
  // b/18063824
  // https://code.google.com/p/chromium/issues/detail?id=427942
  opts->flag_prefix = '-';
  opts->alt_flag_prefix = '/';
  opts->allows_nonspace_arg = true;

  // http://msdn.microsoft.com//library/fwkeyyhe.aspx
  // note: some bool flag may take - as suffix even if it is documented
  // on the above URL? clang-cl defines such flag.
  parser->AddBoolFlag("?");  // alias of help
  parser->AddPrefixFlag("AI");  // specifies a directory to search for #using
  parser->AddPrefixFlag("analyze");  // enable code analysis
  parser->AddPrefixFlag("arch");  // specifies the architecture for code gen
  parser->AddBoolFlag("await");  // enable resumable functions extension

  parser->AddBoolFlag("bigobj");  // increases the num of addressable sections

  parser->AddBoolFlag("C");  // preserves comments during preprocessing
  parser->AddBoolFlag("c");  // compile only
  parser->AddPrefixFlag("cgthreads");  // specify num of cl.exe threads
  parser->AddPrefixFlag("clr");
  parser->AddPrefixFlag("constexpr");  // constexpr options

  parser->AddFlag("D");  // define macro
  parser->AddPrefixFlag("doc");  // process documentation comments
  // /diagnostics:<args,...> controls the format of diagnostic messages
  parser->AddPrefixFlag("diagnostics:");

  parser->AddBoolFlag("E");  // preprocess to stdout
  parser->AddPrefixFlag("EH");  // exception ahdling model
  parser->AddBoolFlag("EP");  // disable linemarker output and preprocess
  parser->AddPrefixFlag("errorReport");

  parser->AddFlag("F");  // set stack size
  parser->AddPrefixFlag("favor");  // optimize for architecture specifics
  parser->AddPrefixFlag("FA");  // output assembly code file
  parser->AddPrefixFlag("Fa");  // output assembly code to this file
  parser->AddBoolFlag("FC");  // full path of source code in diagnostic text
  parser->AddPrefixFlag("Fd");  // set pdb file name
  parser->AddPrefixFlag("Fe");  // set output executable file or directory
  parser->AddFlag("FI");  // include file before parsing
  parser->AddPrefixFlag("Fi");  // set preprocess output file name
  parser->AddPrefixFlag("Fm");  // set map file name
  parser->AddPrefixFlag("Fo");  // set output object file or directory
  parser->AddPrefixFlag("fp");  // specify floating proint behavior
  parser->AddPrefixFlag("Fp");  // set pch file name
  parser->AddPrefixFlag("FR");  // .sbr file
  parser->AddPrefixFlag("Fr");  // .sbr file without info on local var
  parser->AddBoolFlag("FS");  // force synchronous PDB writes
  parser->AddFlag("FU");  // #using
  parser->AddBoolFlag("Fx");  // merges injected code

  parser->AddBoolFlag("GA");  // optimize for win app
  parser->AddBoolFlag("Gd");  // calling convention
  parser->AddBoolFlag("Ge");  // enable stack probes
  parser->AddBoolFlag("GF");  // enable string pool
  parser->AddBoolFlag("GF-");  // disable string pooling
  parser->AddBoolFlag("GH");  // call hook function _pexit
  parser->AddBoolFlag("Gh");  // call hook function _penter
  parser->AddBoolFlag("GL");  // enables whole program optimization
  parser->AddBoolFlag("GL-");
  parser->AddBoolFlag("Gm");  // enables minimal rebuild
  parser->AddBoolFlag("Gm-");
  parser->AddBoolFlag("GR");  // enable emission of RTTI data
  parser->AddBoolFlag("GR-");  // disable emission of RTTI data
  parser->AddBoolFlag("Gr");  // calling convention
  parser->AddBoolFlag("GS");  // buffer security check
  parser->AddBoolFlag("GS-");
  parser->AddPrefixFlag("Gs");  // controls stack probes
  parser->AddBoolFlag("GT");  // fibre safety thread-local storage
  parser->AddBoolFlag("guard:cf");  // enable control flow guard
  parser->AddBoolFlag("guard:cf-");  // disable control flow guard
  parser->AddBoolFlag("Gv");  // calling convention
  parser->AddBoolFlag("Gw");  // put each data item in its own section
  parser->AddBoolFlag("Gw-");  // don't put each data item in its own section
  parser->AddBoolFlag("GX");  // enable exception handling
  parser->AddBoolFlag("Gy");   // put each function in its own section
  parser->AddBoolFlag("Gy-");  // don't put each function in its own section
  parser->AddBoolFlag("GZ");  // same as /RTC
  parser->AddBoolFlag("Gz");  // calling convention

  parser->AddPrefixFlag("H");  // restricts the length of external names
  parser->AddBoolFlag("HELP");  // alias of help
  parser->AddBoolFlag("help");  // display available options
  parser->AddBoolFlag("homeparams");  // copy register parameters to stack
  parser->AddBoolFlag("hotpatch");  // create hotpatchable image

  parser->AddFlag("I");  // add directory to include search path

  parser->AddBoolFlag("J");  // make char type unsinged

  parser->AddBoolFlag("kernel");  // create kernel mode binary
  parser->AddBoolFlag("kernel-");

  parser->AddBoolFlag("LD");  // create DLL
  parser->AddBoolFlag("LDd");  // create debug DLL
  parser->AddFlag("link");  // forward options to the linker
  parser->AddBoolFlag("LN");

  parser->AddPrefixFlag("MD");  // use DLL run time
  // MD, MDd
  parser->AddPrefixFlag("MP");  // build with multiple process
  parser->AddPrefixFlag("MT");  // use static run time
  // MT, MTd

  parser->AddBoolFlag("nologo");

  parser->AddPrefixFlag("O");  // optimization level
  // O1, O2
  // Ob[012], Od, Oi, Oi-, Os, Ot, Ox, Oy, Oy-
  parser->AddBoolFlag("openmp");

  parser->AddBoolFlag("P");  // preprocess to file
  // set standard-conformance mode (feature set subject to change)
  parser->AddBoolFlag("permissive-");

  parser->AddPrefixFlag("Q");
  // Qfast_transcendentals, QIfirst, Qimprecise_fwaits, Qpar
  // Qsafe_fp_loads, Qrev-report:n

  parser->AddPrefixFlag("RTC");  // run time error check

  parser->AddBoolFlag("sdl");  // additional security check
  parser->AddBoolFlag("sdl-");
  parser->AddBoolFlag("showIncludes");  // print info about included files
  parser->AddPrefixFlag("std:");  // C++ standard version

  parser->AddFlag("Tc");  // specify a C source file
  parser->AddBoolFlag("TC");  // treat all source files as C
  parser->AddFlag("Tp");  // specify a C++ source file
  parser->AddBoolFlag("TP");  // treat all source files as C++

  parser->AddFlag("U");  // undefine macro
  parser->AddBoolFlag("u");  // remove all predefined macros

  parser->AddPrefixFlag("V");  // Sets the version string
  parser->AddPrefixFlag("vd");  // control vtordisp placement
  // for member pointers.
  parser->AddBoolFlag("vmb");  // use a best-case representation method
  parser->AddBoolFlag("vmg");  // use a most-general representation
  // set the default most-general representation
  parser->AddBoolFlag("vmm");  // to multiple inheritance
  parser->AddBoolFlag("vms");  // to single inheritance
  parser->AddBoolFlag("vmv");  // to virtual inheritance
  parser->AddBoolFlag("volatile");

  parser->AddPrefixFlag("W");  // warning
  // W0, W1, W2, W3, W4, Wall, WX, WX-, WL, Wp64
  parser->AddPrefixFlag("w");  // disable warning
  // wd4005, ...

  parser->AddBoolFlag("X");  // ignore standard include paths

  parser->AddBoolFlag("Y-");  // ignore precompiled header
  parser->AddPrefixFlag("Yc");  // create precompiled header
  parser->AddBoolFlag("Yd");  // place debug information
  parser->AddPrefixFlag("Yl");  // inject PCH reference for debug library
  parser->AddPrefixFlag("Yu");  // use precompiled header

  parser->AddBoolFlag("Z7");  // debug information format
  parser->AddBoolFlag("Za");  // disable language extensions
  parser->AddPrefixFlag("Zc");  // conformance
  // line number only debug information; b/30077868
  parser->AddBoolFlag("Zd");
  parser->AddBoolFlag("Ze");  // enable microsoft extensions
  parser->AddBoolFlag("ZH:SHA_256");  // use SHA256 for file checksum
  parser->AddBoolFlag("Zg");  // generate function prototype
  parser->AddBoolFlag("ZI");  // produce pdb
  parser->AddBoolFlag("Zi");  // enable debug information
  parser->AddBoolFlag("Zl");  // omit default library name
  parser->AddPrefixFlag("Zm");  // specify precompiled header memory limit
  parser->AddBoolFlag("Zo");  // enhance optimized debugging
  parser->AddBoolFlag("Zo-");
  parser->AddPrefixFlag("Zp");  // default maximum struct packing alignment
  // Zp1, Zp2, Zp4, Zp8, Zp16
  parser->AddFlag("Zs");  // syntax check only
  parser->AddPrefixFlag("ZW");  // windows runtime compilation

  // New flags from VS2015 Update 2
  parser->AddPrefixFlag("source-charset:");  // set source character set.
  parser->AddPrefixFlag("execution-charset:");  // set execution character set.
  parser->AddBoolFlag("utf-8");  // set both character set to utf-8.
  parser->AddBoolFlag("validate-charset");  //  validate utf-8 files.
  parser->AddBoolFlag("validate-charset-");

  // /d2XXX is undocument flag for debugging.
  // See b/27777598, b/68147091
  parser->AddPrefixFlag("d2");

  // Brepro is undocument flag for reproducible build?
  // https://github.com/llvm-project/llvm-project-20170507/blob/3e1fa78737e3b303558e6310c49d31c31827a2bf/clang/include/clang/Driver/CLCompatOptions.td#L55
  parser->AddBoolFlag("Brepro");
  parser->AddBoolFlag("Brepro-");

  // also see clang-cl
  // http://llvm.org/klaus/clang/blob/master/include/clang/Driver/CLCompatOptions.td
  parser->AddFlag("o");  // set output file or directory
  parser->AddBoolFlag("fallback");
  parser->AddBoolFlag("G1");
  parser->AddBoolFlag("G2");
  parser->AddFlag("imsvc");  // both -imsvc, /imsvc.

  // clang-cl flags. only accepts if it starts with '-'.
  opts->flag_prefix = '-';
  opts->alt_flag_prefix = '\0';
  parser->AddFlag("m");
  parser->AddPrefixFlag("fmsc-version=");  // -fmsc-version=<arg>
  parser->AddPrefixFlag("fms-compatibility-version=");  // -fms-compatibility-version=<arg>
  parser->AddFlag("fsanitize");
  parser->AddBoolFlag("fcolor-diagnostics");  // Use color for diagnostics
  parser->AddBoolFlag("fno-standalone-debug");  // turn on the vtable-based optimization
  parser->AddBoolFlag("fstandalone-debug");  // turn off the vtable-based optimization
  parser->AddBoolFlag("gcolumn-info");  // debug information (-g)
  parser->AddBoolFlag("gline-tables-only");  // debug information (-g)
  parser->AddFlag("Xclang");
  parser->AddFlag("isystem");
  parser->AddPrefixFlag("-analyze");  // enable code analysis (--analyze)

  opts->flag_prefix = '-';
  opts->alt_flag_prefix = '/';
}

// static
bool VCFlags::ExpandArgs(const string& cwd, const std::vector<string>& args,
                         std::vector<string>* expanded_args,
                         std::vector<string>* optional_input_filenames) {
  // Expand arguments which start with '@'.
  for (const auto& arg : args) {
    if (absl::StartsWith(arg, "@")) {
      const string& source_list_filename =
          PathResolver::PlatformConvert(arg.substr(1));
      string source_list;
      if (!ReadFileToString(
               file::JoinPathRespectAbsolute(cwd, source_list_filename),
               &source_list)) {
        LOG(ERROR) << "failed to read: " << source_list_filename;
        return false;
      }
      if (optional_input_filenames) {
        optional_input_filenames->push_back(source_list_filename);
      }

      if (source_list[0] == '\xff' && source_list[1] == '\xfe') {
        // UTF-16LE.
        // do we need to handle FEFF(UTF-16BE) case or others?
        // TODO: handle real wide character.
        // use WideCharToMultiByte on Windows, and iconv on posix?
        VLOG(1) << "Convert WC to MB in @" << source_list_filename;
        string source_list_mb;
        // We don't need BOM (the first 2 bytes: 0xFF 0xFE)
        source_list_mb.resize(source_list.size() / 2 - 1);
        for (size_t i = 2; i < source_list.size(); i += 2) {
          source_list_mb[i / 2 - 1] = source_list[i];
          if (source_list[i + 1] != 0) {
            LOG(ERROR) << "failed to convert:" << source_list_filename;
            return false;
          }
        }
        source_list.swap(source_list_mb);
        VLOG(1) << "source_list:" << source_list;
      }
      if (!ParseWinCommandLineToArgv(source_list, expanded_args)) {
        LOG(WARNING) << "failed to parse command line: " << source_list;
        return false;
      }
      VLOG(1) << "expanded_args:" << *expanded_args;
    } else {
      expanded_args->push_back(arg);
    }
  }
  return true;
}

// static
string VCFlags::GetFileNameExtension(const string& orig_filepath) {
  string filepath = PathResolver::PlatformConvert(orig_filepath,
                                                  PathResolver::kWin32PathSep,
                                                  PathResolver::kPreserveCase);
  string extension =
      string(::devtools_goma::GetFileNameExtension(filepath));
  return extension;
}

// static
string VCFlags::ComposeOutputFilePath(const string& input_file_name,
                                      const string& output_file_or_dir,
                                      const string& output_file_ext) {
  string input_file = NormalizeWin32Path(input_file_name);
  string output_target = NormalizeWin32Path(output_file_or_dir);

  bool output_is_dir = false;
  if (output_target.length() &&
      output_target[output_target.length() - 1] == '\\') {
    output_is_dir = true;
  }
  if (output_target.length() && !output_is_dir) {
    return output_target;
  }

  // We need only the filename part of input file
  size_t begin = input_file.find_last_of("/\\");
  size_t end = input_file.rfind('.');
  begin = (begin == string::npos) ? 0 : begin + 1;
  end = (end == string::npos) ? input_file_name.size() : end;
  string new_output;
  if (end > begin) {
    new_output = input_file.substr(begin, end - begin);
    new_output.append(output_file_ext);
    if (output_target.length() && output_is_dir) {
      new_output = output_target + new_output;
    }
  } else {
    new_output = output_target;
  }
  return new_output;
}

// ----------------------------------------------------------------------
// ClangTidyFlags

ClangTidyFlags::ClangTidyFlags(const std::vector<string>& args,
                               const string& cwd)
    : CompilerFlags(args, cwd), seen_hyphen_hyphen_(false) {
  if (!CompilerFlags::ExpandPosixArgs(cwd, args,
                                      &expanded_args_,
                                      &optional_input_filenames_)) {
    Fail("Unable to expand args", args);
    return;
  }

  FlagParser parser;
  DefineFlags(&parser);

  FlagParser::Flag* flag_export_fixes = parser.AddFlag("export-fixes");
  parser.AddFlag("extra-arg")->SetValueOutputWithCallback(
      nullptr, &extra_arg_);
  parser.AddFlag("extra-arg-before")->SetValueOutputWithCallback(
      nullptr, &extra_arg_before_);
  FlagParser::Flag* flag_p = parser.AddFlag("p");

  parser.Parse(expanded_args_);
  unknown_flags_ = parser.unknown_flag_args();

  if (flag_p->seen()) {
    build_path_ = flag_p->GetLastValue();
  }

  // The file specified in -export-fix will have suggested fix.
  // This can be considered as output.
  if (flag_export_fixes->seen()) {
    output_files_.push_back(flag_export_fixes->GetLastValue());
  }

  // We use absolute path for source_files.
  // clang-tidy has 2 kinds of current working directory.
  // One is for clang-tidy itself, the other is for include processor,
  // which is specified in the compilation database.
  // Converting them is hard, so we'd like to use absolute path.
  std::vector<string> source_files;
  for (size_t i = 1; i < args.size(); ++i) {
    if (seen_hyphen_hyphen_) {
      args_after_hyphen_hyphen_.push_back(args[i]);
      continue;
    }

    if (args[i] == "--") {
      seen_hyphen_hyphen_ = true;
      continue;
    }

    if (!args[i].empty() && args[i][0] == '-') {
      // Skip this option since this is clang-tidy option.
      continue;
    }
    source_files.push_back(file::JoinPath(cwd, args[i]));
  }

  input_filenames_ = std::move(source_files);
  is_successful_ = true;
}

void ClangTidyFlags::SetClangArgs(const std::vector<string>& clang_args,
                                  const string& dir) {
  gcc_flags_.reset(new GCCFlags(clang_args, dir));
  is_successful_ = is_successful_ && gcc_flags_->is_successful();
  lang_ = gcc_flags_->lang();
}

void ClangTidyFlags::SetCompilationDatabasePath(const string& compdb_path) {
  optional_input_filenames_.push_back(compdb_path);
}

string ClangTidyFlags::compiler_name() const {
  return "clang-tidy";
}

// static
void ClangTidyFlags::DefineFlags(FlagParser* parser) {
  FlagParser::Options* opts = parser->mutable_options();
  opts->flag_prefix = '-';
  opts->allows_equal_arg = true;
  opts->allows_nonspace_arg = true;

  parser->AddBoolFlag("analyze-temporary-dtors");
  parser->AddFlag("checks");
  parser->AddFlag("config");
  parser->AddBoolFlag("dump_config");
  parser->AddBoolFlag("enable-check-profile");
  parser->AddBoolFlag("explain-config");
  parser->AddBoolFlag("fix");
  parser->AddBoolFlag("fix-errors");
  parser->AddFlag("header-filter");
  parser->AddFlag("line-filter");
  parser->AddFlag("p");
  parser->AddBoolFlag("list-checks");
  parser->AddBoolFlag("system-headers");
  parser->AddBoolFlag("warning-as-errors");
}

// static
string ClangTidyFlags::GetCompilerName(absl::string_view /*arg*/) {
  return "clang-tidy";
}

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
  parser.AddFlag("classpath")->SetValueOutputWithCallback(
      nullptr, &class_paths);
  parser.AddFlag("D")->SetValueOutputWithCallback(
      nullptr, &system_properties);
  parser.AddFlag("jar")->SetValueOutputWithCallback(
      nullptr, &input_filenames_);
  parser.AddNonFlag()->SetOutput(&remained_flags);
  parser.Parse(args_);
  unknown_flags_ = parser.unknown_flag_args();

  ParseJavaClassPaths(class_paths, &jar_files_);
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

// ----------------------------------------------------------------------

string GetCxxCompilerVersionFromCommandOutputs(const string& /* command */,
                                               const string& dumpversion,
                                               const string& version) {
  string result(GetFirstLine(dumpversion));
  // Both GCC and clang contain their full version info in the first
  // line of their --version output.
  // E.g., clang version 2.9 (trunk 127176), gcc (Ubuntu 4.4.3-4ubuntu5) 4.4.3
  result += "[" + NormalizeGccVersion(GetFirstLine(version)) + "]";
  return result;
}

string GetFirstLine(const string& buf) {
  size_t pos = buf.find_first_of("\r\n");
  if (pos == string::npos) {
    return buf;
  }
  return buf.substr(0, pos);
}

string NormalizeGccVersion(const string& version) {
  // gcc version string format:
  // <program name> <package version string> <version string>
  // Note: <package version string> is "(<something>)" by default.
  // Then, we can expect the string until '(' is <program name>.
  size_t pos = version.find('(');
  if (pos == string::npos)
    return version;

  const string program_name = version.substr(0, pos);
  // No need to normalize clang.
  if (program_name.find("clang") != string::npos)
    return version;
  // Only need to normalize cc/c++/gcc/g++/<arch>-<os>-gcc/<arch>-<os>-g++.
  // TODO: should we handle <arch>-<os>-cc or so?
  if (program_name.find("g++") == string::npos &&
      program_name.find("gcc") == string::npos &&
      program_name != "c++ " &&
      program_name != "cc ") {
    return version;
  }

  return version.substr(pos);
}

}  // namespace devtools_goma
