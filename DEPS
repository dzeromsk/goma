vars = {
     "chromium_git": "https://chromium.googlesource.com",
}

deps = {
     # protobuf 3.3.0
     # Note: When you update protobuf, you will need to update
     # test/goma_data.pb.{h,cc}. Copying them from your output directory should
     # work.
     "client/third_party/protobuf/protobuf":
     "https://github.com/google/protobuf.git@a6189acd18b00611c1dc7042299ad75486f08a1a",

     # google-glog
     "client/third_party/glog":
     "https://github.com/google/glog.git@2063b387080c1e7adffd33ca07adff0eb346ff1a",

     # googletest 1.7.0
     "client/third_party/gtest":
     Var("chromium_git") + "/external/googletest.git@6215b1cab9c2cb93cc0110fd536af3be5ac18f93",

     # zlib 1.2.8
     "client/third_party/zlib":
     "https://goma.googlesource.com/zlib.git@50893291621658f355bc5b4d450a8d06a563053d",

     # xz v5.2.0
     "client/third_party/xz":
     "https://goma.googlesource.com/xz.git@fbafe6dd0892b04fdef601580f2c5b0e3745655b",

     # jsoncpp
     "client/third_party/jsoncpp/source":
     Var("chromium_git") + '/external/github.com/open-source-parsers/jsoncpp.git@f572e8e42e22cfcf5ab0aea26574f408943edfa4', # from svn 248

     # gyp
     # Note: this is used by build/vs_toolchain.py, and nobody else may
     # use this.
     "client/tools/gyp":
     Var("chromium_git") + "/external/gyp.git@" +
         "c6f471687407bf28ddfc63f1a8f47aeb7bf54edc",

     # chrome's tools/clang
     "client/tools/clang":
     "https://chromium.googlesource.com/chromium/src/tools/clang.git",

     # chrome's deps/third_party/boringssl
     "client/third_party/boringssl/src":
     "https://boringssl.googlesource.com/boringssl@f8058d41147543d6ad9a5ae5d70e7d19198bbe33",

     # google-breakpad
     "client/third_party/breakpad/breakpad":
     Var("chromium_git") + "/breakpad/breakpad.git@" +
         "70914b2d380d893364ad0110b8af18ba1ed5aaa3",

     # lss
     "client/third_party/lss":
     Var("chromium_git") + "/linux-syscall-support.git@" +
         "a91633d172407f6c83dd69af11510b37afebb7f9",

     # chrome's patched-yasm
     "client/third_party/yasm/source/patched-yasm":
     Var("chromium_git") + "/chromium/deps/yasm/patched-yasm.git@" +
         "b98114e18d8b9b84586b10d24353ab8616d4c5fc",

     # libc++ r256621
     "client/third_party/libc++/trunk":
     Var("chromium_git") + "/chromium/llvm-project/libcxx.git@" +
         "b1ece9c037d879843b0b0f5a2802e1e9d443b75a",

     # libc++abi r256623
     "client/third_party/libc++abi/trunk":
     Var("chromium_git") + "/chromium/llvm-project/libcxxabi.git@" +
         "0edb61e2e581758fc4cd4cd09fc588b3fc91a653",

     # libFuzzer
     "client/third_party/libFuzzer/src":
     Var("chromium_git") + "/chromium/llvm-project/llvm/lib/Fuzzer.git@" +
         "9aa0bddeb6820f6e5d897da410e1e8a3f7fd4b8e",

     # abseil
     "client/third_party/abseil/src":
     "https://github.com/abseil/abseil-cpp.git@055cc7dce10aa6bd7cc2ef64e0fe453fb792da62",

     # cctz (abseil needs this)
     "client/third_party/cctz/src":
     "https://github.com/google/cctz.git@e19879df3a14791b7d483c359c4acd6b2a1cd96b",
}

hooks = [
     {
       "name": "clang",
       "pattern": ".",
       "action": ["python", "client/tools/clang/scripts/update.py"],
     },

     # Pull binutils for linux, it is used for simpletry test.
     {
       "name": "binutils",
       "pattern": ".",
       "action": [
         "python",
         "client/test/third_party/binutils/download.py",
       ],
     },

     # Pull GN binaries.
     {
       "name": "gn_win",
       "pattern": ".",
       "action": [ "download_from_google_storage",
                   "--no_resume",
                   "--platform=win32",
                   "--no_auth",
                   "--bucket", "chromium-gn",
                   "-s", "client/buildtools/win/gn.exe.sha1",
       ],
     },
     {
       "name": "gn_mac",
       "pattern": ".",
       "action": [ "download_from_google_storage",
                   "--no_resume",
                   "--platform=darwin",
                   "--no_auth",
                   "--bucket", "chromium-gn",
                   "-s", "client/buildtools/mac/gn.sha1",
       ],
     },
     {
       "name": "gn_linux64",
       "pattern": ".",
       "action": [ "download_from_google_storage",
                   "--no_resume",
                   "--platform=linux*",
                   "--no_auth",
                   "--bucket", "chromium-gn",
                   "-s", "client/buildtools/linux64/gn.sha1",
       ],
     },
     # Update the Windows toolchain if necessary.
     {
       'name': 'win_toolchain',
       'pattern': '.',
       'action': ['python', 'client/build/vs_toolchain.py', 'update'],
     },
]
