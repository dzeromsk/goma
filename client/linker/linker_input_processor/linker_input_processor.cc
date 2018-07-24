// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "linker_input_processor.h"

#ifndef _WIN32
#include <ar.h>
#ifdef __linux__
#include <elf.h>
#endif
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#else
#include "config_win.h"
#endif

#include <memory>
#include <set>
#include <string>
#include <vector>

#include <glog/logging.h>
#include <glog/stl_logging.h>

#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "arfile.h"
#include "cmdline_parser.h"
#include "compiler_flags.h"
#include "compiler_flags_parser.h"
#include "compiler_info.h"
#include "compiler_specific.h"
#include "content.h"
#include "elf_parser.h"
#include "framework_path_resolver.h"
#include "gcc_flags.h"
#include "ioutil.h"
#include "library_path_resolver.h"
#include "linker_script_parser.h"
#include "path.h"
#include "util.h"

MSVC_PUSH_DISABLE_WARNING_FOR_PROTO()
#include "prototmp/goma_data.pb.h"
MSVC_POP_WARNING()

#ifdef __linux
// TODO: port elf.h in MacOSX and eliminate this ifdef.
// we want to run android cross compile (which uses ELF) on MacOSX.
# include "elf_parser.h"
#endif

#ifdef __MACH__
# include "mach_o_parser.h"
# include <mach-o/fat.h>
# include <mach-o/loader.h>
#endif

#ifndef ELFMAG
# define ELFMAG "\177ELF"
# define SELFMAG 4
#endif

#define TARMAG "!<thin>\n"  // String that begins an thin archive file.
#define STARMAG 8           // Size of that string.

#ifdef _WIN32
// Copied from GNU C ar.h
#define ARMAG   "!<arch>\n"     /* String that begins an archive file.  */
#define SARMAG  8               /* Size of that string.  */
#define SEP '\\'
#else
#define SEP '/'
#endif

namespace {
#ifdef __MACH__
const int kMaxRecursion = 10;
#endif
}

