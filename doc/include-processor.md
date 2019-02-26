# How C/C++ IncludeProcessor works

This document describes the current status of
[C/C++ IncludeProcessor](https://chromium.googlesource.com/infra/goma/client/+/01dbe2875fb5aaa3a3bd125b40afca28ce2faa26/client/cxx/include_processor).

This document is based on
[the goma client revision on Feb 2019](https://chromium.googlesource.com/infra/goma/client/+/01dbe2875fb5aaa3a3bd125b40afca28ce2faa26
).

## Purpose of C/C++ IncludeProcessor

The purpose of C/C++ IncludeProcessor is to locate and list all necessary
included files. For example, if `#include <foo.h>` would be found by the
toolchain's real preprocessor in "some/include/dir/", then
"some/include/dir/foo.h" should appear in the list.

```c++
#include <foo.h>
```

However, there are a few conditionally included files. For example:

```c++
#if FOO() && BAR()
#include <baz.h>
#endif
```

In this case, only when `FOO() && BAR()` is true, `baz.h` is included.
So, C/C++ IncludeProcessor needs to evaluate preprocessor directives.

In the rest of this document, we describe how this evalution works.

## Convert a file content to a list of CppDirective

Assume C/C++ IncludeProcessor wants to list included files for a file "a.cc".

First, we convert "a.cc" content to `IncludeItem`.
`IncludeItem` contains `SharedCppDirectives`, and detected include guard
(if any). `SharedCppDirectives` is conceptually a list of `CppDirective`.
One `CppDirective` corresponds to one cpp directive e.g. `#include <iostream>`.

Definition is the following:

```c++
using CppDirectiveList = std::vector<std::unique_ptr<const CppDirective>>;
using SharedCppDirectives = std::shared_ptr<const CppDirectiveList>;
```

See: [cpp_directive.h](https://chromium.googlesource.com/infra/goma/client/+/01dbe2875fb5aaa3a3bd125b40afca28ce2faa26/client/cxx/include_processor/cpp_directive.h)

Process flow is like the following: See IncludeCache::CreateFromFile for more
details.

```
Input File
  v
  v  DirectiveFilter: Keep only # lines to make parser faster.
  v
Input File (filtered)
  v
  v  CppDirectiveParser: parse # lines and convert them to a list of
  v  CppDirective.
  v
SharedCppDirectives
  v
  v  CppDirectiveOptimizer: remove unnecessary directives,
  v  which won't affect include processor result.
  v
SharedCppDirectives
  v
  v  IncludeGuardDetector: detect include guard to use in CppParser.
  v
IncludeItem
```

The result is cached in
[IncludeCache](https://chromium.googlesource.com/infra/goma/client/+/01dbe2875fb5aaa3a3bd125b40afca28ce2faa26/client/cxx/include_processor/include_cache.h),
and we reuse the conversion result to process the same file.

The cache size is limited by the max number of entries.
After processing all chrome sources, 200~300 MB
will be used in IncludeCache.

## Evaluate a list of CppDirective

After a file can be converted to a list of CppDirectives,
[`CppParser`](https://chromium.googlesource.com/infra/goma/client/+/01dbe2875fb5aaa3a3bd125b40afca28ce2faa26/client/cxx/include_processor/cpp_parser.h#)
evaluates the list of `CppDirectives`.

Evaluation is just processing CppDirectives one by one.
See [`CppParser::ProcessDirectives`](https://chromium.googlesource.com/infra/goma/client/+/01dbe2875fb5aaa3a3bd125b40afca28ce2faa26/client/cxx/include_processor/cpp_parser.cc#114),
to understand how evalution works.

During evaluation, CppParser keeps a hashmap from macro name (string) to Macro.
For example, `#define A FOO BAR` is processed,
`CppParser` has a hashmap entry like
`"A" --> Macro(tokens=["FOO", "BAR"])`.

Note that we pass directives not only from a file input, but also from
a compiler predefined macros (e.g. `__cplusplus`) and
macros defined in a command line flag (e.g. `-DFOO=BAR`).
We need to pass these predefined macros and command line defined macros to
CppParser before evaluating CppDirective from a file input.

### Memory usage

On Linux, the mean size of the hashmap is around 4000 entries.
On Windows, since windows.h is large, it sometimes exceeds 15000 entries.

If the mean memory size of macro entry is just 1KB, macro environment will use
1 \[KB\] * 15,000 \[bytes/entries\] = 15 MB (+ hashmap overhead).
IncludeProcessor works in parallel (usually the number of CPU cores tasks).
If you're using 32 cores machine, 32 * 15 = 480 MB will be used.
Note that this is rough estimation.

## How to expand macro

[`CppParser::ProcessDirectives`](https://chromium.googlesource.com/infra/goma/client/+/01dbe2875fb5aaa3a3bd125b40afca28ce2faa26/client/cxx/include_processor/cpp_parser.cc#114)
just evaluates each directives, so it's easy.
However, one difficult point is how to expand macro.

Macro expansion uses two different expanders:
[CppMacroExpanderCBV](https://chromium.googlesource.com/infra/goma/client/+/01dbe2875fb5aaa3a3bd125b40afca28ce2faa26/client/cxx/include_processor/cpp_macro_expander_cbv.h)
and
[CppMacroExpanderNaive](https://chromium.googlesource.com/infra/goma/client/+/01dbe2875fb5aaa3a3bd125b40afca28ce2faa26/client/cxx/include_processor/cpp_macro_expander_naive.h).

See comment about how they work. Especially, CBV version has several examples.
Naive version is based on https://www.spinellis.gr/blog/20060626/cpp.algo.pdf.
