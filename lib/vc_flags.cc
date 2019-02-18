// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/vc_flags.h"

#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "base/path.h"
#include "glog/logging.h"
#include "glog/stl_logging.h"
#include "lib/cmdline_parser.h"
#include "lib/compiler_flags.h"
#include "lib/file_helper.h"
#include "lib/flag_parser.h"
#include "lib/known_warning_options.h"
#include "lib/path_resolver.h"
#include "lib/path_util.h"
using std::string;

namespace devtools_goma {

namespace {

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
  // Note file::Basename does not understand "\\" as a path delimiter
  // on non-Windows.
  return absl::AsciiStrToLower(GetBasename(in));
}

}  // namespace

class Win32PathNormalizer : public FlagParser::Callback {
 public:
  // Returns parsed flag value of value for flag.
  string ParseFlagValue(const FlagParser::Flag& flag,
                        const string& value) override;
};

string Win32PathNormalizer::ParseFlagValue(const FlagParser::Flag& /* flag */,
                                           const string& value) {
  return NormalizeWin32Path(value);
}

/* static */
bool VCFlags::IsVCCommand(absl::string_view arg) {
  // As a substring "cl" would be found in other commands like "clang" or
  // "nacl-gcc".  Also, "cl" is case-insensitive on Windows and can be postfixed
  // with ".exe".
  const string& s = ToNormalizedBasename(arg);
  return s == "cl.exe" || s == "cl";
}

