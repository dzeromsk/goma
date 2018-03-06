# Goma

*Goma* is a distributed compiler service for open-source project such as
Chromium and Android. It's some kind of replacement of distcc+ccache.

NOTE: currently the goma backend is not available for non googlers.
We're working so that chromium developers can use it. Stay tuned.

# How goma works

Goma hooks a compile request, and sends it to a backend compile server.
If you have plenty of backend servers, a lot of compile can be processed in
parallel, for example, -j100, -j500 or -j1000.

Also, the goma backend caches the compile result. If the same compile request
comes, the cached result is returned from the goma cache server.

# How to build

goma client can be built on Linux, Mac, and Win.

## Install dependencies

1. Install [depot_tools](http://dev.chromium.org/developers/how-tos/install-depot-tools).
2. Install dependencies.

On debian or ubuntu,

```
$ sudo apt-get install libssl-dev libc6-dev-i386
```

On Mac, install Xcode.

On Windows, install Visual Studio 2017. Community edition is OK.


## Checkout source

```
$ gclient config https://chromium.googlesource.com/infra/goma/client
$ gclient sync
$ cd client
```


We assume goma is checked out to `$GOMA_SRC`.

## Build

```
$ cd "$GOMA_SRC/client"
$ gclient sync
$ gn gen --args='is_debug=false' out/Release
$ ninja -C out/Release
```

### Several important gn args

The build option can be modified with gn args.

```
is_debug=true/false
  Do debug build if true.
dcheck_always_on=true/false
  Enable DCHECK always (even in release build).
is_asan=true/false
  Use ASan build (with clang).
use_link_time_optimization=true/false
  Currently working only on Win. If true, /LTCG is enable.
use_lld=true/false
  Use lld for link (it will be fast)
```

## Run unittest

```
$ cd "$GOMA_SRC/client"
$ ./build/run_unittest.py --target=Release --build-dir=out
```

# How to use

## For Chromium/Android development

Goma can be integrated with Chromium/Android development easily.

1. Build goma client
2. Start compiler_proxy

```
$ "$GOMA_SRC/client/out/Release/goma_ctl.py" start
```

### For Chromium

In Chromium src, specify the following args in `gn args`

```
use_goma = true
goma_dir = "$GOMA_SRC/client/out/Release"  (Replace $GOMA_SRC to your checkout)
```

Then build like the following:

```
$ cd /path/to/chromium/src/out/Release
$ ninja -j100 chrome
```

### For Android

```
$ source build/envsetup.sh
$ lunch aosp_arm-eng
$ GOMA_DIR=$GOMA_SRC/client/out/Release USE_GOMA=true make -j4
```

Here, `-j4` is not related to goma parallelism. Android internally sets
`-j500` (or `-j` with `NINJA_REMOTE_NUM_JOBS` environment variable) for goma.

## For general development

1. Build goma client
2. Start `compiler_proxy`

```
$ ./goma_ctl.py ensure_start
```

3. Change your build script so that `gomacc` is prepended to compiler command.
   For example:

```
$ gomacc clang++ -c foo.cc
```

4. Build your product with `make -j100`, `ninja -j100` or larger -j.
   Check http://localhost:8080 to see compiler_proxy is actually working.


### Tips

* You can use [autoninja](https://chromium.googlesource.com/chromium/tools/depot_tools.git/+/master/autoninja) in depot_tools instead of specifying gomacc manually.