namespace devtools_goma {

LinkerInputProcessor::LinkerInputProcessor(const std::vector<string>& args,
                                           const string& current_directory)
    : flags_(CompilerFlagsParser::New(args, current_directory)),
      library_path_resolver_(new LibraryPathResolver(current_directory)),
      framework_path_resolver_(new FrameworkPathResolver(current_directory)) {}

LinkerInputProcessor::LinkerInputProcessor(
    const string& current_directory)
    : library_path_resolver_(new LibraryPathResolver(current_directory)),
      framework_path_resolver_(new FrameworkPathResolver(current_directory)) {
}


LinkerInputProcessor::~LinkerInputProcessor() {
}

bool LinkerInputProcessor::GetInputFilesAndLibraryPath(
    const CompilerInfo& /* compiler_info */,
    const CommandSpec& command_spec,
    std::set<string>* input_files,
    std::vector<string>* library_paths) {
  if (flags_.get() == nullptr) {
    return false;
  }
  std::vector<string> driver_args;
  std::vector<string> driver_envs;
  if (!CaptureDriverCommandLine(command_spec, &driver_args, &driver_envs)) {
    return false;
  }
  VLOG(1) << "driver command line:" << driver_args;
  std::vector<string> input_paths;
  ParseDriverCommandLine(driver_args, &input_paths);
  VLOG(2) << "input paths:" << input_paths;
  VLOG(1) << "driver environment:" << driver_envs;
  // TODO: make sure ld do not need to see LIBRARY_PATH.
  GetLibraryPath(driver_envs, library_paths);
  VLOG(1) << "my library path is: " << *library_paths;

  // Note: input_paths could be modified in this loop, you must not
  // use ranged-for here.  Or, you may see use after free.
  for (size_t i = 0; i < input_paths.size(); ++i) {
    if (input_paths[i].empty())
      continue;
    const string filename =
        file::JoinPathRespectAbsolute(flags_->cwd(), input_paths[i]);
    VLOG(1) << "Input: " << filename;
    if (!input_files->insert(filename).second) {
      VLOG(2) << "already checked:" << filename;
      continue;
    }
    switch (CheckFileType(filename)) {
      case THIN_ARCHIVE_FILE:
        ParseThinArchive(filename, input_files);
        break;
      case OTHER_FILE:
        TryParseLinkerScript(filename, &input_paths);
        break;
      case ELF_BINARY_FILE:
        TryParseElfNeeded(filename, &input_paths);
        break;
      case MACHO_FAT_FILE:
#ifdef __MACH__
        TryParseMachONeeded(filename, kMaxRecursion, input_files);
#endif
        break;
      case MACHO_OBJECT_FILE:
        FALLTHROUGH_INTENDED;
      case ARCHIVE_FILE:
        FALLTHROUGH_INTENDED;
      case BAD_FILE:
        break;
    }
  }
  VLOG(2) << "input files:" << *input_files;
  return true;
}

bool LinkerInputProcessor::CaptureDriverCommandLine(
    const CommandSpec& command_spec,
    std::vector<string>* driver_args,
    std::vector<string>* driver_envs) {
  CHECK(flags_.get());
  std::vector<string> dump_args;
  dump_args.push_back(command_spec.local_compiler_path());
  dump_args.push_back("-###");
  for (size_t i = 1; i < flags_->args().size(); ++i) {
    dump_args.push_back(flags_->args()[i]);
  }
  std::vector<string> env;
  env.push_back("LC_ALL=C");
  int32_t status = -1;
  const string dump_output =
      ReadCommandOutput(dump_args[0], dump_args, env, flags_->cwd(),
                        MERGE_STDOUT_STDERR, &status);
  if (status != 0) {
    LOG(ERROR) << "command failed with exit=" << status
               << " args=" << dump_args
               << " env=" << env
               << " cwd=" << flags_->cwd();
    return false;
  }

  return ParseDumpOutput(dump_output, driver_args, driver_envs);
}

/* static */
bool LinkerInputProcessor::ParseDumpOutput(
    const string& dump_output,
    std::vector<string>* driver_args,
    std::vector<string>* driver_envs) {
  // dump_output (gcc -### output) will be
  // gcc's specs, important envs (COMPILER_PATH, LIBRARY_PATH, etc) and
  // command to be executed, starting SPACE, following command arguments
  // in double quotes.
  absl::string_view buf(dump_output);
  size_t pos;
  std::vector<string> envs;

  do {
    pos = buf.find_first_of("\n");
    absl::string_view line = buf.substr(0, pos);
    VLOG(3) << "ParseDumpOutput: " << line;
    buf.remove_prefix(pos + 1);

    if (absl::StartsWith(line, "LIBRARY_PATH=") ||
        absl::StartsWith(line, "COMPILER_PATH=")) {
      driver_envs->push_back(string(line));
    }
    if (line[0] == ' ') {
      driver_args->clear();
      if (!ParsePosixCommandLineToArgv(line, driver_args))
        return false;
    }
  } while (pos != absl::string_view::npos);

  if (driver_args->empty())
    return false;
  return true;
}

void LinkerInputProcessor::ParseDriverCommandLine(
    const std::vector<string>& args,
    std::vector<string>* input_paths) {
  // TODO: make sure that changing file order is acceptable.
  // Before: as-is except -l options resolved by latter -L options.
  // Now: files without flags -> files with flags -> -l options ->
  //      -framework options.
  FlagParser driver_flag;
  driver_flag.mutable_options()->flag_prefix = '-';
  driver_flag.mutable_options()->allows_equal_arg = true;
  driver_flag.mutable_options()->allows_nonspace_arg = true;
  driver_flag.mutable_options()->has_command_name = true;

  // Skip values.
  driver_flag.AddFlag("z");
  driver_flag.AddFlag("m");
  driver_flag.AddFlag("o");  // we need this for incremental link?
  // For Mac.
  driver_flag.AddFlag("macosx_version_min");
  driver_flag.AddFlag("exported_symbol");
  driver_flag.AddFlag("install_name");
  driver_flag.AddFlag("dylib_install_name");

  // For input files.
  bool static_link = false;
  bool no_default_searchpath = false;
  std::vector<string> searchdirs;
  std::vector<string> lvalues;
  std::vector<string> frameworkpaths;
  std::vector<string> frameworks;
  std::vector<string> files_to_find;
  driver_flag.AddBoolFlag("static")->SetSeenOutput(&static_link);
  driver_flag.AddFlag("L")->SetValueOutputWithCallback(nullptr, &searchdirs);
  driver_flag.AddFlag("l")->SetValueOutputWithCallback(nullptr, &lvalues);
  driver_flag.AddFlag("dynamic-linker")->SetValueOutputWithCallback(
      nullptr, &files_to_find);
  driver_flag.AddFlag("F")->SetValueOutputWithCallback(
      nullptr, &frameworkpaths);
  driver_flag.AddFlag("framework")->SetValueOutputWithCallback(
      nullptr, &frameworks);
  driver_flag.AddBoolFlag("Z")->SetSeenOutput(&no_default_searchpath);
  // sysroot: replaced with '=' in search path. (Linux)
  FlagParser::Flag* flag_sysroot = driver_flag.AddFlag("-sysroot");
  // syslibroot: prefix for all search paths. (Mac)
  FlagParser::Flag* flag_syslibroot = driver_flag.AddFlag("syslibroot");
  FlagParser::Flag* flag_arch = driver_flag.AddFlag("arch");
  driver_flag.AddNonFlag()->SetOutput(input_paths);
  // Don't count soname's value as input files.
  driver_flag.AddFlag("soname");
  // TODO: -T (--script) support?

  driver_flag.Parse(args);
  library_path_resolver_->SetSysroot(flag_sysroot->GetLastValue());
  library_path_resolver_->SetSyslibroot(flag_syslibroot->GetLastValue());
  framework_path_resolver_->SetSyslibroot(flag_syslibroot->GetLastValue());
  library_path_resolver_->AppendSearchdirs(searchdirs);
  framework_path_resolver_->AppendSearchpaths(frameworkpaths);
  arch_ = flag_arch->GetLastValue();
  if (no_default_searchpath)
    LOG(WARNING) << "sorry -Z is not supported yet.";

  // Start finding -lx from -L dir.
  if (static_link)
    library_path_resolver_->PreventSharedLibrary();

  for (const auto& file : files_to_find) {
    string path = library_path_resolver_->FindByFullname(file);
    if (path.empty()) {
      LOG(WARNING) << "file not found:" << file;
      continue;
    }
    input_paths->push_back(path);
  }

  for (const auto& lvalue : lvalues) {
    string path = library_path_resolver_->ExpandLibraryPath(lvalue);
    if (path.empty()) {
      LOG(WARNING) << "library not found -l" << lvalue;
      continue;
    }
    input_paths->push_back(path);
  }
  for (const auto& framework : frameworks) {
    string path = framework_path_resolver_->ExpandFrameworkPath(framework);
    if (path.empty()) {
      LOG(WARNING) << "framework not found -framework " << framework;
      continue;
    }
    input_paths->push_back(path);
  }
}

void LinkerInputProcessor::GetLibraryPath(
    const std::vector<string>& envs,
    std::vector<string>* library_paths) {
  absl::string_view libpath_string;
  static const char* kPathPrefix = "LIBRARY_PATH=";
  for (const auto& env : envs) {
    if (absl::StartsWith(env, kPathPrefix)) {
      libpath_string = absl::string_view(env.c_str(), env.size());
      libpath_string.remove_prefix(strlen(kPathPrefix));
      break;
    }
  }

  // Use -L if LIBRARY_PATH env. not found. (for clang)
  if (libpath_string.empty()) {
    const std::vector<string>& searchdirs =
        library_path_resolver_->searchdirs();
    library_paths->assign(searchdirs.begin(), searchdirs.end());
    return;
  }

  // Normalize LIBRARY_PATH and append to |library_paths|.
  size_t pos;
  const string& cwd = library_path_resolver_->cwd();
  do {
    pos = libpath_string.find_first_of(":");
    absl::string_view entry = libpath_string.substr(0, pos);
    // some/thing/ and some/thing should be the same path.
    if (absl::EndsWith(entry, "/")) {
      entry.remove_suffix(1);
    }
    // Consider relative path, which might not be needed.
    library_paths->push_back(
        file::JoinPathRespectAbsolute(cwd, string(entry)));
    libpath_string.remove_prefix(pos + 1);
  } while (pos != absl::string_view::npos);
}

/* static */
LinkerInputProcessor::FileType LinkerInputProcessor::CheckFileType(
    const string& path) {
  ScopedFd fd(ScopedFd::OpenForRead(path));
  if (!fd.valid())
    return BAD_FILE;
  char buf[8];
  for (int r, len = 0; len < 8;) {
    r = fd.Read(buf + len, sizeof(buf) - len);
    if (r < 0) {
      PLOG(ERROR) << "read " << path;
      return BAD_FILE;
    }
    if (r == 0)
      return OTHER_FILE;
    len += r;
  }
  if (memcmp(buf, ELFMAG, SELFMAG) == 0)
    return ELF_BINARY_FILE;
  if (memcmp(buf, TARMAG, STARMAG) == 0)
    return THIN_ARCHIVE_FILE;
  if (memcmp(buf, ARMAG, SARMAG) == 0)
    return ARCHIVE_FILE;
#ifdef __MACH__
  uint32_t* header = reinterpret_cast<uint32_t*>(buf);
  if (*header == FAT_MAGIC || *header == FAT_CIGAM) {
    if (absl::EndsWith(path, ".a"))
      return ARCHIVE_FILE;
    else
      return MACHO_FAT_FILE;
  }
  if (*header == MH_MAGIC || *header == MH_CIGAM ||
      *header == MH_MAGIC_64 || *header == MH_CIGAM_64)
    return MACHO_OBJECT_FILE;
#endif

  return OTHER_FILE;
}

/* static */
void LinkerInputProcessor::ParseThinArchive(
    const string& filename, std::set<string>* input_files) {
  VLOG(1) << "thin archive:" << filename;
  ArFile ar(filename);
  DCHECK(ar.Exists()) << filename;
  DCHECK(ar.IsThinArchive()) << filename;
  size_t pos = filename.rfind(SEP);
  DCHECK_NE(string::npos, pos) << filename;
  const string ar_dir = filename.substr(0, pos);
  VLOG(1) << "ar_dir:" << ar_dir;
  std::vector<ArFile::EntryHeader> entries;
  ar.GetEntries(&entries);
  for (size_t i = 0; i < entries.size(); ++i) {
    const string entry_name = file::JoinPath(ar_dir, entries[i].ar_name);
    VLOG(1) << "entry[" << i << "] " << entries[i].ar_name
            << " " << entry_name;
    input_files->insert(entry_name);
  }
}

void LinkerInputProcessor::TryParseLinkerScript(
    const string& filename, std::vector<string>* input_paths) {
  VLOG(1) << "Try linker script:" << filename;
  LinkerScriptParser parser(
      Content::CreateFromFile(filename),
      library_path_resolver_->cwd(),
      library_path_resolver_->searchdirs(),
      library_path_resolver_->sysroot());
  if (parser.Parse()) {
    VLOG(1) << "linker script:" << filename;
    if (!parser.startup().empty())
      input_paths->push_back(parser.startup());
    if (!parser.inputs().empty()) {
      for (size_t i = 0; i < parser.inputs().size(); ++i) {
        input_paths->push_back(parser.inputs()[i]);
      }
    }
    library_path_resolver_->AppendSearchdirs(parser.searchdirs());
  } else {
    VLOG(1) << "not linker script:" << filename;
  }
}

void LinkerInputProcessor::TryParseElfNeeded(
    const string& filename,
    std::vector<string>* input_paths) {
#ifdef __linux__
  std::unique_ptr<ElfParser> elf(ElfParser::NewElfParser(filename));
  if (elf == nullptr || !elf->valid())
    return;
  std::vector<string> needed;
  if (!elf->ReadDynamicNeeded(&needed))
    return;
  for (const auto& path : needed) {
    string pathname = library_path_resolver_->FindBySoname(path);
    if (pathname.empty()) {
      LOG(WARNING) << "so not found:" << path << " needed by " << filename;
      continue;
    }
    input_paths->push_back(pathname);
  }
#elif defined(_WIN32)
  UNREFERENCED_PARAMETER(filename);
  UNREFERENCED_PARAMETER(input_paths);
#endif
}

#ifdef __MACH__
// Although TryParseElfNeeded and TryParseMachONeeded does almost the same,
// I think two shared object types has significant difference.
// Elf does not need to be investigated recursively, but MachO dylib does.
void LinkerInputProcessor::TryParseMachONeeded(
    const string& filename,
    const int max_recursion,
    std::set<string>* input_files) {
  MachO macho(filename);
  if (!macho.valid())
    return;

  std::vector<MachO::DylibEntry> needed;
  if (!macho.GetDylibs(arch_, &needed))
    return;

  for (size_t i = 0; i < needed.size(); ++i) {
    string dylib_name = needed[i].name;

    if (dylib_name[0] == '/')
      dylib_name = file::JoinPath(library_path_resolver_->syslibroot(),
                                  dylib_name);

    // If not found with the absolute path, should be searched. (unlikely)
    if (dylib_name[0] != '/' || (access(dylib_name.c_str(), R_OK) != 0)) {
      const string path_name = library_path_resolver_->FindBySoname(
          string(file::Basename(dylib_name)));
      if (path_name.empty()) {
        LOG(WARNING) << "dylib not found:" << dylib_name
                     << " needed by " << filename;
        continue;
      }
      dylib_name = path_name;
    }

    if (!input_files->insert(dylib_name).second) {
      VLOG(2) << "already checked:" << filename;
      continue;
    }
    // TODO: consider to parse MACHO_OBJECT_FILE if needed.
    if (CheckFileType(dylib_name) != MACHO_FAT_FILE)
      continue;

    if (max_recursion > 0)
      TryParseMachONeeded(dylib_name, max_recursion - 1, input_files);
    else
      LOG(WARNING) << "Hit max dylib recursion depth: "
                   << " input_files=" << *input_files
                   << " filename=" << filename
                   << " kMaxRecursion=" << kMaxRecursion;
  }
}
#endif

}  // namespace devtools_goma
