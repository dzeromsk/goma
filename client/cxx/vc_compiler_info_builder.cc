// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vc_compiler_info_builder.h"

#include "clang_compiler_info_builder_helper.h"
#include "cmdline_parser.h"
#include "counterz.h"
#include "glog/logging.h"
#include "glog/stl_logging.h"
#include "mypath.h"
#include "path.h"
#include "util.h"
#include "vc_flags.h"

namespace devtools_goma {

namespace {

string GetVCOutputString(const string& cl_exe_path,
                         const string& vcflags,
                         const string& dumb_file,
                         const std::vector<string>& compiler_info_flags,
                         const std::vector<string>& compiler_info_envs,
                         const string& cwd) {
  // The trick we do here gives both include path and predefined macros.
  std::vector<string> argv;
  argv.push_back(cl_exe_path);
  argv.push_back("/nologo");
  argv.push_back(vcflags);
  copy(compiler_info_flags.begin(), compiler_info_flags.end(),
       back_inserter(argv));
  argv.push_back(dumb_file);
  int32_t dummy;  // It is fine to return non zero status code.

  {
    GOMA_COUNTERZ("ReadCommandOutput(/nologo)");
    return ReadCommandOutput(cl_exe_path, argv, compiler_info_envs, cwd,
                             MERGE_STDOUT_STDERR, &dummy);
  }
}

// Since clang-cl is emulation of cl.exe, it might not have meaningful
// clang-cl -dumpversion.  It leads inconsistency of goma's compiler version
// format between clang and clang-cl.  Former expect <dumpversion>[<version>]
// latter cannot have <dumpversion>.
// As a result, let me use different way of getting version string.
// TODO: make this support gcc and use this instead of
//                    GetGccTarget.
string GetClangClSharpOutput(const string& clang_path,
                             const std::vector<string>& compiler_info_flags,
                             const std::vector<string>& compiler_info_envs,
                             const string& cwd) {
  std::vector<string> argv;
  argv.push_back(clang_path);
  copy(compiler_info_flags.begin(), compiler_info_flags.end(),
       back_inserter(argv));
  argv.push_back("-###");
  int32_t status = 0;
  string output;
  {
    GOMA_COUNTERZ("ReadCommandOutput(###)");
    output = ReadCommandOutput(clang_path, argv, compiler_info_envs, cwd,
                               MERGE_STDOUT_STDERR, &status);
  }
  if (status != 0) {
    LOG(ERROR) << "ReadCommandOutput exited with non zero status code."
               << " clang_path=" << clang_path << " status=" << status
               << " argv=" << argv
               << " compiler_info_envs=" << compiler_info_envs << " cwd=" << cwd
               << " output=" << output;
    return "";
  }
  return output;
}

}  // anonymous namespace

void VCCompilerInfoBuilder::SetTypeSpecificCompilerInfo(
    const CompilerFlags& flags,
    const string& local_compiler_path,
    const string& abs_local_compiler_path,
    const std::vector<string>& compiler_info_envs,
    CompilerInfoData* data) const {
  const VCFlags& vc_flags = static_cast<const VCFlags&>(flags);
  if (VCFlags::IsClangClCommand(local_compiler_path)) {
    SetClangClSpecificCompilerInfo(vc_flags, local_compiler_path,
                                   abs_local_compiler_path, compiler_info_envs,
                                   data);
  } else {
    SetClexeSpecificCompilerInfo(vc_flags, local_compiler_path,
                                 abs_local_compiler_path, compiler_info_envs,
                                 data);
  }
}

void VCCompilerInfoBuilder::SetClexeSpecificCompilerInfo(
    const VCFlags& vc_flags,
    const string& local_compiler_path,
    const string& abs_local_compiler_path,
    const std::vector<string>& compiler_info_envs,
    CompilerInfoData* data) const {
  string vcflags_path = GetMyDirectory();
  vcflags_path += "\\vcflags.exe";
  data->mutable_cxx()->set_predefined_macros(data->cxx().predefined_macros() +
                                             vc_flags.implicit_macros());
  if (!VCCompilerInfoBuilder::GetVCVersion(
          abs_local_compiler_path, compiler_info_envs, vc_flags.cwd(),
          data->mutable_version(), data->mutable_target())) {
    AddErrorMessage(
        "Failed to get cl.exe version for " + abs_local_compiler_path, data);
    LOG(ERROR) << data->error_message();
    return;
  }
  if (!GetVCDefaultValues(abs_local_compiler_path, vcflags_path,
                          vc_flags.compiler_info_flags(), compiler_info_envs,
                          vc_flags.cwd(), data->lang(), data)) {
    AddErrorMessage(
        "Failed to get cl.exe system include path "
        " or predifined macros for " +
            abs_local_compiler_path,
        data);
    LOG(ERROR) << data->error_message();
    return;
  }

  // TODO: collect executable resources?
}

void VCCompilerInfoBuilder::SetClangClSpecificCompilerInfo(
    const VCFlags& vc_flags,
    const string& local_compiler_path,
    const string& abs_local_compiler_path,
    const std::vector<string>& compiler_info_envs,
    CompilerInfoData* data) const {
  const string& lang_flag = vc_flags.is_cplusplus() ? "/TP" : "/TC";
  if (!ClangCompilerInfoBuilderHelper::SetBasicCompilerInfo(
          local_compiler_path, vc_flags.compiler_info_flags(),
          compiler_info_envs, vc_flags.cwd(), lang_flag,
          vc_flags.resource_dir(), vc_flags.is_cplusplus(), false, data)) {
    DCHECK(data->has_error_message());
    // If error occurred in SetBasicCompilerInfo, we do not need to
    // continue.
    return;
  }

  const string& sharp_output =
      GetClangClSharpOutput(local_compiler_path, vc_flags.compiler_info_flags(),
                            compiler_info_envs, vc_flags.cwd());
  if (sharp_output.empty() ||
      !ClangCompilerInfoBuilderHelper::ParseClangVersionTarget(
          sharp_output, data->mutable_version(), data->mutable_target())) {
    AddErrorMessage(
        "Failed to get version string for " + abs_local_compiler_path, data);
    LOG(ERROR) << data->error_message();
    return;
  }

  // --- Experimental. Add compiler resource.
  {
    std::vector<string> resource_paths_to_collect;

    // local compiler.
    resource_paths_to_collect.push_back(local_compiler_path);

    // TODO: Not sure the whole list of dlls to run clang-cl.exe
    // correctly. However, `dumpbin /DEPENDENTS clang-cl.exe` prints nothing
    // special, so currently I don't collect dlls. Some dlls might be necessary
    // to use some feature.

    for (const auto& resource_path : resource_paths_to_collect) {
      CompilerInfoData::ResourceInfo r;
      if (!CompilerInfoBuilder::ResourceInfoFromPath(
              vc_flags.cwd(), resource_path,
              CompilerInfoData::EXECUTABLE_BINARY, &r)) {
        AddErrorMessage("failed to get resource info for " + resource_path,
                        data);
        return;
      }
      *data->add_resource() = std::move(r);
    }
  }
}

/* static */
bool VCCompilerInfoBuilder::ParseVCVersion(const string& vc_logo,
                                           string* version,
                                           string* target) {
  // VC's logo format:
  // ... Version 16.00.40219.01 for 80x86
  // so we return cl 16.00.40219.01
  string::size_type pos = vc_logo.find("Version ");
  string::size_type pos2 = vc_logo.find(" for");
  string::size_type pos3 = vc_logo.find("\r");
  if (pos == string::npos || pos2 == string::npos || pos3 == string::npos ||
      pos2 < pos || pos3 < pos2) {
    LOG(INFO) << "Unable to parse cl.exe output."
              << " vc_logo=" << vc_logo;
    return false;
  }
  pos += 8;  // 8: length of "Version "
  *version = vc_logo.substr(pos, pos2 - pos);
  *target = vc_logo.substr(pos2 + 5, pos3 - pos2 - 5);
  return true;
}

/* static */
bool VCCompilerInfoBuilder::GetVCVersion(const string& cl_exe_path,
                                         const std::vector<string>& env,
                                         const string& cwd,
                                         string* version,
                                         string* target) {
  std::vector<string> argv;
  argv.push_back(cl_exe_path);
  int32_t status = 0;
  string vc_logo;
  {
    GOMA_COUNTERZ("ReadCommandOutput(vc version)");
    vc_logo = ReadCommandOutput(cl_exe_path, argv, env, cwd,
                                MERGE_STDOUT_STDERR, &status);
  }
  if (status != 0) {
    LOG(ERROR) << "ReadCommandOutput exited with non zero status code."
               << " cl_exe_path=" << cl_exe_path
               << " status=" << status
               << " argv=" << argv
               << " env=" << env
               << " cwd=" << cwd
               << " vc_logo=" << vc_logo;
    return false;
  }
  if (!ParseVCVersion(vc_logo, version, target)) {
    LOG(ERROR) << "Failed to parse VCVersion."
               << " cl_exe_path=" << cl_exe_path
               << " status=" << status
               << " argv=" << argv
               << " env=" << env
               << " cwd=" << cwd
               << " vc_logo=" << vc_logo;
    return false;
  }
  return true;
}

/* static */
bool VCCompilerInfoBuilder::ParseVCOutputString(
    const string& output,
    std::vector<string>* include_paths,
    string* predefined_macros) {
  std::vector<string> args;
  // |output| doesn't contains command name, so adds "cl.exe" here.
  args.push_back("cl.exe");
  if (!ParseWinCommandLineToArgv(output, &args)) {
    LOG(ERROR) << "Fail parse cmdline:" << output;
    return false;
  }

  VCFlags flags(args, ".");
  if (!flags.is_successful()) {
    LOG(ERROR) << "ParseVCOutput error:" << flags.fail_message();
    return false;
  }

  copy(flags.include_dirs().begin(), flags.include_dirs().end(),
       back_inserter(*include_paths));

  if (predefined_macros == nullptr)
    return true;
  std::ostringstream ss;
  for (const auto& elm : flags.commandline_macros()) {
    const string& macro = elm.first;
    DCHECK(elm.second) << macro;
    size_t found = macro.find('=');
    if (found == string::npos) {
      ss << "#define " << macro << "\n";
    } else {
      ss << "#define " << macro.substr(0, found) << " "
         << macro.substr(found + 1) << "\n";
    }
  }
  *predefined_macros += ss.str();
  return true;
}

// static
bool VCCompilerInfoBuilder::GetVCDefaultValues(
    const string& cl_exe_path,
    const string& vcflags_path,
    const std::vector<string>& compiler_info_flags,
    const std::vector<string>& compiler_info_envs,
    const string& cwd,
    const string& lang,
    CompilerInfoData* compiler_info) {
  // VC++ accepts two different undocumented flags to dump all predefined values
  // in preprocessor.  /B1 is for C and /Bx is for C++.
  string vc_cpp_flags = "/Bx";
  string vc_c_flags = "/B1";
  vc_cpp_flags += vcflags_path;
  vc_c_flags += vcflags_path;

  // It does not matter that non-exist-file.cpp/.c is on disk or not.  VCFlags
  // will error out cl.exe and display the information we want before actually
  // opening that file.
  string output_cpp =
      GetVCOutputString(cl_exe_path, vc_cpp_flags, "non-exist-file.cpp",
                        compiler_info_flags, compiler_info_envs, cwd);
  string output_c =
      GetVCOutputString(cl_exe_path, vc_c_flags, "non-exist-file.c",
                        compiler_info_flags, compiler_info_envs, cwd);

  std::vector<string> cxx_system_include_paths;
  if (!VCCompilerInfoBuilder::ParseVCOutputString(
          output_cpp, &cxx_system_include_paths,
          lang == "c++"
              ? compiler_info->mutable_cxx()->mutable_predefined_macros()
              : nullptr)) {
    return false;
  }
  for (const auto& p : cxx_system_include_paths) {
    compiler_info->mutable_cxx()->add_cxx_system_include_paths(p);
  }
  std::vector<string> system_include_paths;
  if (!VCCompilerInfoBuilder::ParseVCOutputString(
          output_c, &system_include_paths,
          lang == "c"
              ? compiler_info->mutable_cxx()->mutable_predefined_macros()
              : nullptr)) {
    return false;
  }
  for (const auto& p : system_include_paths) {
    compiler_info->mutable_cxx()->add_system_include_paths(p);
  }
  return true;
}

}  // namespace devtools_goma
