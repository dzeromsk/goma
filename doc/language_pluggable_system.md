# Language pluggable system: How to add a new type of compiler in goma

[TOC]

## Abstract

This document describes how to add a new compiler type (e.g. rustc, swift) to
Goma.

You have to define 4 components to add a new type of compiler,

* `CompilerFlags`
* `CompilerInfo`
* `IncludeProcessor`
* `ExecReqNormalizer`

and 1 class to collect compiler type specific code into one place.

* `CompilerTypeSpecific`

As an example, we have defined `Fake` type. It pretends to be a compiler,
so we can use it as a test.

Note: since Goma has evolved with C/C++, we still have C/C++ specific code here
and there, even though we have refactored our code a lot.
However, code for new compiler types should be separated from the Goma core
code.

## First of all… Understand rough prodecure of Goma

Understand the rough procedure of Goma, and what kind of components are
necessary to implement a new compiler type.

Here, we consider the following command line as an example.

```sh
$ gomacc clang++ -Ibar -Ibaz -std=c++11 -c foo.cc -o foo.o
```

This command line is passed to `compiler_proxy` (via `ExecReq` proto)
from `gomacc`. The first argument `gomacc` is dropped
when `compiler_proxy` receives the command line.

In `compiler_proxy`, the following processed will be performed.

1. *`CompilerFlags`*, which is a parsed result of command line, is made from
   the command line.
    * It contains compiler flags, input/output files seen from the command line.
    * In some languages (e.g. javac), Output filenames cannot be determined from
      command line (in java case, class name is used for an output filename). In
      that case, output directories can be specified instead of output files.
      Either of output directories or output filenames must be set to
      `CompilerFlags`.
    * From the example, compiler flags are `-Ibar`, `-Ibaz`, `-std=c++11`, `-c`,
      and `-o foo.o`. Input file is `foo.cc`, and output file is `foo.o`.
      Output directory is not specified in this example.
2. Collect compiler information into *`CompilerInfo`*.
    * *`CompilerInfoBuilder`* collects SHA256 digest of compiler,
      compiler version (e.g. `clang 7.0`), compile target
      (e.g. `x86_64-unknown-linux`), etc.
      For C/C++, predefined macros are also collected.
    * This process can be slow, so we use cache. The cache key is created from
      `compiler_info_flags`, `cwd`, and `local_compiler_path`.
      `compiler_info_flags` is the flags (used in the command line) which
      affects CompilerInfo. The detail is described in CompilerFlags section.
3. Run *`IncludeProcessor`*, which lists input files that will be used for each
   compile request.
    * In C/C++, we have our own C preprocessor to list all header files.
    * This process must be fast, since this runs each compile unit.
    * The listed input files are added into `ExecReq`.
4. `compiler_proxy` sends a compile request to Goma server
   (via `ExecReq` proto),
   and receives the compile result (via `ExecResp` proto).
   If `LocalOutputCache` is enabled, `compiler_proxy` caches `ExecResp` result
   in local storage. The key of `LocalOutputCache` is `ExecReq` normalized with
   *`ExecReqNormalizer`*.

Actually there are a few more steps (e.g. to modify command line, to modify
upload files, and to modify compile results) to absorb client/server environment
difference. If you have to modify them, see
[client/compile_task.cc](../client/compile_task.cc).

Note that we use *`CompilerTypeSpecific`* class to call several compiler type
specific procedures.

## CompilerFlags

*`CompilerFlags`* is the parsed result of a command line. It receives a compiler
command (e.g. `clang++ -Ibar -Ibaz -std=c++11 -c foo.cc -o foo.o`),
and parses it.

`CompilerFlags` collects input/output files (or sometimes output directories),
compiler flags, and `compiler_info_flags` from the command line.

`compiler_info_flags` is a set of compiler_flags that can affect `CompilerInfo`.
All compiler flags that can affect CompilerInfo must be stored in
`compiler_info_flags`. In C/C++, `CompilerInfo` contains predefined macros
e.g. `__cplusplus`. Since `-std=c++11` affects the value of the predefined
macros, `-std=c++11` must be listed as `compiler_info_flags`.
See CompilerInfo section to understand what are defined in CompilerInfo.