/* static */
bool VCFlags::IsClangClCommand(absl::string_view arg) {
  const string& s = ToNormalizedBasename(arg);
  return s == "clang-cl.exe" || s == "clang-cl";
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
    : CxxFlags(args, cwd),
      is_cplusplus_(true),
      ignore_stdinc_(false),
      has_Brepro_(false),
      require_mspdbserv_(false) {
  bool result =
      ExpandArgs(cwd, args, &expanded_args_, &optional_input_filenames_);
  if (!result) {
    Fail("Unable to expand args");
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
  FlagParser::Flag* flag_resource_dir = nullptr;
  FlagParser::Flag* flag_fsanitize = parser.AddFlag("fsanitize");
  FlagParser::Flag* flag_fno_sanitize_blacklist = nullptr;
  FlagParser::Flag* flag_fsanitize_blacklist = nullptr;
  FlagParser::Flag* flag_mllvm = parser.AddFlag("mllvm");
  FlagParser::Flag* flag_isystem = parser.AddFlag("isystem");
  // TODO: check -iquote?
  // http://clang.llvm.org/docs/UsersManual.html#id8
  FlagParser::Flag* flag_imsvc = parser.AddFlag("imsvc");
  FlagParser::Flag* flag_std = parser.AddFlag("std");  // e.g. -std=c11
  FlagParser::Flag* flag_no_canonical_prefixes =
      parser.AddBoolFlag("no-canonical-prefixes");
  FlagParser::Flag* flag_target = parser.AddFlag("target");
  FlagParser::Flag* flag_hyphen_target = parser.AddFlag("-target");
  std::vector<string> incremental_linker_flags;
  parser.AddBoolFlag("Brepro")->SetOutput(&incremental_linker_flags);
  parser.AddBoolFlag("Brepro-")->SetOutput(&incremental_linker_flags);
  if (compiler_name() == "clang-cl") {
    flag_m->SetOutput(&compiler_info_flags_);
    flag_fmsc_version->SetOutput(&compiler_info_flags_);
    flag_fms_compatibility_version->SetOutput(&compiler_info_flags_);
    flag_resource_dir = parser.AddFlag("resource-dir");
    flag_resource_dir->SetOutput(&compiler_info_flags_);
    flag_fsanitize->SetOutput(&compiler_info_flags_);
    // TODO: do we need to support more sanitize options?
    flag_fno_sanitize_blacklist = parser.AddBoolFlag("fno-sanitize-blacklist");
    flag_fsanitize_blacklist = parser.AddFlag("fsanitize-blacklist=");
    flag_mllvm->SetOutput(&compiler_info_flags_);
    flag_isystem->SetOutput(&compiler_info_flags_);
    flag_imsvc->SetOutput(&compiler_info_flags_);
    flag_std->SetOutput(&compiler_info_flags_);
    flag_no_canonical_prefixes->SetOutput(&compiler_info_flags_);
    flag_target->SetOutput(&compiler_info_flags_);
    flag_hyphen_target->SetOutput(&compiler_info_flags_);

    parser.AddBoolFlag("w")->SetOutput(&compiler_info_flags_);

    // Make these understood.
    parser.AddBoolFlag(
        "fansi-escape-codes");  // Use ANSI escape codes for diagnostics
    parser.AddBoolFlag(
        "fdiagnostics-absolute-paths");  // Print absolute paths in diagnostics

    // Make it understand Xclang.
    parser.AddFlag("Xclang")->SetOutput(&compiler_info_flags_);

    parser.AddBoolFlag("mincremental-linker-compatible")
        ->SetOutput(&incremental_linker_flags);
    parser.AddBoolFlag("mno-incremental-linker-compatible")
        ->SetOutput(&incremental_linker_flags);
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
        GetExtension(input_filenames_[0]) == "c") &&
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

  if (flag_resource_dir && flag_resource_dir->seen()) {
    resource_dir_ = flag_resource_dir->GetLastValue();
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
    if (last == "-mno-incremental-linker-compatible" || last == "/Brepro" ||
        last == "-Brepro") {
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
      output_files_.push_back(ComposeOutputFilePath(
          input_filenames_[0], force_output, new_extension));
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
      "PATHEXT=", "SystemDrive=", "SystemRoot=",
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
      "INCLUDE=",      "LIB=",          "MSC_CMD_FLAGS=",
      "VCINSTALLDIR=", "VSINSTALLDIR=", "WindowsSdkDir=",
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
  parser->AddBoolFlag("?");     // alias of help
  parser->AddPrefixFlag("AI");  // specifies a directory to search for #using
  parser->AddPrefixFlag("analyze");  // enable code analysis
  parser->AddPrefixFlag("arch");     // specifies the architecture for code gen
  parser->AddBoolFlag("await");      // enable resumable functions extension

  parser->AddBoolFlag("bigobj");  // increases the num of addressable sections

  parser->AddBoolFlag("C");  // preserves comments during preprocessing
  parser->AddBoolFlag("c");  // compile only
  parser->AddPrefixFlag("cgthreads");  // specify num of cl.exe threads
  parser->AddPrefixFlag("clr");
  parser->AddPrefixFlag("constexpr");  // constexpr options

  parser->AddFlag("D");          // define macro
  parser->AddPrefixFlag("doc");  // process documentation comments
  // /diagnostics:<args,...> controls the format of diagnostic messages
  parser->AddPrefixFlag("diagnostics:");

  parser->AddBoolFlag("E");     // preprocess to stdout
  parser->AddPrefixFlag("EH");  // exception ahdling model
  parser->AddBoolFlag("EP");    // disable linemarker output and preprocess
  parser->AddPrefixFlag("errorReport");

  parser->AddFlag("F");            // set stack size
  parser->AddPrefixFlag("favor");  // optimize for architecture specifics
  parser->AddPrefixFlag("FA");     // output assembly code file
  parser->AddPrefixFlag("Fa");     // output assembly code to this file
  parser->AddBoolFlag("FC");    // full path of source code in diagnostic text
  parser->AddPrefixFlag("Fd");  // set pdb file name
  parser->AddPrefixFlag("Fe");  // set output executable file or directory
  parser->AddFlag("FI");        // include file before parsing
  parser->AddPrefixFlag("Fi");  // set preprocess output file name
  parser->AddPrefixFlag("Fm");  // set map file name
  parser->AddPrefixFlag("Fo");  // set output object file or directory
  parser->AddPrefixFlag("fp");  // specify floating proint behavior
  parser->AddPrefixFlag("Fp");  // set pch file name
  parser->AddPrefixFlag("FR");  // .sbr file
  parser->AddPrefixFlag("Fr");  // .sbr file without info on local var
  parser->AddBoolFlag("FS");    // force synchronous PDB writes
  parser->AddFlag("FU");        // #using
  parser->AddBoolFlag("Fx");    // merges injected code

  parser->AddBoolFlag("GA");   // optimize for win app
  parser->AddBoolFlag("Gd");   // calling convention
  parser->AddBoolFlag("Ge");   // enable stack probes
  parser->AddBoolFlag("GF");   // enable string pool
  parser->AddBoolFlag("GF-");  // disable string pooling
  parser->AddBoolFlag("GH");   // call hook function _pexit
  parser->AddBoolFlag("Gh");   // call hook function _penter
  parser->AddBoolFlag("GL");   // enables whole program optimization
  parser->AddBoolFlag("GL-");
  parser->AddBoolFlag("Gm");  // enables minimal rebuild
  parser->AddBoolFlag("Gm-");
  parser->AddBoolFlag("GR");   // enable emission of RTTI data
  parser->AddBoolFlag("GR-");  // disable emission of RTTI data
  parser->AddBoolFlag("Gr");   // calling convention
  parser->AddBoolFlag("GS");   // buffer security check
  parser->AddBoolFlag("GS-");
  parser->AddPrefixFlag("Gs");       // controls stack probes
  parser->AddBoolFlag("GT");         // fibre safety thread-local storage
  parser->AddBoolFlag("guard:cf");   // enable control flow guard
  parser->AddBoolFlag("guard:cf-");  // disable control flow guard
  parser->AddBoolFlag("Gv");         // calling convention
  parser->AddBoolFlag("Gw");         // put each data item in its own section
  parser->AddBoolFlag("Gw-");  // don't put each data item in its own section
  parser->AddBoolFlag("GX");   // enable exception handling
  parser->AddBoolFlag("Gy");   // put each function in its own section
  parser->AddBoolFlag("Gy-");  // don't put each function in its own section
  parser->AddBoolFlag("GZ");   // same as /RTC
  parser->AddBoolFlag("Gz");   // calling convention

  parser->AddPrefixFlag("H");         // restricts the length of external names
  parser->AddBoolFlag("HELP");        // alias of help
  parser->AddBoolFlag("help");        // display available options
  parser->AddBoolFlag("homeparams");  // copy register parameters to stack
  parser->AddBoolFlag("hotpatch");    // create hotpatchable image

  parser->AddFlag("I");  // add directory to include search path

  parser->AddBoolFlag("J");  // make char type unsinged

  parser->AddBoolFlag("kernel");  // create kernel mode binary
  parser->AddBoolFlag("kernel-");

  parser->AddBoolFlag("LD");   // create DLL
  parser->AddBoolFlag("LDd");  // create debug DLL
  parser->AddFlag("link");     // forward options to the linker
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
  parser->AddPrefixFlag("std:");        // C++ standard version

  parser->AddFlag("Tc");      // specify a C source file
  parser->AddBoolFlag("TC");  // treat all source files as C
  parser->AddFlag("Tp");      // specify a C++ source file
  parser->AddBoolFlag("TP");  // treat all source files as C++

  parser->AddFlag("U");      // undefine macro
  parser->AddBoolFlag("u");  // remove all predefined macros

  parser->AddPrefixFlag("V");   // Sets the version string
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

  parser->AddBoolFlag("Y-");    // ignore precompiled header
  parser->AddPrefixFlag("Yc");  // create precompiled header
  parser->AddBoolFlag("Yd");    // place debug information
  parser->AddPrefixFlag("Yl");  // inject PCH reference for debug library
  parser->AddPrefixFlag("Yu");  // use precompiled header

  parser->AddBoolFlag("Z7");    // debug information format
  parser->AddBoolFlag("Za");    // disable language extensions
  parser->AddPrefixFlag("Zc");  // conformance
  // line number only debug information; b/30077868
  parser->AddBoolFlag("Zd");
  parser->AddBoolFlag("Ze");          // enable microsoft extensions
  parser->AddBoolFlag("ZH:SHA_256");  // use SHA256 for file checksum
  parser->AddBoolFlag("Zg");          // generate function prototype
  parser->AddBoolFlag("ZI");          // produce pdb
  parser->AddBoolFlag("Zi");          // enable debug information
  parser->AddBoolFlag("Zl");          // omit default library name
  parser->AddPrefixFlag("Zm");        // specify precompiled header memory limit
  parser->AddBoolFlag("Zo");          // enhance optimized debugging
  parser->AddBoolFlag("Zo-");
  parser->AddPrefixFlag("Zp");  // default maximum struct packing alignment
  // Zp1, Zp2, Zp4, Zp8, Zp16
  parser->AddFlag("Zs");        // syntax check only
  parser->AddPrefixFlag("ZW");  // windows runtime compilation

  // New flags from VS2015 Update 2
  parser->AddPrefixFlag("source-charset:");     // set source character set.
  parser->AddPrefixFlag("execution-charset:");  // set execution character set.
  parser->AddBoolFlag("utf-8");             // set both character set to utf-8.
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
  parser->AddPrefixFlag(
      "fms-compatibility-version=");  // -fms-compatibility-version=<arg>
  parser->AddFlag("fsanitize");
  parser->AddBoolFlag("fcolor-diagnostics");  // Use color for diagnostics
  parser->AddBoolFlag(
      "fno-standalone-debug");  // turn on the vtable-based optimization
  parser->AddBoolFlag(
      "fstandalone-debug");  // turn off the vtable-based optimization
  parser->AddBoolFlag("gcolumn-info");       // debug information (-g)
  parser->AddBoolFlag("gline-tables-only");  // debug information (-g)
  parser->AddFlag("Xclang");
  parser->AddFlag("isystem");
  parser->AddPrefixFlag("-analyze");  // enable code analysis (--analyze)
  parser->AddFlag("target");
  parser->AddFlag("-target");

  opts->flag_prefix = '-';
  opts->alt_flag_prefix = '/';
}

// static
bool VCFlags::ExpandArgs(const string& cwd,
                         const std::vector<string>& args,
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

}  // namespace devtools_goma