### How to define CompilerFlags

1. Add a new compiler flag type in `CompilerFlagType`
   ([lib/compiler_flag_type.h](../lib/compiler_flag_type.h)).
2. Define `<Language>Flags` which is derived from `CompilerFlags` in
  `lib/<language>_flags.h`
    1. In ctor, parse command line.
       * List input files and output files which can be determined from command
         line.
       * Set `lang_`. C/C++ compiler often provides a language switch
         (e.g. `-x`). To make the rest of procedures easier, we have `lang_`.
         If the compiler accepts only one language, just set it.
         For example, `lang_` would be `c`, `c++`, or `java`.
       * List `compiler_info_flags`.
       * If everything is OK, set `true` to `is_successful_`.
    2. Define `IsServerImportantEnv()` if a compiler requires to use envvars.
       Only envvars where `IsServerImportantEnv` returns true are passed to
       Goma server.
    3. For later use, implement `Is<Language>Command`, and `GetCompilerName`
       (see `FakeFlags` for example).

example

* [lib/fake_flags.cc](../lib/fake_flags.cc)
* [lib/fake_flags.h](../lib/fake_flags.h)

## CompilerInfo

*`CompilerInfo`* contains the information how the compiler is configured.
The builder class `CompilerInfoBuilder` is separated from `CompilerInfo`,

`CompilerInfo` has the following information. This is not the complete list.
See [client/compiler_info.h](../client/compiler_info.h) for more information.
* SHA256 digest of compiler, compiler version, compiler target.
  These are sent to Goma server, and Goma server will use them to determine
  compiler to use.
* `subprograms`. They are programs that the compiler might use during a compile.
  For example, `objcopy`. It's used to identify which subprogram is used.
* `additional_flags`. Command line flags automatically added for remote compile.
  They are used to absorb client/server environment difference.
* `resource`: Files, which are not defined in user's command line and not listed
  with include processor, but necessary for remote compile. Some compiler
  implicitly uses a resource file (e.g. if `-fsanitize=address` is passed to
  `clang`, `asan_blacklist.txt` is implicitly used.)

For C/C++, `CompilerInfo` also has system include directories (e.g. `-isystem`),
and compiler predefined macros (e.g. `__linux__`).
See [client/cxx/cxx_compiler_info.h](../client/cxx/cxx_compiler_info.h) for
more information.

### How to define CompilerInfo

1. Define a new compiler type in `CompilerInfoType`
   ([client/compiler_info.h](../client/compiler_info.h)).
2. Define a new compiler info data extension in
   [client/compiler_info_data.proto](../client_compiler_info_data.proto).
    1. The existence of data extension is a key to determine `CompilerInfoType`.
       Even if no extension data is required, we have to define an empty type
       here.
3. Define `<Language>CompilerInfo` in `client/<language>/<language>_compiler_info.h`
    1. The only virtual method is to return `CompilerInfoType`, so this is easy.
4. Define `<Language>CompilerInfoBuilder` in `client/<language>/<language>_compiler_info_buidler.h`
    1. Implement `SetTypeSpecificCompilerInfo()`. In this method,
        1. extend `CompilerInfoData` with language extension type
        2. calculate compiler version and target.
    2. Non type specific code (e.g. to calculate the SHA256 digest of compiler)
       is already implemented in the base class (`CompilerInfoBuilder`).

example

* [client/fake/fake_compiler_info.cc](../client/fake/fake_compiler_info.cc)
* [client/fake/fake_compiler_info.h](../client/fake/fake_compiler_info.h)
* [client/fake/fake_compiler_info_builder.cc](../client/fake/fake_compiler_info_builder.cc)
* [client/fake/fake_compiler_info_builder.h](../client/fake/fake_compiler_info_builder.h)

## IncludeProcessor

`IncludeProcessor` lists input files used in a compile.
`IncludeProcessor` receives `CompilerInfo`, `CompilerFlags`, (`CommandSpec`,)
and returns the file list which will be used during the compile.

For C/C++, we have our own C preprocessor to list all header files
used for a compile.
For java, it checks `.jar` files to list dependent .jar files.

### How to define IncludeProcessor

1. Define `<Language>IncludeProcessor`
   in `client/<language>/<languagle>_include_processor.h`
2. In `<Language>CompilerTypeSpecific` class (which is described later),
   implement `RunIncludeProcessor`.

example

* [client/fake/fake_include_processor.cc](../client/fake/fake_include_processor.cc)
* [client/fake/fake_include_processor.h](../client/fake/fake_include_processor.h)

## ExecReqNormalizer

*`ExecReqNormalizer`* normalizes `ExecReq` for `LocalOutputCache`.
`LocalOutputCache` can store the compile result in local storage (like ccache).
The key of the `LocalOutputCache` is the SHA256 digest of the normalized
`ExecReq`.

If it is sure some fields of `ExecReq` does not affect compile result, they
can be normalized with `ExecReqNormalizer`.

For example, when the compiler is `clang` and the command line contains
`-g0` (= omit debug information), then `cwd` can be removed from
the normalized `ExecReq`.

If you not sure, just keep `ExecReq` as is.

### How to define ExecReqNormalizer

1. Define `<Language>ExecReqNormalizer` in `lib/<language>_execreq_normalizer.h`
    * Usually it should be derived from `ConfigurableExecReqNormalizer`.
      If not sure, just return `Config::AsIs()` from `Configure()` method.
      This doesn't normalize anything, however, this should be ok for the first
      step.

example

* [lib/fake_execreq_normalizer.cc](../lib/fake_execreq_normalizer.cc)
* [lib/fake_execreq_normalizer.h](../lib/fake_execreq_normalizer.h)

## CompilerTypeSpecific

`CompilerTypeSpecific` is a collection of methods to call compiler type specific
processes in `compiler_proxy`.

### How to define CompilerTypeSpecific

1. Define `<Language>CompilerTypeSpecific` in
   `client/<language>/<language>_compiler_type_specific.h`
    * `<Language>CompilerTypeSpecific` should be derived
      from `CompilerTypeSpecific`.
2. Add your class into `CompilerTypeSpecificCollection` in
   `client/compiler_type_specific_collection.h`

example

* [client/fake/fake_compiler_type_specific.cc](../client/fake/fake_compiler_type_specific.cc)
* [client/fake/fake_compiler_type_specific.h](../client/fake/fake_compiler_type_specific.h)

## How to test client

You must want to check these components are working correctly.
The one way to check is to run the new type of compiler with Goma.

1. Start `compiler_proxy` (`goma_ctl.py ensure_start`)
2. Run your compiler with adding `GOMA_USE_LOCAL=false`
    * `GOMA_USE_LOCAL=false gomacc <compiler_path> …`
    * This enforces a compile request is sent to Goma server (it means
      we can enforce `IncludeProcessor` runs).

Then, open `compiler_proxy` dashboard
[http://localhost:8088/](http://localhost:8088/). A new task must
be shown in the dashboard. Probably, your task is shown as `GOMA ERROR`
(red color) if your compiler is not registered in Goma server.
`GOMA ERROR` means the compile failed in the remote, but succeeded locally.
If it's shown as `FAILURE` (pink color), your compile failed locally, too.
This indicates something is wrong, but we have to see `compiler_proxy` log
[http://localhost:8088/logz?INFO](http://localhost:8088/logz?INFO)
to investigate what's really wrong.

[http://localhost:8088/compilerinfoz](http://localhost:8088/compilerinfoz) shows
the list of `CompilerInfo`. Find `[compiler info]` section,
and find your compiler from the list.

Open your compile task in the dashbaord
[http://localhost:8088/](http://localhost:8088/). `inputs` shows the list of
files which is sent to Goma server. If your `IncludeProcessor` didn't work
correctly, some files might be missing.

For `CompilerFlag`, currently we don't have a good interface to see the details
in `compiler_proxy` dashboard. We recommend you write a unittest.

