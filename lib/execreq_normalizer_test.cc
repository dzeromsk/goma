// Copyright 2016 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.



#include "execreq_normalizer.h"

#include "absl/strings/match.h"
#include "compiler_flags.h"
#include "execreq_verifier.h"
#include "google/protobuf/text_format.h"
#include "google/protobuf/util/message_differencer.h"
#include "gtest/gtest.h"
#include "path.h"
using google::protobuf::TextFormat;
using google::protobuf::util::MessageDifferencer;

namespace {

const char kExecReqToNormalize[] = "command_spec {\n"
    "  name: \"clang\"\n"
    "  version: \"4.2.1[clang version 3.5.0 (trunk 214024)]\"\n"
    "  target: \"x86_64-unknown-linux-gnu\"\n"
    "  system_include_path: \"/tmp/src/third_party/include\"\n"
    "  cxx_system_include_path: \"/tmp/src/third_party/include\"\n"
    "}\n"
    "arg: \"clang\"\n"
    "arg: \"-I\"\n"
    "arg: \"/tmp/src/third_party/include\"\n"
    "arg: \"-Xclang\"\n"
    "arg: \"/tmp/src/third_party/lib/libFindBadConstructs.so\"\n"
    "arg: \"-gcc-toolchain=/tmp/src/third_party/target_toolchain\"\n"
    "arg: \"-B/tmp/src/out/Release/bin\"\n"
    "arg: \"--sysroot=/tmp/src/build/linux/sysroot\"\n"
    "arg: \"-resource-dir=/tmp/src/third_party/clang\"\n"
    "arg: \"-c\"\n"
    "arg: \"hello.c\"\n"
    "cwd: \"/tmp/src/out/Release\"\n"
    "env: \"PWD=/tmp/src/out/Release\"\n"
    "Input {\n"
    "  filename: \"/tmp/src/hello.c\"\n"
    "  hash_key: \"152d72ea117deff2af0cf0ca3aaa46a20a5f0c0e4ccb8b6d"
        "559d507401ae81e9\"\n"
    "}\n";
const int kExecReqToNormalizeArgSize = 11;

const char kExecReqToNormalizeGcc[] = "command_spec {\n"
    "  name: \"gcc\"\n"
    "  version: \"4.8[(Ubuntu 4.8.4-2ubuntu1~14.04) 4.8.4]\"\n"
    "  target: \"x86_64-linux-gnu\"\n"
    "  system_include_path: \"/tmp/src/third_party/include\"\n"
    "  cxx_system_include_path: \"/tmp/src/third_party/include\"\n"
    "}\n"
    "arg: \"gcc\"\n"
    "arg: \"-I\"\n"
    "arg: \"/tmp/src/third_party/include\"\n"
    "arg: \"-gcc-toolchain=/tmp/src/third_party/target_toolchain\"\n"
    "arg: \"-B/tmp/src/out/Release/bin\"\n"
    "arg: \"--sysroot=/tmp/src/build/linux/sysroot\"\n"
    "arg: \"-c\"\n"
    "arg: \"hello.c\"\n"
    "cwd: \"/tmp/src/out/Release\"\n"
    "env: \"PWD=/tmp/src/out/Release\"\n"
    "Input {\n"
    "  filename: \"/tmp/src/hello.c\"\n"
    "  hash_key: \"152d72ea117deff2af0cf0ca3aaa46a20a5f0c0e4ccb8b6d"
        "559d507401ae81e9\"\n"
    "}\n";
const int kExecReqToNormalizeGccArgSize = 8;

const char kExecReqToNormalizeRelativeArgs[] = "command_spec {\n"
    "  name: \"clang\"\n"
    "  version: \"4.2.1[clang version 3.5.0 (trunk 214024)]\"\n"
    "  target: \"x86_64-unknown-linux-gnu\"\n"
    "  system_include_path: \"/tmp/src/third_party/include\"\n"
    "  cxx_system_include_path: \"/tmp/src/third_party/include\"\n"
    "}\n"
    "arg: \"clang\"\n"
    "arg: \"-I\"\n"
    "arg: \"../../third_party/include\"\n"
    "arg: \"-Xclang\"\n"
    "arg: \"../../third_party/lib/libFindBadConstructs.so\"\n"
    "arg: \"-gcc-toolchain=../third_party/target_toolchain\"\n"
    "arg: \"-B./bin\"\n"
    "arg: \"--sysroot=../../build/linux/sysroot\"\n"
    "arg: \"-resource-dir=../../third_party/clang\"\n"
    "arg: \"-c\"\n"
    "arg: \"hello.c\"\n"
    "cwd: \"/tmp/src/out/Release\"\n"
    "env: \"PWD=/tmp/src/out/Release\"\n"
    "Input {\n"
    "  filename: \"/tmp/hello.c\"\n"
    "  hash_key: \"152d72ea117deff2af0cf0ca3aaa46a20a5f0c0e4ccb8b6d"
        "559d507401ae81e9\"\n"
    "}\n";

const char kExecReqToNormalizeLink[] = "command_spec {\n"
    "  name: \"gcc\"\n"
    "  version: \"4.4.3[Ubuntu 4.4.3-4ubuntu5]\"\n"
    "  target: \"x86_64-linux-gnu\"\n"
    "  system_include_path: \"/tmp/src/third_party/include\"\n"
    "  cxx_system_include_path: \"/tmp/src/third_party/include\"\n"
    "}\n"
    "arg: \"gcc\"\n"
    "arg: \"-I\"\n"
    "arg: \"/tmp/src/third_party/include\"\n"
    "arg: \"-L\"\n"
    "arg: \"/tmp/src/third_party/lib\"\n"
    "arg: \"-Xclang\"\n"
    "arg: \"/tmp/src/third_party/lib/libFindBadConstructs.so\"\n"
    "arg: \"-B/tmp/src/out/Release/bin\"\n"
    "arg: \"--sysroot=/tmp/src/build/linux/sysroot\"\n"
    "arg: \"-resource-dir=/tmp/src/third_party/clang\"\n"
    "arg: \"hello.o\"\n"
    "cwd: \"/tmp/src/out/Release\"\n"
    "env: \"PWD=/tmp/src/out/Release\"\n"
    "Input {\n"
    "  filename: \"/tmp/hello.o\"\n"
    "  hash_key: \"152d72ea117deff2af0cf0ca3aaa46a20a5f0c0e4ccb8b6d"
        "559d507401ae81e9\"\n"
    "}\n";

const char kExecReqToNormalizeWinAlice[] = R"(command_spec {
  name: "cl.exe"
  version: "19.00.24215.1"
  target: "x64"
  binary_hash: "7928c17d5185cf7dca794e9970e2463985315adf832f3fde7becfc71673d2fd3"
  local_compiler_path: "c:\\src\\chromium\\src\\third_party\\depot_tools\\win_toolchain\\vs_files\\f53e4598951162bad6330f7a167486c7ae5db1e5\\vc\\bin\\amd64\\cl.exe"
  cxx_system_include_path: "c:\\src\\chromium\\src\\third_party\\depot_tools\\win_toolchain\\vs_files\\f53e4598951162bad6330f7a167486c7ae5db1e5\\win_sdk\\bin\\..\\..\\win_sdk\\include\\10.0.15063.0\\um"
  cxx_system_include_path: "c:\\src\\chromium\\src\\third_party\\depot_tools\\win_toolchain\\vs_files\\f53e4598951162bad6330f7a167486c7ae5db1e5\\win_sdk\\bin\\..\\..\\win_sdk\\include\\10.0.15063.0\\shared"
  cxx_system_include_path: "c:\\src\\chromium\\src\\third_party\\depot_tools\\win_toolchain\\vs_files\\f53e4598951162bad6330f7a167486c7ae5db1e5\\win_sdk\\bin\\..\\..\\win_sdk\\include\\10.0.15063.0\\winrt"
  cxx_system_include_path: "c:\\src\\chromium\\src\\third_party\\depot_tools\\win_toolchain\\vs_files\\f53e4598951162bad6330f7a167486c7ae5db1e5\\win_sdk\\bin\\..\\..\\win_sdk\\include\\10.0.15063.0\\ucrt"
  cxx_system_include_path: "c:\\src\\chromium\\src\\third_party\\depot_tools\\win_toolchain\\vs_files\\f53e4598951162bad6330f7a167486c7ae5db1e5\\win_sdk\\bin\\..\\..\\vc\\include"
  cxx_system_include_path: "c:\\src\\chromium\\src\\third_party\\depot_tools\\win_toolchain\\vs_files\\f53e4598951162bad6330f7a167486c7ae5db1e5\\win_sdk\\bin\\..\\..\\vc\\atlmfc\\include"
}
arg: "c:\\src\\chromium\\src\\third_party\\depot_tools\\win_toolchain\\vs_files\\f53e4598951162bad6330f7a167486c7ae5db1e5\\vc\\bin\\amd64/cl.exe"
arg: "/nologo"
arg: "/showIncludes"
arg: "/FC"
arg: "@obj/chrome/browser/ui/ui_3/session_crashed_bubble_view.obj.rsp"
arg: "/c"
arg: "../../chrome/browser/ui/views/session_crashed_bubble_view.cc"
arg: "/Foobj/chrome/browser/ui/ui_3/session_crashed_bubble_view.obj"
arg: "/Fdobj/chrome/browser/ui/ui_3_cc.pdb"
cwd: "C:\\src\\chromium\\src\\out\\Alice"
Input {
  filename: "..\\..\\third_party\\boringssl\\src\\include\\openssl\\base.h"
  hash_key: "12812aef7084e6a0764657261ad92b9ef93a5e20aea26675324ad4b3c761a863"
}
Input {
  filename: "c:\\src\\chromium\\src\\third_party\\depot_tools\\win_toolchain\\vs_files\\f53e4598951162bad6330f7a167486c7ae5db1e5\\win_sdk\\bin\\..\\..\\vc\\include\\algorithm"
  hash_key: "40908bd6f47550869ac26ac15b189d72f67187123066d8dfd2cf4435e0d53fc6"
}
expanded_arg: "c:\\src\\chromium\\src\\third_party\\depot_tools\\win_toolchain\\vs_files\\f53e4598951162bad6330f7a167486c7ae5db1e5\\vc\\bin\\amd64/cl.exe"
expanded_arg: "/nologo"
expanded_arg: "/showIncludes"
expanded_arg: "/FC"
expanded_arg: "-DI18N_PHONENUMBERS_NO_THREAD_SAFETY=1"
expanded_arg: "-Igen"
expanded_arg: "-I../../third_party/libaddressinput/src/cpp/include"
expanded_arg: "/D__DATE__="
expanded_arg: "/D__TIME__="
expanded_arg: "/D__TIMESTAMP__="
expanded_arg: "/Gy"
expanded_arg: "/FS"
expanded_arg: "/bigobj"
expanded_arg: "/d2FastFail"
expanded_arg: "/Zc:sizedDealloc-"
expanded_arg: "/W4"
expanded_arg: "/WX"
expanded_arg: "/utf-8"
expanded_arg: "/O1"
expanded_arg: "/Ob2"
expanded_arg: "/Oy-"
expanded_arg: "/d2Zi+"
expanded_arg: "/Zc:inline"
expanded_arg: "/Gw"
expanded_arg: "/Oi"
expanded_arg: "/MD"
expanded_arg: "/wd4267"
expanded_arg: "/TP"
expanded_arg: "/wd4577"
expanded_arg: "/GR-"
expanded_arg: "/c"
expanded_arg: "../../chrome/browser/ui/views/session_crashed_bubble_view.cc"
expanded_arg: "/Foobj/chrome/browser/ui/ui_3/session_crashed_bubble_view.obj"
expanded_arg: "/Fdobj/chrome/browser/ui/ui_3_cc.pdb"
)";

const char kExecReqToNormalizeWinBob[] = R"(command_spec {
  name: "cl.exe"
  version: "19.00.24215.1"
  target: "x64"
  binary_hash: "7928c17d5185cf7dca794e9970e2463985315adf832f3fde7becfc71673d2fd3"
  local_compiler_path: "c:\\src\\chromium\\src\\third_party\\depot_tools\\win_toolchain\\vs_files\\f53e4598951162bad6330f7a167486c7ae5db1e5\\vc\\bin\\amd64\\cl.exe"
  cxx_system_include_path: "c:\\src\\chromium\\src\\third_party\\depot_tools\\win_toolchain\\vs_files\\f53e4598951162bad6330f7a167486c7ae5db1e5\\win_sdk\\bin\\..\\..\\win_sdk\\include\\10.0.15063.0\\um"
  cxx_system_include_path: "c:\\src\\chromium\\src\\third_party\\depot_tools\\win_toolchain\\vs_files\\f53e4598951162bad6330f7a167486c7ae5db1e5\\win_sdk\\bin\\..\\..\\win_sdk\\include\\10.0.15063.0\\shared"
  cxx_system_include_path: "c:\\src\\chromium\\src\\third_party\\depot_tools\\win_toolchain\\vs_files\\f53e4598951162bad6330f7a167486c7ae5db1e5\\win_sdk\\bin\\..\\..\\win_sdk\\include\\10.0.15063.0\\winrt"
  cxx_system_include_path: "c:\\src\\chromium\\src\\third_party\\depot_tools\\win_toolchain\\vs_files\\f53e4598951162bad6330f7a167486c7ae5db1e5\\win_sdk\\bin\\..\\..\\win_sdk\\include\\10.0.15063.0\\ucrt"
  cxx_system_include_path: "c:\\src\\chromium\\src\\third_party\\depot_tools\\win_toolchain\\vs_files\\f53e4598951162bad6330f7a167486c7ae5db1e5\\win_sdk\\bin\\..\\..\\vc\\include"
  cxx_system_include_path: "c:\\src\\chromium\\src\\third_party\\depot_tools\\win_toolchain\\vs_files\\f53e4598951162bad6330f7a167486c7ae5db1e5\\win_sdk\\bin\\..\\..\\vc\\atlmfc\\include"
}
arg: "c:\\src\\chromium\\src\\third_party\\depot_tools\\win_toolchain\\vs_files\\f53e4598951162bad6330f7a167486c7ae5db1e5\\vc\\bin\\amd64/cl.exe"
arg: "/nologo"
arg: "/showIncludes"
arg: "/FC"
arg: "@obj/chrome/browser/ui/ui_3/session_crashed_bubble_view.obj.rsp"
arg: "/c"
arg: "../../chrome/browser/ui/views/session_crashed_bubble_view.cc"
arg: "/Foobj/chrome/browser/ui/ui_3/session_crashed_bubble_view.obj"
arg: "/Fdobj/chrome/browser/ui/ui_3_cc.pdb"
cwd: "C:\\src\\chromium\\src\\out\\Bob"
Input {
  filename: "..\\..\\third_party\\boringssl\\src\\include\\openssl\\base.h"
  hash_key: "12812aef7084e6a0764657261ad92b9ef93a5e20aea26675324ad4b3c761a863"
}
Input {
  filename: "c:\\src\\chromium\\src\\third_party\\depot_tools\\win_toolchain\\vs_files\\f53e4598951162bad6330f7a167486c7ae5db1e5\\win_sdk\\bin\\..\\..\\vc\\include\\algorithm"
  hash_key: "40908bd6f47550869ac26ac15b189d72f67187123066d8dfd2cf4435e0d53fc6"
}
expanded_arg: "c:\\src\\chromium\\src\\third_party\\depot_tools\\win_toolchain\\vs_files\\f53e4598951162bad6330f7a167486c7ae5db1e5\\vc\\bin\\amd64/cl.exe"
expanded_arg: "/nologo"
expanded_arg: "/showIncludes"
expanded_arg: "/FC"
expanded_arg: "-DI18N_PHONENUMBERS_NO_THREAD_SAFETY=1"
expanded_arg: "-Igen"
expanded_arg: "-I../../third_party/libaddressinput/src/cpp/include"
expanded_arg: "/D__DATE__="
expanded_arg: "/D__TIME__="
expanded_arg: "/D__TIMESTAMP__="
expanded_arg: "/Gy"
expanded_arg: "/FS"
expanded_arg: "/bigobj"
expanded_arg: "/d2FastFail"
expanded_arg: "/Zc:sizedDealloc-"
expanded_arg: "/W4"
expanded_arg: "/WX"
expanded_arg: "/utf-8"
expanded_arg: "/O1"
expanded_arg: "/Ob2"
expanded_arg: "/Oy-"
expanded_arg: "/d2Zi+"
expanded_arg: "/Zc:inline"
expanded_arg: "/Gw"
expanded_arg: "/Oi"
expanded_arg: "/MD"
expanded_arg: "/wd4267"
expanded_arg: "/TP"
expanded_arg: "/wd4577"
expanded_arg: "/GR-"
expanded_arg: "/c"
expanded_arg: "../../chrome/browser/ui/views/session_crashed_bubble_view.cc"
expanded_arg: "/Foobj/chrome/browser/ui/ui_3/session_crashed_bubble_view.obj"
expanded_arg: "/Fdobj/chrome/browser/ui/ui_3_cc.pdb"
)";

const char kExecReqToNormalizeWin[] = "command_spec {\n"
    "  name: \"cl.exe\"\n"
    "  version: \"15.00.30729.01\"\n"
    "  target: \"80x86\"\n"
    "  local_compiler_path: \"c:\\\\Program Files (x86)"
        "\\\\Microsoft Visual Studio 9.0\\\\VC\\\\BIN\\\\cl.exe\"\n"
    "  system_include_path: \"c:\\\\Program Files (x86)"
        "\\\\Microsoft Visual Studio 9.0\\\\VC\\\\INCLUDE\"\n"
    "  cxx_system_include_path: \"c:\\\\Program Files (x86)"
        "\\\\Microsoft Visual Studio 9.0\\\\VC\\\\INCLUDE\"\n"
    "}\n"
    "arg: \"cl\"\n"
    "arg: \"/TP\"\n"
    "arg: \"/showIncludes\"\n"
    "arg: \"/Z7\"\n"
    "arg: \"/FoC:\\\\src\\\\goma\\\\client\\\\build\\\\Debug"
        "\\\\vc\\\\stdafx.obj\"\n"
    "arg: \"stdafx.cpp\"\n"
    "cwd: \"C:\\\\src\\\\goma\\\\client\\\\test\\\\vc\"\n"
    "Input {\n"
    "  filename: \"C:\\\\src\\\\goma\\\\client\\\\test\\\\vc\\\\stdafx.cpp\"\n"
    "  hash_key: \"152d72ea117deff2af0cf0ca3aaa46a20a5f0c0e4ccb8b6d"
        "559d507401ae81e9\"\n"
    "}\n";

const char kExecReqToNormalizeWinPNaCl[] =
    "command_spec <\n"
    "  name: \"clang++\"\n"
    "  version: \"4.2.1[clang version 3.7.0 (https://chromium.googlesource.com"
        "/a/native_client/pnacl-clang.git "
        "ce163fdd0f16b4481e5cf77a16d45e9b4dc8300e"
        ") (https://chromium.googlesource.com/a/native_client/pnacl-llvm.git "
        "83991f993fea6cd9c515df12c3270ab9c0746215)]\"\n"
    "  target: \"x86_64--nacl\"\n"
    "  binary_hash: \"b15df3ea17efb0f8e7a617dd5727aec329eae89a5c8d42dedc9602f9"
        "ae433c42\"\n"
    "  local_compiler_path: \"C:\\\\Users\\\\dummy\\\\pnacl_newlib\\\\bin"
        "\\\\x86_64-nacl-clang++.exe\"\n"
    "  cxx_system_include_path: \"C:\\\\Users\\\\dummy\\\\pnacl_newlib\\\\"
    "bin/../x86_64-nacl/include/c++/v1\"\n"
    "  cxx_system_include_path: \"C:\\\\Users\\\\dummy\\\\pnacl_newlib\\\\"
        "bin\\\\..\\\\lib\\\\clang\\\\3.7.0\\\\include\"\n"
    "  cxx_system_include_path: \"C:\\\\Users\\\\dummy\\\\pnacl_newlib\\\\"
        "bin/../x86_64-nacl\\\\include\"\n"
    ">\n"
    "arg: \"../../native_client/toolchain/win_x86/pnacl_newlib/bin/"
        "x86_64-nacl-clang++.exe\"\n"
    "arg: \"-MMD\"\n"
    "arg: \"-MF\"\n"
    "arg: \"clang_newlib_x64/obj/chrome/test/data/nacl/"
        "ppapi_crash_via_exit_call_nexe/ppapi_crash_via_exit_call.o.d\"\n"
    "arg: \"-Iclang_newlib_x64/gen\"\n"
    "arg: \"-c\"\n"
    "arg: \"../../chrome/test/data/nacl/crash/ppapi_crash_via_exit_call.cc\"\n"
    "arg: \"-o\"\n"
    "arg: \"clang_newlib_x64/obj/chrome/test/data/nacl/"
        "ppapi_crash_via_exit_call_nexe/ppapi_crash_via_exit_call.o\"\n"
    "env: \"PATHEXT=.COM;.EXE;.BAT;.CMD;.VBS;.VBE;.JS;.JSE;.WSF;.WSH;.MSC\"\n"
    "env: \"SystemRoot=C:\\\\Windows\"\n"
    "cwd: \"C:\\\\Users\\\\dummy\\\\out\\\\Default\"\n"
    "Input {\n"
    "  filename: \"C:\\\\Users\\\\dummy\\\\pnacl_newlib\\\\bin\\\\..\\\\"
        "lib\\\\clang\\\\3.7.0\\\\include\\\\limits.h\"\n"
    "  hash_key: \"48cdf007c86904f26d7dcd38f04f69d21022add3e48aab145a3d22"
        "16c061840d\"\n"
    "}\n";

const char kExecReqToNormalizePNaClTranslate[] =
    "command_spec <\n"
    "  name: \"clang++\"\n"
    "  version: \"4.2.1[clang version 3.7.0 (https://chromium.googlesource.com"
        "/a/native_client/pnacl-clang.git "
        "ce163fdd0f16b4481e5cf77a16d45e9b4dc8300e"
        ") (https://chromium.googlesource.com/a/native_client/pnacl-llvm.git "
        "83991f993fea6cd9c515df12c3270ab9c0746215)]\"\n"
    "  target: \"le32-unknown-nacl\"\n"
    "  binary_hash: \"b15df3ea17efb0f8e7a617dd5727aec329eae89a5c8d42dedc9602f9"
        "ae433c42\"\n"
    "  local_compiler_path: \"/dummy/pnacl_newlib/bin/pnacl-clang++\"\n"
    "  cxx_system_include_path: \"/dummy/pnacl_newlib/"
    "bin/../x86_64-nacl/include/c++/v1\"\n"
    "  cxx_system_include_path: \"/dummy/pnacl_newlib/"
        "bin/../lib/clang/3.7.0/include\"\n"
    "  cxx_system_include_path: \"/dummy/pnacl_newlib/"
        "bin/../x86_64-nacl/include\"\n"
    ">\n"
    "arg: \"../../native_client/toolchain/linux_x86/pnacl_newlib/bin/"
        "pnacl-clang++\"\n"
    "arg: \"-MMD\"\n"
    "arg: \"-MF\"\n"
    "arg: \"clang_newlib_x64/obj/chrome/test/data/nacl/"
        "ppapi_crash_via_exit_call_nexe/ppapi_crash_via_exit_call.o.d\"\n"
    "arg: \"-Iclang_newlib_x64/gen\"\n"
    "arg: \"-c\"\n"
    "arg: \"../../chrome/test/data/nacl/crash/ppapi_crash_via_exit_call.cc\"\n"
    "arg: \"-o\"\n"
    "arg: \"clang_newlib_x64/obj/chrome/test/data/nacl/"
        "ppapi_crash_via_exit_call_nexe/ppapi_crash_via_exit_call.o\"\n"
    "arg: \"--pnacl-allow-translate\"\n"
    "arg: \"-arch\"\n"
    "arg: \"x86-32-nonsfi\"\n"
    "cwd: \"/dummy/out/Default\"\n"
    "Input {\n"
    "  filename: \"/dummy/pnacl_newlib/bin/../"
        "lib/clang/3.7.0/include/limits.h\"\n"
    "  hash_key: \"48cdf007c86904f26d7dcd38f04f69d21022add3e48aab145a3d22"
        "16c061840d\"\n"
    "}\n";

const char kExecReqToNormalizeInputOrder[] = "command_spec {\n"
    "  name: \"gcc\"\n"
    "  version: \"4.4.3[Ubuntu 4.4.3-4ubuntu5]\"\n"
    "  target: \"x86_64-linux-gnu\"\n"
    "  system_include_path: \"/tmp/src/third_party/include\"\n"
    "  cxx_system_include_path: \"/tmp/src/third_party/include\"\n"
    "}\n"
    "arg: \"gcc\"\n"
    "arg: \"-I\"\n"
    "arg: \"/tmp/src/third_party/include\"\n"
    "arg: \"-Xclang\"\n"
    "arg: \"/tmp/src/third_party/lib/libFindBadConstructs.so\"\n"
    "arg: \"-c\"\n"
    "arg: \"hello.c\"\n"
    "cwd: \"/tmp/src/out/Release\"\n"
    "env: \"PWD=/tmp/src/out/Release\"\n"
    "Input {\n"
    "  filename: \"/tmp/hello1.c\"\n"
    "  hash_key: \"aaaaaaaaaa\"\n"
    "}\n"
    "Input {\n"
    "  filename: \"/tmp/src/out/Release/hello.c\"\n"
    "  hash_key: \"bbbbbbbbbb\"\n"
    "}\n"
    "Input {\n"
    "  filename: \"/tmp/test/hello2.c\"\n"
    "  hash_key: \"cccccccccc\"\n"
    "}\n";

const char kExecReqToNormalizeContent[] = "command_spec {\n"
    "  name: \"gcc\"\n"
    "  version: \"4.4.3[Ubuntu 4.4.3-4ubuntu5]\"\n"
    "  target: \"x86_64-linux-gnu\"\n"
    "  system_include_path: \"/tmp/src/third_party/include\"\n"
    "  cxx_system_include_path: \"/tmp/src/third_party/include\"\n"
    "}\n"
    "arg: \"gcc\"\n"
    "arg: \"-I\"\n"
    "arg: \"/tmp/src/third_party/include\"\n"
    "arg: \"-Xclang\"\n"
    "arg: \"/tmp/src/third_party/lib/libFindBadConstructs.so\"\n"
    "arg: \"-c\"\n"
    "arg: \"hello.c\"\n"
    "cwd: \"/tmp/src/out/Release\"\n"
    "env: \"PWD=/tmp/src/out/Release\"\n"
    "Input {\n"
    "  filename: \"/tmp/hello.c\"\n"
    "  hash_key: \"dummy_hash_key\"\n"
    "  content {\n"
    "    blob_type: FILE\n"
    "    content: \"0123456789\"\n"
    "    file_size: 10\n"
    "  }\n"
    "}\n";

// TODO: Extract this to separated file.
const char kExecReqToAmbiguaousDebugPrefixMap[] = R"(command_spec {
  name: "clang"
  version: "4.2.1[clang version 5.0.0 (trunk 300839)]"
  target: "x86_64-unknown-linux-gnu"
  binary_hash: "5f650cc98121b383aaa25e53a135d8b4c5e0748f25082b4f2d428a5934d22fda"
  local_compiler_path: "../../third_party/llvm-build/Release+Asserts/bin/clang++"
  cxx_system_include_path: "../../build/linux/debian_jessie_amd64-sysroot/usr/lib/gcc/x86_64-linux-gnu/4.8/../../../../include/c++/4.8"
  cxx_system_include_path: "../../build/linux/debian_jessie_amd64-sysroot/usr/lib/gcc/x86_64-linux-gnu/4.8/../../../../include/x86_64-linux-gnu/c++/4.8"
  cxx_system_include_path: "../../build/linux/debian_jessie_amd64-sysroot/usr/lib/gcc/x86_64-linux-gnu/4.8/../../../../include/c++/4.8/backward"
  cxx_system_include_path: "/home/goma/chromium/src/third_party/llvm-build/Release+Asserts/lib/clang/5.0.0/include"
  cxx_system_include_path: "../../build/linux/debian_jessie_amd64-sysroot/usr/include/x86_64-linux-gnu"
  cxx_system_include_path: "../../build/linux/debian_jessie_amd64-sysroot/usr/include"
}
arg: "../../third_party/llvm-build/Release+Asserts/bin/clang++"
arg: "-MMD"
arg: "-MF"
arg: "obj/base/allocator/tcmalloc/malloc_hook.o.d"
arg: "-DNO_HEAP_CHECK"
arg: "-DV8_DEPRECATION_WARNINGS"
arg: "-DDCHECK_ALWAYS_ON=1"
arg: "-DUSE_UDEV"
arg: "-DUSE_AURA=1"
arg: "-DUSE_PANGO=1"
arg: "-DUSE_CAIRO=1"
arg: "-DUSE_GLIB=1"
arg: "-DUSE_NSS_CERTS=1"
arg: "-DUSE_X11=1"
arg: "-DFULL_SAFE_BROWSING"
arg: "-DSAFE_BROWSING_CSD"
arg: "-DSAFE_BROWSING_DB_LOCAL"
arg: "-DCHROMIUM_BUILD"
arg: "-DFIELDTRIAL_TESTING_ENABLED"
arg: "-DCR_CLANG_REVISION=\"300839-1\""
arg: "-D_FILE_OFFSET_BITS=64"
arg: "-D_LARGEFILE_SOURCE"
arg: "-D_LARGEFILE64_SOURCE"
arg: "-DNDEBUG"
arg: "-DNVALGRIND"
arg: "-DDYNAMIC_ANNOTATIONS_ENABLED=0"
arg: "-DTCMALLOC_DONT_REPLACE_SYSTEM_ALLOC"
arg: "-I../../base/allocator"
arg: "-I../../third_party/tcmalloc/chromium/src/base"
arg: "-I../../third_party/tcmalloc/chromium/src"
arg: "-I../.."
arg: "-Igen"
arg: "-fno-strict-aliasing"
arg: "--param=ssp-buffer-size=4"
arg: "-fstack-protector"
arg: "-Wno-builtin-macro-redefined"
arg: "-D__DATE__="
arg: "-D__TIME__="
arg: "-D__TIMESTAMP__="
arg: "-funwind-tables"
arg: "-fPIC"
arg: "-pipe"
arg: "-B../../third_party/binutils/Linux_x64/Release/bin"
arg: "-fcolor-diagnostics"
arg: "-fdebug-prefix-map=/home/goma/chromium/src=."
arg: "-m64"
arg: "-march=x86-64"
arg: "-pthread"
arg: "-fomit-frame-pointer"
arg: "-g1"
arg: "--sysroot=../../build/linux/debian_jessie_amd64-sysroot"
arg: "-fvisibility=hidden"
arg: "-Xclang"
arg: "-load"
arg: "-Xclang"
arg: "../../third_party/llvm-build/Release+Asserts/lib/libFindBadConstructs.so"
arg: "-Xclang"
arg: "-add-plugin"
arg: "-Xclang"
arg: "find-bad-constructs"
arg: "-Xclang"
arg: "-plugin-arg-find-bad-constructs"
arg: "-Xclang"
arg: "check-auto-raw-pointer"
arg: "-Xclang"
arg: "-plugin-arg-find-bad-constructs"
arg: "-Xclang"
arg: "check-ipc"
arg: "-Wheader-hygiene"
arg: "-Wstring-conversion"
arg: "-Wtautological-overlap-compare"
arg: "-Werror"
arg: "-Wall"
arg: "-Wno-unused-variable"
arg: "-Wno-missing-field-initializers"
arg: "-Wno-unused-parameter"
arg: "-Wno-c++11-narrowing"
arg: "-Wno-covered-switch-default"
arg: "-Wno-unneeded-internal-declaration"
arg: "-Wno-inconsistent-missing-override"
arg: "-Wno-undefined-var-template"
arg: "-Wno-nonportable-include-path"
arg: "-Wno-address-of-packed-member"
arg: "-Wno-unused-lambda-capture"
arg: "-Wno-user-defined-warnings"
arg: "-Wno-reorder"
arg: "-Wno-unused-function"
arg: "-Wno-unused-local-typedefs"
arg: "-Wno-unused-private-field"
arg: "-Wno-sign-compare"
arg: "-Wno-unused-result"
arg: "-O2"
arg: "-fno-ident"
arg: "-fdata-sections"
arg: "-ffunction-sections"
arg: "-fvisibility-inlines-hidden"
arg: "-std=gnu++11"
arg: "-fno-rtti"
arg: "-fno-exceptions"
arg: "-Wno-deprecated"
arg: "-c"
arg: "../../third_party/tcmalloc/chromium/src/malloc_hook.cc"
arg: "-o"
arg: "obj/base/allocator/tcmalloc/malloc_hook.o"
arg: "-fuse-init-array"
env: "PWD=/home/goma/chromium/src/out/rel_ng"
cwd: "/home/goma/chromium/src/out/rel_ng"
subprogram {
  path: "home/goma/chromium/src/out/rel_ng/../../third_party/llvm-build/Release+Asserts/lib/libFindBadConstructs.so"
  binary_hash: "119407f17eb4777402734571183eb5518806900d9c7c7ce5ad71d242aad249f0"
}
subprogram {
  path: "/home/goma/chromium/src/out/rel_ng/../../third_party/binutils/Linux_x64/Release/bin/objcopy"
  binary_hash: "9ccd249906d57ef2ccd24cf19c67c8d645d309c49c284af9d42813caf87fba7e"
}
requester_info {
  username: "goma"
  compiler_proxy_id: "goma@goma.example.com:8088/1494385386/0"
  api_version: 2
  pid: 94105
  retry: 0
}
hermetic_mode: true
experimental_is_external_user: false
)";

const char kExecReqToNormalizeDebugPrefixMapAlice[] = "command_spec {\n"
    "  name: \"clang\"\n"
    "  version: \"4.2.1[clang version 3.5.0 (trunk 214024)]\"\n"
    "  target: \"x86_64-unknown-linux-gnu\"\n"
    "  system_include_path: \"/home/alice/src/third_party/include\"\n"
    "  cxx_system_include_path: \"/home/alice/src/third_party/include\"\n"
    "}\n"
    "arg: \"clang\"\n"
    "arg: \"-I\"\n"
    "arg: \"/home/alice/src/third_party/include\"\n"
    "arg: \"-Xclang\"\n"
    "arg: \"/home/alice/src/third_party/lib/libFindBadConstructs.so\"\n"
    "arg: \"-gcc-toolchain=/home/alice/src/third_party/target_toolchain\"\n"
    "arg: \"-B/home/alice/src/out/Release/bin\"\n"
    "arg: \"--sysroot=/home/alice/src/build/linux/sysroot\"\n"
    "arg: \"-resource-dir=/home/alice/src/third_party/clang\"\n"
    "arg: \"-g\"\n"
    "arg: \"-fdebug-prefix-map=/home/alice=/base_dir\"\n"
    "arg: \"-c\"\n"
    "arg: \"hello.c\"\n"
    "cwd: \"/home/alice/src/out/Release\"\n"
    "env: \"PWD=/tmp/src/out/Release\"\n"
    "Input {\n"
    "  filename: \"/home/alice/hello.c\"\n"
    "  hash_key: \"152d72ea117deff2af0cf0ca3aaa46a20a5f0c0e4ccb8b6d"
        "559d507401ae81e9\"\n"
    "}\n";

const char kExecReqToNormalizeDebugPrefixMapBob[] = "command_spec {\n"
    "  name: \"clang\"\n"
    "  version: \"4.2.1[clang version 3.5.0 (trunk 214024)]\"\n"
    "  target: \"x86_64-unknown-linux-gnu\"\n"
    "  system_include_path: \"/home/bob/src/third_party/include\"\n"
    "  cxx_system_include_path: \"/home/bob/src/third_party/include\"\n"
    "}\n"
    "arg: \"clang\"\n"
    "arg: \"-I\"\n"
    "arg: \"/home/bob/src/third_party/include\"\n"
    "arg: \"-Xclang\"\n"
    "arg: \"/home/bob/src/third_party/lib/libFindBadConstructs.so\"\n"
    "arg: \"-gcc-toolchain=/home/bob/src/third_party/target_toolchain\"\n"
    "arg: \"-B/home/bob/src/out/Release/bin\"\n"
    "arg: \"--sysroot=/home/bob/src/build/linux/sysroot\"\n"
    "arg: \"-resource-dir=/home/bob/src/third_party/clang\"\n"
    "arg: \"-g\"\n"
    "arg: \"-fdebug-prefix-map=/home/bob=/base_dir\"\n"
    "arg: \"-c\"\n"
    "arg: \"hello.c\"\n"
    "cwd: \"/home/bob/src/out/Release\"\n"
    "env: \"PWD=/tmp/src/out/Release\"\n"
    "Input {\n"
    "  filename: \"/home/bob/hello.c\"\n"
    "  hash_key: \"152d72ea117deff2af0cf0ca3aaa46a20a5f0c0e4ccb8b6d"
        "559d507401ae81e9\"\n"
    "}\n";

// Test case for arg "-fdebug-prefix-map=/proc/self/cwd="
const char kExecReqToNormalizeDebugPrefixMapAlicePSC[] = "command_spec {\n"
    "  name: \"clang\"\n"
    "  version: \"4.2.1[clang version 3.5.0 (trunk 214024)]\"\n"
    "  target: \"x86_64-unknown-linux-gnu\"\n"
    "  system_include_path: \"/home/alice/src/third_party/include\"\n"
    "  cxx_system_include_path: \"/home/alice/src/third_party/include\"\n"
    "}\n"
    "arg: \"clang\"\n"
    "arg: \"-I\"\n"
    "arg: \"/home/alice/src/third_party/include\"\n"
    "arg: \"-Xclang\"\n"
    "arg: \"/home/alice/src/third_party/lib/libFindBadConstructs.so\"\n"
    "arg: \"-gcc-toolchain=/home/alice/src/third_party/target_toolchain\"\n"
    "arg: \"-B/home/alice/src/out/Release/bin\"\n"
    "arg: \"--sysroot=/home/alice/src/build/linux/sysroot\"\n"
    "arg: \"-resource-dir=/home/alice/src/third_party/clang\"\n"
    "arg: \"-g\"\n"
    "arg: \"-fdebug-prefix-map=/proc/self/cwd=\"\n"
    "arg: \"-c\"\n"
    "arg: \"hello.c\"\n"
    "cwd: \"/home/alice/src\"\n"
    "env: \"PWD=/proc/self/cwd\"\n"
    "Input {\n"
    "  filename: \"/home/alice/src/hello.c\"\n"
    "  hash_key: \"152d72ea117deff2af0cf0ca3aaa46a20a5f0c0e4ccb8b6d"
        "559d507401ae81e9\"\n"
    "}\n";

const char kExecReqToNormalizeDebugPrefixMapBobPSC[] = "command_spec {\n"
    "  name: \"clang\"\n"
    "  version: \"4.2.1[clang version 3.5.0 (trunk 214024)]\"\n"
    "  target: \"x86_64-unknown-linux-gnu\"\n"
    "  system_include_path: \"/home/bob/src/third_party/include\"\n"
    "  cxx_system_include_path: \"/home/bob/src/third_party/include\"\n"
    "}\n"
    "arg: \"clang\"\n"
    "arg: \"-I\"\n"
    "arg: \"/home/bob/src/third_party/include\"\n"
    "arg: \"-Xclang\"\n"
    "arg: \"/home/bob/src/third_party/lib/libFindBadConstructs.so\"\n"
    "arg: \"-gcc-toolchain=/home/bob/src/third_party/target_toolchain\"\n"
    "arg: \"-B/home/bob/src/out/Release/bin\"\n"
    "arg: \"--sysroot=/home/bob/src/build/linux/sysroot\"\n"
    "arg: \"-resource-dir=/home/bob/src/third_party/clang\"\n"
    "arg: \"-g\"\n"
    "arg: \"-fdebug-prefix-map=/proc/self/cwd=\"\n"
    "arg: \"-c\"\n"
    "arg: \"hello.c\"\n"
    "cwd: \"/home/bob/src\"\n"
    "env: \"PWD=/proc/self/cwd\"\n"
    "Input {\n"
    "  filename: \"/home/bob/src/hello.c\"\n"
    "  hash_key: \"152d72ea117deff2af0cf0ca3aaa46a20a5f0c0e4ccb8b6d"
        "559d507401ae81e9\"\n"
    "}\n";

// Test case for arg both "-fdebug-prefix-map=/proc/self/cwd=" and
// "-fdebug-prefix-map=/home/$USER/src/=" given.
// TODO: Have test to confirm that
// the determinism of build is in the way we intended.
const char kExecReqToNormalize2DebugPrefixMapAlicePSC[] = "command_spec {\n"
    "  name: \"clang\"\n"
    "  version: \"4.2.1[clang version 3.5.0 (trunk 214024)]\"\n"
    "  target: \"x86_64-unknown-linux-gnu\"\n"
    "  system_include_path: \"/home/alice/src/third_party/include\"\n"
    "  cxx_system_include_path: \"/home/alice/src/third_party/include\"\n"
    "}\n"
    "arg: \"clang\"\n"
    "arg: \"-I\"\n"
    "arg: \"/home/alice/src/third_party/include\"\n"
    "arg: \"-Xclang\"\n"
    "arg: \"/home/alice/src/third_party/lib/libFindBadConstructs.so\"\n"
    "arg: \"-gcc-toolchain=/home/alice/src/third_party/target_toolchain\"\n"
    "arg: \"-B/home/alice/src/out/Release/bin\"\n"
    "arg: \"--sysroot=/home/alice/src/build/linux/sysroot\"\n"
    "arg: \"-resource-dir=/home/alice/src/third_party/clang\"\n"
    "arg: \"-g\"\n"
    "arg: \"-fdebug-prefix-map=/proc/self/cwd=\"\n"
    "arg: \"-fdebug-prefix-map=/home/alice/src/=\"\n"
    "arg: \"-c\"\n"
    "arg: \"hello.c\"\n"
    "cwd: \"/home/alice/src\"\n"
    "env: \"PWD=/proc/self/cwd\"\n"
    "Input {\n"
    "  filename: \"/home/alice/src/hello.c\"\n"
    "  hash_key: \"152d72ea117deff2af0cf0ca3aaa46a20a5f0c0e4ccb8b6d"
        "559d507401ae81e9\"\n"
    "}\n";

const char kExecReqToNormalize2DebugPrefixMapBobPSC[] = "command_spec {\n"
    "  name: \"clang\"\n"
    "  version: \"4.2.1[clang version 3.5.0 (trunk 214024)]\"\n"
    "  target: \"x86_64-unknown-linux-gnu\"\n"
    "  system_include_path: \"/home/bob/src/third_party/include\"\n"
    "  cxx_system_include_path: \"/home/bob/src/third_party/include\"\n"
    "}\n"
    "arg: \"clang\"\n"
    "arg: \"-I\"\n"
    "arg: \"/home/bob/src/third_party/include\"\n"
    "arg: \"-Xclang\"\n"
    "arg: \"/home/bob/src/third_party/lib/libFindBadConstructs.so\"\n"
    "arg: \"-gcc-toolchain=/home/bob/src/third_party/target_toolchain\"\n"
    "arg: \"-B/home/bob/src/out/Release/bin\"\n"
    "arg: \"--sysroot=/home/bob/src/build/linux/sysroot\"\n"
    "arg: \"-resource-dir=/home/bob/src/third_party/clang\"\n"
    "arg: \"-g\"\n"
    "arg: \"-fdebug-prefix-map=/proc/self/cwd=\"\n"
    "arg: \"-fdebug-prefix-map=/home/bob/src/=\"\n"
    "arg: \"-c\"\n"
    "arg: \"hello.c\"\n"
    "cwd: \"/home/bob/src\"\n"
    "env: \"PWD=/proc/self/cwd\"\n"
    "Input {\n"
    "  filename: \"/home/bob/src/hello.c\"\n"
    "  hash_key: \"152d72ea117deff2af0cf0ca3aaa46a20a5f0c0e4ccb8b6d"
        "559d507401ae81e9\"\n"
    "}\n";

// Test case for arg both "-fdebug-prefix-map=/proc/self/cwd=" and
// "-fdebug-prefix-map=/home/$USER/src/=" given in gcc.
const char kExecReqToNormalize2DebugPrefixMapAlicePSCGCC[] = "command_spec {\n"
    "  name: \"gcc\"\n"
    "  version: \"4.4.3[Ubuntu 4.4.3-4ubuntu5]\"\n"
    "  target: \"x86_64-linux-gnu\"\n"
    "  system_include_path: \"/home/alice/src/third_party/include\"\n"
    "  cxx_system_include_path: \"/home/alice/src/third_party/include\"\n"
    "}\n"
    "arg: \"gcc\"\n"
    "arg: \"-I\"\n"
    "arg: \"third_party/include\"\n"
    "arg: \"-gcc-toolchain=third_party/target_toolchain\"\n"
    "arg: \"-Bout/Release/bin\"\n"
    "arg: \"--sysroot=/home/alice/src/build/linux/sysroot\"\n"
    "arg: \"-resource-dir=/home/alice/src/third_party/clang\"\n"
    "arg: \"-g\"\n"
    "arg: \"-fdebug-prefix-map=/proc/self/cwd=\"\n"
    "arg: \"-fdebug-prefix-map=/home/alice/src/=\"\n"
    "arg: \"-c\"\n"
    "arg: \"hello.c\"\n"
    "cwd: \"/home/alice/src\"\n"
    "env: \"PWD=/proc/self/cwd\"\n"
    "Input {\n"
    "  filename: \"/home/alice/src/hello.c\"\n"
    "  hash_key: \"152d72ea117deff2af0cf0ca3aaa46a20a5f0c0e4ccb8b6d"
        "559d507401ae81e9\"\n"
    "}\n";

const char kExecReqToNormalize2DebugPrefixMapBobPSCGCC[] = "command_spec {\n"
    "  name: \"gcc\"\n"
    "  version: \"4.4.3[Ubuntu 4.4.3-4ubuntu5]\"\n"
    "  target: \"x86_64-linux-gnu\"\n"
    "  system_include_path: \"/home/bob/src/third_party/include\"\n"
    "  cxx_system_include_path: \"/home/bob/src/third_party/include\"\n"
    "}\n"
    "arg: \"gcc\"\n"
    "arg: \"-I\"\n"
    "arg: \"third_party/include\"\n"
    "arg: \"-gcc-toolchain=third_party/target_toolchain\"\n"
    "arg: \"-Bout/Release/bin\"\n"
    "arg: \"--sysroot=/home/bob/src/build/linux/sysroot\"\n"
    "arg: \"-resource-dir=/home/bob/src/third_party/clang\"\n"
    "arg: \"-g\"\n"
    "arg: \"-fdebug-prefix-map=/proc/self/cwd=\"\n"
    "arg: \"-fdebug-prefix-map=/home/bob/src/=\"\n"
    "arg: \"-c\"\n"
    "arg: \"hello.c\"\n"
    "cwd: \"/home/bob/src\"\n"
    "env: \"PWD=/proc/self/cwd\"\n"
    "Input {\n"
    "  filename: \"/home/bob/src/hello.c\"\n"
    "  hash_key: \"152d72ea117deff2af0cf0ca3aaa46a20a5f0c0e4ccb8b6d"
        "559d507401ae81e9\"\n"
    "}\n";

// Test case for arg "-fdebug-prefix-map=/proc/self/cwd=" in gcc.
const char kExecReqToNormalizeDebugPrefixMapAlicePSCGCC[] = "command_spec {\n"
    "  name: \"gcc\"\n"
    "  version: \"4.4.3[Ubuntu 4.4.3-4ubuntu5]\"\n"
    "  target: \"x86_64-linux-gnu\"\n"
    "  system_include_path: \"/tmp/src/third_party/include\"\n"
    "  cxx_system_include_path: \"/tmp/src/third_party/include\"\n"
    "}\n"
    "arg: \"gcc\"\n"
    "arg: \"-I\"\n"
    "arg: \"third_party/include\"\n"
    "arg: \"-gcc-toolchain=third_party/target_toolchain\"\n"
    "arg: \"-Bout/Release/bin\"\n"
    "arg: \"--sysroot=src/build/linux/sysroot\"\n"
    "arg: \"-resource-dir=src/third_party/clang\"\n"
    "arg: \"-g\"\n"
    "arg: \"-fdebug-prefix-map=/proc/self/cwd=\"\n"
    "arg: \"-c\"\n"
    "arg: \"hello.c\"\n"
    "cwd: \"/home/alice/src\"\n"
    "env: \"PWD=/proc/self/cwd\"\n"
    "Input {\n"
    "  filename: \"/home/alice/src/hello.c\"\n"
    "  hash_key: \"152d72ea117deff2af0cf0ca3aaa46a20a5f0c0e4ccb8b6d"
        "559d507401ae81e9\"\n"
    "}\n";

const char kExecReqToNormalizeDebugPrefixMapBobPSCGCC[] = "command_spec {\n"
    "  name: \"gcc\"\n"
    "  version: \"4.4.3[Ubuntu 4.4.3-4ubuntu5]\"\n"
    "  target: \"x86_64-linux-gnu\"\n"
    "  system_include_path: \"/tmp/src/third_party/include\"\n"
    "  cxx_system_include_path: \"/tmp/src/third_party/include\"\n"
    "}\n"
    "arg: \"gcc\"\n"
    "arg: \"-I\"\n"
    "arg: \"third_party/include\"\n"
    "arg: \"-gcc-toolchain=third_party/target_toolchain\"\n"
    "arg: \"-Bout/Release/bin\"\n"
    "arg: \"--sysroot=src/build/linux/sysroot\"\n"
    "arg: \"-resource-dir=src/third_party/clang\"\n"
    "arg: \"-g\"\n"
    "arg: \"-fdebug-prefix-map=/proc/self/cwd=\"\n"
    "arg: \"-c\"\n"
    "arg: \"hello.c\"\n"
    "cwd: \"/home/bob/src\"\n"
    "env: \"PWD=/proc/self/cwd\"\n"
    "Input {\n"
    "  filename: \"/home/bob/src/hello.c\"\n"
    "  hash_key: \"152d72ea117deff2af0cf0ca3aaa46a20a5f0c0e4ccb8b6d"
        "559d507401ae81e9\"\n"
    "}\n";

// Test case for preserve arg "-fdebug-prefix-map=/proc/self/cwd=" in gcc.
const char kExecReqToNoNormalizeDebugPrefixMapAlicePSCGCC[] = "command_spec {\n"
    "  name: \"gcc\"\n"
    "  version: \"4.4.3[Ubuntu 4.4.3-4ubuntu5]\"\n"
    "  target: \"x86_64-linux-gnu\"\n"
    "  system_include_path: \"/tmp/src/third_party/include\"\n"
    "  cxx_system_include_path: \"/tmp/src/third_party/include\"\n"
    "}\n"
    "arg: \"gcc\"\n"
    "arg: \"-I\"\n"
    "arg: \"/home/alice/src/third_party/include\"\n"
    "arg: \"-gcc-toolchain=/home/alice/src/third_party/target_toolchain\"\n"
    "arg: \"-B/home/alice/src/out/Release/bin\"\n"
    "arg: \"--sysroot=/home/alice/src/build/linux/sysroot\"\n"
    "arg: \"-resource-dir=/home/alice/src/third_party/clang\"\n"
    "arg: \"-g\"\n"
    "arg: \"-fdebug-prefix-map=/proc/self/cwd=\"\n"
    "arg: \"-c\"\n"
    "arg: \"hello.c\"\n"
    "cwd: \"/home/alice/src\"\n"
    "env: \"PWD=/proc/self/cwd\"\n"
    "Input {\n"
    "  filename: \"/home/alice/src/hello.c\"\n"
    "  hash_key: \"152d72ea117deff2af0cf0ca3aaa46a20a5f0c0e4ccb8b6d"
        "559d507401ae81e9\"\n"
    "}\n";

const char kExecReqToNoNormalizeDebugPrefixMapBobPSCGCC[] = "command_spec {\n"
    "  name: \"gcc\"\n"
    "  version: \"4.4.3[Ubuntu 4.4.3-4ubuntu5]\"\n"
    "  target: \"x86_64-linux-gnu\"\n"
    "  system_include_path: \"/tmp/src/third_party/include\"\n"
    "  cxx_system_include_path: \"/tmp/src/third_party/include\"\n"
    "}\n"
    "arg: \"gcc\"\n"
    "arg: \"-I\"\n"
    "arg: \"/home/bob/src/third_party/include\"\n"
    "arg: \"-gcc-toolchain=/home/bob/src/third_party/target_toolchain\"\n"
    "arg: \"-B/home/bob/src/out/Release/bin\"\n"
    "arg: \"--sysroot=/home/bob/src/build/linux/sysroot\"\n"
    "arg: \"-resource-dir=/home/bob/src/third_party/clang\"\n"
    "arg: \"-g\"\n"
    "arg: \"-fdebug-prefix-map=/proc/self/cwd=\"\n"
    "arg: \"-c\"\n"
    "arg: \"hello.c\"\n"
    "cwd: \"/home/bob/src\"\n"
    "env: \"PWD=/proc/self/cwd\"\n"
    "Input {\n"
    "  filename: \"/home/bob/src/hello.c\"\n"
    "  hash_key: \"152d72ea117deff2af0cf0ca3aaa46a20a5f0c0e4ccb8b6d"
        "559d507401ae81e9\"\n"
    "}\n";

// Test case for arg "-fdebug-prefix-map=/proc/self/cwd="
// without PWD=/proc/self/cwd
const char kExecReqToNormalizeDebugPrefixMapAlicePSCNoPWD[] = "command_spec {\n"
    "  name: \"clang\"\n"
    "  version: \"4.2.1[clang version 3.5.0 (trunk 214024)]\"\n"
    "  target: \"x86_64-unknown-linux-gnu\"\n"
    "  system_include_path: \"/home/alice/src/third_party/include\"\n"
    "  cxx_system_include_path: \"/home/alice/src/third_party/include\"\n"
    "}\n"
    "arg: \"clang\"\n"
    "arg: \"-I\"\n"
    "arg: \"/home/alice/src/third_party/include\"\n"
    "arg: \"-Xclang\"\n"
    "arg: \"/home/alice/src/third_party/lib/libFindBadConstructs.so\"\n"
    "arg: \"-gcc-toolchain=/home/alice/src/third_party/target_toolchain\"\n"
    "arg: \"-B/home/alice/src/out/Release/bin\"\n"
    "arg: \"--sysroot=/home/alice/src/build/linux/sysroot\"\n"
    "arg: \"-resource-dir=/home/alice/src/third_party/clang\"\n"
    "arg: \"-g\"\n"
    "arg: \"-fdebug-prefix-map=/proc/self/cwd=\"\n"
    "arg: \"-c\"\n"
    "arg: \"hello.c\"\n"
    "cwd: \"/home/alice/src\"\n"
    "env: \"PWD=/home/alice/src\"\n"
    "Input {\n"
    "  filename: \"/home/alice/src/hello.c\"\n"
    "  hash_key: \"152d72ea117deff2af0cf0ca3aaa46a20a5f0c0e4ccb8b6d"
        "559d507401ae81e9\"\n"
    "}\n";

const char kExecReqToNormalizeDebugPrefixMapBobPSCNoPWD[] = "command_spec {\n"
    "  name: \"clang\"\n"
    "  version: \"4.2.1[clang version 3.5.0 (trunk 214024)]\"\n"
    "  target: \"x86_64-unknown-linux-gnu\"\n"
    "  system_include_path: \"/home/bob/src/third_party/include\"\n"
    "  cxx_system_include_path: \"/home/bob/src/third_party/include\"\n"
    "}\n"
    "arg: \"clang\"\n"
    "arg: \"-I\"\n"
    "arg: \"/home/bob/src/third_party/include\"\n"
    "arg: \"-Xclang\"\n"
    "arg: \"/home/bob/src/third_party/lib/libFindBadConstructs.so\"\n"
    "arg: \"-gcc-toolchain=/home/bob/src/third_party/target_toolchain\"\n"
    "arg: \"-B/home/bob/src/out/Release/bin\"\n"
    "arg: \"--sysroot=/home/bob/src/build/linux/sysroot\"\n"
    "arg: \"-resource-dir=/home/bob/src/third_party/clang\"\n"
    "arg: \"-g\"\n"
    "arg: \"-fdebug-prefix-map=/proc/self/cwd=\"\n"
    "arg: \"-c\"\n"
    "arg: \"hello.c\"\n"
    "cwd: \"/home/bob/src\"\n"
    "env: \"PWD=/home/bob/src\"\n"
    "Input {\n"
    "  filename: \"/home/bob/src/hello.c\"\n"
    "  hash_key: \"152d72ea117deff2af0cf0ca3aaa46a20a5f0c0e4ccb8b6d"
        "559d507401ae81e9\"\n"
    "}\n";

const char kExecReqToNormalizeJavac[] = "command_spec {\n"
    "  name: \"javac\"\n"
    "  version: \"1.8.0_45-internal\"\n"
    "  target: \"java\"\n"
    "}\n"
    "arg: \"javac\"\n"
    "arg: \"-J-Xmx1024M\"\n"
    "arg: \"-Xmaxerrs\"\n"
    "arg: \"9999999\"\n"
    "arg: \"-encoding\"\n"
    "arg: \"UTF-8\"\n"
    "arg: \"-bootclasspath\"\n"
    "arg: \"-classpath\"\n"
    "arg: \"dummy.jar:dummy2.jar\"\n"
    "arg: \"-extdirs\"\n"
    "arg: \"-d\"\n"
    "arg: \"dest\"\n"
    "arg: \"-g\"\n"
    "arg: \"-encoding\"\n"
    "arg: \"UTF-8\"\n"
    "arg: \"-Xmaxwarns\"\n"
    "arg: \"9999999\"\n"
    "arg: \"-source\"\n"
    "arg: \"1.8\"\n"
    "arg: \"-target\"\n"
    "arg: \"1.8\"\n"
    "arg: \"hello.java\"\n"
    "cwd: \"/home/bob/src\"\n"
    "env: \"PWD=/home/bob/src\"\n"
    "Input {\n"
    "  filename: \"/home/bob/src/hello.java\"\n"
    "  hash_key: \"152d72ea117deff2af0cf0ca3aaa46a20a5f0c0e4ccb8b6d"
        "559d507401ae81e9\"\n"
    "}\n";

}  // anonymous namespace

TEST(ExecReqNormalizerTest, NormalizeExecReqForCacheKey) {
  devtools_goma::ExecReq req;
  const std::vector<string> kTestOptions{
      "Xclang", "B", "I", "gcc-toolchain", "-sysroot", "resource-dir"};

  // Check all features can be disabled.
  ASSERT_TRUE(TextFormat::ParseFromString(kExecReqToNormalize, &req));
  ASSERT_TRUE(devtools_goma::VerifyExecReq(req));
  devtools_goma::NormalizeExecReqForCacheKey(
      0, false, false, std::vector<string>(), std::map<string, string>(), &req);
  EXPECT_EQ(1, req.command_spec().system_include_path_size());
  EXPECT_EQ("/tmp/src/third_party/include",
            req.command_spec().system_include_path(0));
  EXPECT_EQ(1, req.command_spec().cxx_system_include_path_size());
  EXPECT_EQ("/tmp/src/third_party/include",
            req.command_spec().cxx_system_include_path(0));
  EXPECT_EQ(kExecReqToNormalizeArgSize, req.arg_size());
  EXPECT_EQ("/tmp/src/third_party/include", req.arg(2));
  EXPECT_EQ("/tmp/src/third_party/lib/libFindBadConstructs.so", req.arg(4));
  EXPECT_EQ("-gcc-toolchain=/tmp/src/third_party/target_toolchain", req.arg(5));
  EXPECT_EQ("-B/tmp/src/out/Release/bin", req.arg(6));
  EXPECT_EQ("--sysroot=/tmp/src/build/linux/sysroot", req.arg(7));
  EXPECT_EQ("-resource-dir=/tmp/src/third_party/clang", req.arg(8));
  EXPECT_TRUE(req.cwd().empty());
  EXPECT_TRUE(req.env().empty());
  EXPECT_EQ(1, req.input_size());
  EXPECT_FALSE(req.input(0).has_filename());
  EXPECT_TRUE(req.input(0).has_hash_key());
}

TEST(ExecReqNormalizerTest, NormalizeExecReqForCacheKeyRelativeSystemPath) {
  devtools_goma::ExecReq req;
  const std::vector<string> kTestOptions{
      "Xclang", "B", "I", "gcc-toolchain", "-sysroot", "resource-dir"};

  // Convert system include path.
  ASSERT_TRUE(TextFormat::ParseFromString(kExecReqToNormalize, &req));
  ASSERT_TRUE(devtools_goma::VerifyExecReq(req));
  devtools_goma::NormalizeExecReqForCacheKey(
      0, true, false, std::vector<string>(), std::map<string, string>(), &req);
  EXPECT_EQ(1, req.command_spec().system_include_path_size());
  EXPECT_EQ("../../third_party/include",
            req.command_spec().system_include_path(0));
  EXPECT_EQ(1, req.command_spec().cxx_system_include_path_size());
  EXPECT_EQ("../../third_party/include",
            req.command_spec().cxx_system_include_path(0));
  EXPECT_EQ(kExecReqToNormalizeArgSize, req.arg_size());
  EXPECT_EQ("/tmp/src/third_party/include", req.arg(2));
  EXPECT_EQ("/tmp/src/third_party/lib/libFindBadConstructs.so", req.arg(4));
  EXPECT_EQ("-gcc-toolchain=/tmp/src/third_party/target_toolchain", req.arg(5));
  EXPECT_EQ("-B/tmp/src/out/Release/bin", req.arg(6));
  EXPECT_EQ("--sysroot=/tmp/src/build/linux/sysroot", req.arg(7));
  EXPECT_EQ("-resource-dir=/tmp/src/third_party/clang", req.arg(8));
  EXPECT_TRUE(req.cwd().empty());
  EXPECT_TRUE(req.env().empty());
  EXPECT_EQ(1, req.input_size());
  EXPECT_FALSE(req.input(0).has_filename());
  EXPECT_TRUE(req.input(0).has_hash_key());
}

// Convert arguments followed by the certain flags.
TEST(ExecReqNormalizerTest, NormalizeExecReqForCacheKeyRelativeSysroot) {
  devtools_goma::ExecReq req;
  const std::vector<string> kTestOptions{
      "Xclang", "B", "I", "gcc-toolchain", "-sysroot", "resource-dir"};

  ASSERT_TRUE(TextFormat::ParseFromString(kExecReqToNormalize, &req));
  ASSERT_TRUE(devtools_goma::VerifyExecReq(req));
  devtools_goma::NormalizeExecReqForCacheKey(
      0, false, false, kTestOptions, std::map<string, string>(), &req);
  EXPECT_EQ(1, req.command_spec().system_include_path_size());
  EXPECT_EQ("/tmp/src/third_party/include",
            req.command_spec().system_include_path(0));
  EXPECT_EQ(1, req.command_spec().cxx_system_include_path_size());
  EXPECT_EQ("/tmp/src/third_party/include",
            req.command_spec().cxx_system_include_path(0));
  EXPECT_EQ(kExecReqToNormalizeArgSize, req.arg_size());
  EXPECT_EQ("../../third_party/include", req.arg(2));
  EXPECT_EQ("../../third_party/lib/libFindBadConstructs.so", req.arg(4));
  EXPECT_EQ("-gcc-toolchain=../../third_party/target_toolchain", req.arg(5));
  EXPECT_EQ("-Bbin", req.arg(6));
  EXPECT_EQ("--sysroot=../../build/linux/sysroot", req.arg(7));
  EXPECT_EQ("-resource-dir=../../third_party/clang", req.arg(8));
  EXPECT_TRUE(req.cwd().empty());
  EXPECT_TRUE(req.env().empty());
  EXPECT_EQ(1, req.input_size());
  EXPECT_FALSE(req.input(0).has_filename());
  EXPECT_TRUE(req.input(0).has_hash_key());
}

// -g.
TEST(ExecReqNormalizerTest, NormalizeExecReqForCacheKeyWithFlagG) {
  devtools_goma::ExecReq req;
  const std::vector<string> kTestOptions{
      "Xclang", "B", "I", "gcc-toolchain", "-sysroot", "resource-dir"};

  ASSERT_TRUE(TextFormat::ParseFromString(kExecReqToNormalize, &req));
  req.add_arg("-g");
  ASSERT_TRUE(devtools_goma::VerifyExecReq(req));
  devtools_goma::NormalizeExecReqForCacheKey(
      0, true, false, kTestOptions, std::map<string, string>(), &req);
  EXPECT_EQ(1, req.command_spec().system_include_path_size());
  EXPECT_EQ("/tmp/src/third_party/include",
            req.command_spec().system_include_path(0));
  EXPECT_EQ(1, req.command_spec().cxx_system_include_path_size());
  EXPECT_EQ("/tmp/src/third_party/include",
            req.command_spec().cxx_system_include_path(0));
  // +1 because "-g" is added.
  EXPECT_EQ(kExecReqToNormalizeArgSize + 1, req.arg_size());
  EXPECT_EQ("/tmp/src/third_party/include", req.arg(2));
  EXPECT_EQ("/tmp/src/third_party/lib/libFindBadConstructs.so", req.arg(4));
  EXPECT_EQ("/tmp/src/out/Release", req.cwd());
  EXPECT_FALSE(req.env().empty());
  EXPECT_EQ("PWD=/tmp/src/out/Release", req.env(0));
  EXPECT_EQ(1, req.input_size());
  EXPECT_EQ("/tmp/src/hello.c", req.input(0).filename());
  EXPECT_TRUE(req.input(0).has_hash_key());
}

// -g0.
TEST(ExecReqNormalizerTest, NormalizeExecReqForCacheKeyWithFlagG0) {
  devtools_goma::ExecReq req;
  const std::vector<string> kTestOptions{
      "Xclang", "B", "I", "gcc-toolchain", "-sysroot", "resource-dir"};

  ASSERT_TRUE(TextFormat::ParseFromString(kExecReqToNormalize, &req));
  req.add_arg("-g0");
  ASSERT_TRUE(devtools_goma::VerifyExecReq(req));
  devtools_goma::NormalizeExecReqForCacheKey(
      0, true, false, kTestOptions, std::map<string, string>(), &req);
  EXPECT_EQ(1, req.command_spec().system_include_path_size());
  EXPECT_EQ("../../third_party/include",
            req.command_spec().system_include_path(0));
  EXPECT_EQ(1, req.command_spec().cxx_system_include_path_size());
  EXPECT_EQ("../../third_party/include",
            req.command_spec().cxx_system_include_path(0));
  // +1 because "-g0" is added.
  EXPECT_EQ(kExecReqToNormalizeArgSize + 1, req.arg_size());
  EXPECT_EQ("../../third_party/include", req.arg(2));
  EXPECT_EQ("../../third_party/lib/libFindBadConstructs.so", req.arg(4));
  EXPECT_TRUE(req.cwd().empty());
  EXPECT_TRUE(req.env().empty());
  EXPECT_EQ(1, req.input_size());
  EXPECT_FALSE(req.input(0).has_filename());
  EXPECT_TRUE(req.input(0).has_hash_key());
}

// -gsplit-dwarf (fission)
TEST(ExecReqNormalizerTest, NormalizeExecReqForCacheKeyWithFission) {
  devtools_goma::ExecReq req;
  const std::vector<string> kTestOptions{
      "Xclang", "B", "I", "gcc-toolchain", "-sysroot", "resource-dir"};

  ASSERT_TRUE(TextFormat::ParseFromString(kExecReqToNormalize, &req));
  req.add_arg("-gsplit-dwarf");
  ASSERT_TRUE(devtools_goma::VerifyExecReq(req));
  devtools_goma::NormalizeExecReqForCacheKey(
      0, true, false, kTestOptions, std::map<string, string>(), &req);
  EXPECT_EQ(1, req.command_spec().system_include_path_size());
  EXPECT_EQ("/tmp/src/third_party/include",
            req.command_spec().system_include_path(0));
  EXPECT_EQ(1, req.command_spec().cxx_system_include_path_size());
  EXPECT_EQ("/tmp/src/third_party/include",
            req.command_spec().cxx_system_include_path(0));
  // +1 because "-gsplit-dwarf" is added.
  EXPECT_EQ(kExecReqToNormalizeArgSize + 1, req.arg_size());
  EXPECT_EQ("/tmp/src/third_party/include", req.arg(2));
  EXPECT_EQ("/tmp/src/third_party/lib/libFindBadConstructs.so", req.arg(4));
  EXPECT_EQ("/tmp/src/out/Release", req.cwd());
  EXPECT_FALSE(req.env().empty());
  EXPECT_EQ("PWD=/tmp/src/out/Release", req.env(0));
  EXPECT_EQ(1, req.input_size());
  EXPECT_EQ("/tmp/src/hello.c", req.input(0).filename());
  EXPECT_TRUE(req.input(0).has_hash_key());
}

// -fdebug-prefix-map should be normalized with release build.
TEST(ExecReqNormalizerTest, NormalizeExecReqForCacheKeyWithDebugPrefixMap) {
  devtools_goma::ExecReq req;
  const std::vector<string> kTestOptions{
      "Xclang", "B", "I", "gcc-toolchain", "-sysroot", "resource-dir"};

  ASSERT_TRUE(TextFormat::ParseFromString(kExecReqToNormalize, &req));
  req.add_arg("-fdebug-prefix-map=/tmp/src=/ts");
  ASSERT_TRUE(devtools_goma::VerifyExecReq(req));
  devtools_goma::NormalizeExecReqForCacheKey(
      0, true, false, kTestOptions, std::map<string, string>(), &req);
  EXPECT_EQ(1, req.command_spec().system_include_path_size());
  EXPECT_EQ("../../third_party/include",
            req.command_spec().system_include_path(0));
  EXPECT_EQ(1, req.command_spec().cxx_system_include_path_size());
  EXPECT_EQ("../../third_party/include",
            req.command_spec().cxx_system_include_path(0));
  // +1 because "-fdebug-prefix-map" is added.
  EXPECT_EQ(kExecReqToNormalizeArgSize + 1, req.arg_size());
  EXPECT_EQ("../../third_party/include", req.arg(2));
  EXPECT_EQ("../../third_party/lib/libFindBadConstructs.so", req.arg(4));
  EXPECT_EQ("-fdebug-prefix-map=", req.arg(kExecReqToNormalizeArgSize));
  EXPECT_TRUE(req.cwd().empty());
  EXPECT_TRUE(req.env().empty());
  EXPECT_EQ(1, req.input_size());
  EXPECT_FALSE(req.input(0).has_filename());
  EXPECT_TRUE(req.input(0).has_hash_key());
}

// -fdebug-prefix-map should be normalized with -g0
TEST(ExecReqNormalizerTest,
     NormalizeExecReqForCacheKeyWithDebugPrefixMapWithFlagG0) {
  devtools_goma::ExecReq req;
  const std::vector<string> kTestOptions{
      "Xclang", "B", "I", "gcc-toolchain", "-sysroot", "resource-dir"};

  ASSERT_TRUE(TextFormat::ParseFromString(kExecReqToNormalize, &req));
  req.add_arg("-g0");
  req.add_arg("-fdebug-prefix-map=/tmp/src=/ts");
  ASSERT_TRUE(devtools_goma::VerifyExecReq(req));
  devtools_goma::NormalizeExecReqForCacheKey(
      0, true, false, kTestOptions, std::map<string, string>(), &req);
  EXPECT_EQ(1, req.command_spec().system_include_path_size());
  EXPECT_EQ("../../third_party/include",
            req.command_spec().system_include_path(0));
  EXPECT_EQ(1, req.command_spec().cxx_system_include_path_size());
  EXPECT_EQ("../../third_party/include",
            req.command_spec().cxx_system_include_path(0));
  // +2 because "-g0" and "-fdebug-prefix-map" are added.
  EXPECT_EQ(kExecReqToNormalizeArgSize + 2, req.arg_size());
  EXPECT_EQ("../../third_party/include", req.arg(2));
  EXPECT_EQ("../../third_party/lib/libFindBadConstructs.so", req.arg(4));
  EXPECT_EQ("-fdebug-prefix-map=", req.arg(kExecReqToNormalizeArgSize + 1));
  EXPECT_TRUE(req.cwd().empty());
  EXPECT_TRUE(req.env().empty());
  EXPECT_EQ(1, req.input_size());
  EXPECT_FALSE(req.input(0).has_filename());
  EXPECT_TRUE(req.input(0).has_hash_key());
}

// Not normalize args but normalize -fdebug-prefix-map.
TEST(ExecReqNormalizerTest,
     NormalizeExecReqForCacheKeyWithDebugPrefixMapWithRelativeArgs) {
  devtools_goma::ExecReq req;
  const std::vector<string> kTestOptions{
      "Xclang", "B", "I", "gcc-toolchain", "-sysroot", "resource-dir"};

  ASSERT_TRUE(TextFormat::ParseFromString(
      kExecReqToNormalizeRelativeArgs, &req));
  req.add_arg("-fdebug-prefix-map=/tmp/src=/ts");
  ASSERT_TRUE(devtools_goma::VerifyExecReq(req));
  devtools_goma::NormalizeExecReqForCacheKey(
      0, true, false, kTestOptions, std::map<string, string>(), &req);
  EXPECT_EQ(1, req.command_spec().system_include_path_size());
  EXPECT_EQ("../../third_party/include",
            req.command_spec().system_include_path(0));
  EXPECT_EQ(1, req.command_spec().cxx_system_include_path_size());
  EXPECT_EQ("../../third_party/include",
            req.command_spec().cxx_system_include_path(0));
  // +1 because "-fdebug-prefix-map" are added.
  EXPECT_EQ(kExecReqToNormalizeArgSize + 1, req.arg_size());
  EXPECT_EQ("../../third_party/include", req.arg(2));
  EXPECT_EQ("../../third_party/lib/libFindBadConstructs.so", req.arg(4));
  EXPECT_EQ("-fdebug-prefix-map=", req.arg(kExecReqToNormalizeArgSize));
  EXPECT_TRUE(req.cwd().empty());
  EXPECT_TRUE(req.env().empty());
  EXPECT_EQ(1, req.input_size());
  EXPECT_FALSE(req.input(0).has_filename());
  EXPECT_TRUE(req.input(0).has_hash_key());
}

// -MD
TEST(ExecReqNormalizerTest, NormalizeExecReqForCacheKeyWithMD) {
  devtools_goma::ExecReq req;
  const std::vector<string> kTestOptions{
      "Xclang", "B", "I", "gcc-toolchain", "-sysroot", "resource-dir"};

  ASSERT_TRUE(TextFormat::ParseFromString(kExecReqToNormalize, &req));
  req.add_arg("-MD");
  ASSERT_TRUE(devtools_goma::VerifyExecReq(req));
  devtools_goma::NormalizeExecReqForCacheKey(
      0, true, false, kTestOptions, std::map<string, string>(), &req);
  EXPECT_EQ(1, req.command_spec().system_include_path_size());
  EXPECT_EQ("/tmp/src/third_party/include",
            req.command_spec().system_include_path(0));
  EXPECT_EQ(1, req.command_spec().cxx_system_include_path_size());
  EXPECT_EQ("/tmp/src/third_party/include",
            req.command_spec().cxx_system_include_path(0));
  // +1 because "-MD" is added.
  EXPECT_EQ(kExecReqToNormalizeArgSize + 1, req.arg_size());
  EXPECT_EQ("/tmp/src/third_party/include", req.arg(2));
  EXPECT_EQ("../../third_party/lib/libFindBadConstructs.so", req.arg(4));
  EXPECT_TRUE(req.cwd().empty());
  EXPECT_TRUE(req.env().empty());
  EXPECT_EQ(1, req.input_size());
  EXPECT_FALSE(req.input(0).has_filename());
  EXPECT_TRUE(req.input(0).has_hash_key());
}

// -M && -MF
TEST(ExecReqNormalizerTest, NormalizeExecReqForCacheKeyWithMMF) {
  devtools_goma::ExecReq req;
  const std::vector<string> kTestOptions{
      "Xclang", "B", "I", "gcc-toolchain", "-sysroot", "resource-dir"};

  ASSERT_TRUE(TextFormat::ParseFromString(kExecReqToNormalize, &req));
  req.add_arg("-M");
  req.add_arg("-MF");
  req.add_arg("hello.d");
  ASSERT_TRUE(devtools_goma::VerifyExecReq(req));
  devtools_goma::NormalizeExecReqForCacheKey(
      0, true, false, kTestOptions, std::map<string, string>(), &req);
  EXPECT_EQ(1, req.command_spec().system_include_path_size());
  EXPECT_EQ("/tmp/src/third_party/include",
            req.command_spec().system_include_path(0));
  EXPECT_EQ(1, req.command_spec().cxx_system_include_path_size());
  EXPECT_EQ("/tmp/src/third_party/include",
            req.command_spec().cxx_system_include_path(0));
  // +3 because "-M", "-MF", and filename are added.
  EXPECT_EQ(kExecReqToNormalizeArgSize + 3, req.arg_size());
  EXPECT_EQ("/tmp/src/third_party/include", req.arg(2));
  EXPECT_EQ("../../third_party/lib/libFindBadConstructs.so", req.arg(4));
  EXPECT_TRUE(req.cwd().empty());
  EXPECT_TRUE(req.env().empty());
  EXPECT_EQ(1, req.input_size());
  EXPECT_FALSE(req.input(0).has_filename());
  EXPECT_TRUE(req.input(0).has_hash_key());
}

// -M
TEST(ExecReqNormalizerTest, NormalizeExecReqForCacheKeyWithM) {
  devtools_goma::ExecReq req;
  const std::vector<string> kTestOptions{
      "Xclang", "B", "I", "gcc-toolchain", "-sysroot", "resource-dir"};

  ASSERT_TRUE(TextFormat::ParseFromString(kExecReqToNormalize, &req));
  req.add_arg("-M");
  ASSERT_TRUE(devtools_goma::VerifyExecReq(req));
  devtools_goma::NormalizeExecReqForCacheKey(
      0, true, false, kTestOptions, std::map<string, string>(), &req);
  EXPECT_EQ(1, req.command_spec().system_include_path_size());
  EXPECT_EQ("/tmp/src/third_party/include",
            req.command_spec().system_include_path(0));
  EXPECT_EQ(1, req.command_spec().cxx_system_include_path_size());
  EXPECT_EQ("/tmp/src/third_party/include",
            req.command_spec().cxx_system_include_path(0));
  // +1 because "-M", is added.
  EXPECT_EQ(kExecReqToNormalizeArgSize + 1, req.arg_size());
  EXPECT_EQ("/tmp/src/third_party/include", req.arg(2));
  EXPECT_EQ("../../third_party/lib/libFindBadConstructs.so", req.arg(4));
  EXPECT_TRUE(req.cwd().empty());
  EXPECT_TRUE(req.env().empty());
  EXPECT_EQ(1, req.input_size());
  EXPECT_FALSE(req.input(0).has_filename());
  EXPECT_TRUE(req.input(0).has_hash_key());
}

// When -MM or -MMD is specified, we can convert system paths to
// relative paths.
// -MMD
TEST(ExecReqNormalizerTest, NormalizeExecReqForCacheKeyWithMMD) {
  devtools_goma::ExecReq req;
  const std::vector<string> kTestOptions{
      "Xclang", "B", "I", "gcc-toolchain", "-sysroot", "resource-dir"};

  ASSERT_TRUE(TextFormat::ParseFromString(kExecReqToNormalize, &req));
  req.add_arg("-MMD");
  ASSERT_TRUE(devtools_goma::VerifyExecReq(req));
  devtools_goma::NormalizeExecReqForCacheKey(
      0, true, false, kTestOptions, std::map<string, string>(), &req);
  EXPECT_EQ(1, req.command_spec().system_include_path_size());
  EXPECT_EQ("../../third_party/include",
            req.command_spec().system_include_path(0));
  EXPECT_EQ(1, req.command_spec().cxx_system_include_path_size());
  EXPECT_EQ("../../third_party/include",
            req.command_spec().cxx_system_include_path(0));
  // +1 because "-MMD" is added.
  EXPECT_EQ(kExecReqToNormalizeArgSize + 1, req.arg_size());
  EXPECT_EQ("../../third_party/include", req.arg(2));
  EXPECT_EQ("../../third_party/lib/libFindBadConstructs.so", req.arg(4));
  EXPECT_TRUE(req.cwd().empty());
  EXPECT_TRUE(req.env().empty());
  EXPECT_EQ(1, req.input_size());
  EXPECT_FALSE(req.input(0).has_filename());
  EXPECT_TRUE(req.input(0).has_hash_key());
}

// -MM + -MF
TEST(ExecReqNormalizerTest, NormalizeExecReqForCacheKeyWithMMMF) {
  devtools_goma::ExecReq req;
  const std::vector<string> kTestOptions{
      "Xclang", "B", "I", "gcc-toolchain", "-sysroot", "resource-dir"};

  ASSERT_TRUE(TextFormat::ParseFromString(kExecReqToNormalize, &req));
  req.add_arg("-MM");
  req.add_arg("-MF");
  req.add_arg("hello.d");
  ASSERT_TRUE(devtools_goma::VerifyExecReq(req));
  devtools_goma::NormalizeExecReqForCacheKey(
      0, true, false, kTestOptions, std::map<string, string>(), &req);
  EXPECT_EQ(1, req.command_spec().system_include_path_size());
  EXPECT_EQ("../../third_party/include",
            req.command_spec().system_include_path(0));
  EXPECT_EQ(1, req.command_spec().cxx_system_include_path_size());
  EXPECT_EQ("../../third_party/include",
            req.command_spec().cxx_system_include_path(0));
  // +3 because "-MM", "-MF", and filename are added.
  EXPECT_EQ(kExecReqToNormalizeArgSize + 3, req.arg_size());
  EXPECT_EQ("../../third_party/include", req.arg(2));
  EXPECT_EQ("../../third_party/lib/libFindBadConstructs.so", req.arg(4));
  EXPECT_TRUE(req.cwd().empty());
  EXPECT_TRUE(req.env().empty());
  EXPECT_EQ(1, req.input_size());
  EXPECT_FALSE(req.input(0).has_filename());
  EXPECT_TRUE(req.input(0).has_hash_key());
}

// -MM
TEST(ExecReqNormalizerTest, NormalizeExecReqForCacheKeyWithMM) {
  devtools_goma::ExecReq req;
  const std::vector<string> kTestOptions{
      "Xclang", "B", "I", "gcc-toolchain", "-sysroot", "resource-dir"};

  ASSERT_TRUE(TextFormat::ParseFromString(kExecReqToNormalize, &req));
  req.add_arg("-MM");
  ASSERT_TRUE(devtools_goma::VerifyExecReq(req));
  devtools_goma::NormalizeExecReqForCacheKey(
      0, true, false, kTestOptions, std::map<string, string>(), &req);
  EXPECT_EQ(1, req.command_spec().system_include_path_size());
  EXPECT_EQ("../../third_party/include",
            req.command_spec().system_include_path(0));
  EXPECT_EQ(1, req.command_spec().cxx_system_include_path_size());
  EXPECT_EQ("../../third_party/include",
            req.command_spec().cxx_system_include_path(0));
  // +1 because "-MM", is added.
  EXPECT_EQ(kExecReqToNormalizeArgSize + 1, req.arg_size());
  EXPECT_EQ("../../third_party/include", req.arg(2));
  EXPECT_EQ("../../third_party/lib/libFindBadConstructs.so", req.arg(4));
  EXPECT_TRUE(req.cwd().empty());
  EXPECT_TRUE(req.env().empty());
  EXPECT_EQ(1, req.input_size());
  EXPECT_FALSE(req.input(0).has_filename());
  EXPECT_TRUE(req.input(0).has_hash_key());
}

// -MF only
TEST(ExecReqNormalizerTest, NormalizeExecReqForCacheKeyWithMF) {
  devtools_goma::ExecReq req;
  const std::vector<string> kTestOptions{
      "Xclang", "B", "I", "gcc-toolchain", "-sysroot", "resource-dir"};

  ASSERT_TRUE(TextFormat::ParseFromString(kExecReqToNormalize, &req));
  req.add_arg("-MF");
  req.add_arg("hello.d");
  ASSERT_TRUE(devtools_goma::VerifyExecReq(req));
  devtools_goma::NormalizeExecReqForCacheKey(
      0, true, false, kTestOptions, std::map<string, string>(), &req);
  EXPECT_EQ(1, req.command_spec().system_include_path_size());
  EXPECT_EQ("../../third_party/include",
            req.command_spec().system_include_path(0));
  EXPECT_EQ(1, req.command_spec().cxx_system_include_path_size());
  EXPECT_EQ("../../third_party/include",
            req.command_spec().cxx_system_include_path(0));
  // +2 because "-MF", and filename are added.
  EXPECT_EQ(kExecReqToNormalizeArgSize + 2, req.arg_size());
  EXPECT_EQ("../../third_party/include", req.arg(2));
  EXPECT_EQ("../../third_party/lib/libFindBadConstructs.so", req.arg(4));
  EXPECT_TRUE(req.cwd().empty());
  EXPECT_TRUE(req.env().empty());
  EXPECT_EQ(1, req.input_size());
  EXPECT_FALSE(req.input(0).has_filename());
  EXPECT_TRUE(req.input(0).has_hash_key());
}

// If both -MD and -MMD are speicified, -MMD won't be used,
// regardless of the commandline order.
// -MD & -MMD
TEST(ExecReqNormalizerTest, NormalizeExecReqForCacheKeyWithMDMMD) {
  devtools_goma::ExecReq req;
  const std::vector<string> kTestOptions{
      "Xclang", "B", "I", "gcc-toolchain", "-sysroot", "resource-dir"};

  ASSERT_TRUE(TextFormat::ParseFromString(kExecReqToNormalize, &req));
  req.add_arg("-MD");
  req.add_arg("-MMD");
  ASSERT_TRUE(devtools_goma::VerifyExecReq(req));
  devtools_goma::NormalizeExecReqForCacheKey(
      0, true, false, kTestOptions, std::map<string, string>(), &req);
  EXPECT_EQ(1, req.command_spec().system_include_path_size());
  EXPECT_EQ("/tmp/src/third_party/include",
            req.command_spec().system_include_path(0));
  EXPECT_EQ(1, req.command_spec().cxx_system_include_path_size());
  EXPECT_EQ("/tmp/src/third_party/include",
            req.command_spec().cxx_system_include_path(0));
  // +2 because "-MD" and "-MMD" are added.
  EXPECT_EQ(kExecReqToNormalizeArgSize + 2, req.arg_size());
  EXPECT_EQ("/tmp/src/third_party/include", req.arg(2));
  EXPECT_EQ("../../third_party/lib/libFindBadConstructs.so", req.arg(4));
  EXPECT_TRUE(req.cwd().empty());
  EXPECT_TRUE(req.env().empty());
  EXPECT_EQ(1, req.input_size());
  EXPECT_FALSE(req.input(0).has_filename());
  EXPECT_TRUE(req.input(0).has_hash_key());
}

// -MMD & -MD (inverted order)
TEST(ExecReqNormalizerTest, NormalizeExecReqForCacheKeyWithMMDMD) {
  devtools_goma::ExecReq req;
  const std::vector<string> kTestOptions{
      "Xclang", "B", "I", "gcc-toolchain", "-sysroot", "resource-dir"};

  ASSERT_TRUE(TextFormat::ParseFromString(kExecReqToNormalize, &req));
  req.add_arg("-MMD");
  req.add_arg("-MD");
  ASSERT_TRUE(devtools_goma::VerifyExecReq(req));
  devtools_goma::NormalizeExecReqForCacheKey(
      0, true, false, kTestOptions, std::map<string, string>(), &req);
  EXPECT_EQ(1, req.command_spec().system_include_path_size());
  EXPECT_EQ("/tmp/src/third_party/include",
            req.command_spec().system_include_path(0));
  EXPECT_EQ(1, req.command_spec().cxx_system_include_path_size());
  EXPECT_EQ("/tmp/src/third_party/include",
            req.command_spec().cxx_system_include_path(0));
  // +2 because "-MD" and "-MMD" are added.
  EXPECT_EQ(kExecReqToNormalizeArgSize + 2, req.arg_size());
  EXPECT_EQ("/tmp/src/third_party/include", req.arg(2));
  EXPECT_EQ("../../third_party/lib/libFindBadConstructs.so", req.arg(4));
  EXPECT_TRUE(req.cwd().empty());
  EXPECT_TRUE(req.env().empty());
  EXPECT_EQ(1, req.input_size());
  EXPECT_FALSE(req.input(0).has_filename());
  EXPECT_TRUE(req.input(0).has_hash_key());
}

// -MMD & -MD (with gcc)
// -MD should be ignored if -MMD exists.
TEST(ExecReqNormalizerTest, NormalizeExecReqForCacheKeyWithMMDMDGCC) {
  devtools_goma::ExecReq req;
  const std::vector<string> kTestOptions{
      "Xclang", "B", "I", "gcc-toolchain", "-sysroot", "resource-dir"};

  ASSERT_TRUE(TextFormat::ParseFromString(
      kExecReqToNormalizeGcc, &req));
  req.add_arg("-MMD");
  req.add_arg("-MD");
  ASSERT_TRUE(devtools_goma::VerifyExecReq(req));
  devtools_goma::NormalizeExecReqForCacheKey(
      0, true, false, kTestOptions, std::map<string, string>(), &req);
  EXPECT_EQ(1, req.command_spec().system_include_path_size());
  EXPECT_EQ("../../third_party/include",
            req.command_spec().system_include_path(0));
  EXPECT_EQ(1, req.command_spec().cxx_system_include_path_size());
  EXPECT_EQ("../../third_party/include",
            req.command_spec().cxx_system_include_path(0));
  // +2 because "-MD" and "-MMD" are added.
  EXPECT_EQ(kExecReqToNormalizeGccArgSize + 2, req.arg_size());
  EXPECT_EQ("../../third_party/include", req.arg(2));
  EXPECT_TRUE(req.cwd().empty());
  EXPECT_TRUE(req.env().empty());
  EXPECT_EQ(1, req.input_size());
  EXPECT_FALSE(req.input(0).has_filename());
  EXPECT_TRUE(req.input(0).has_hash_key());
}

// link.
TEST(ExecReqNormalizerTest, NormalizeExecReqForCacheKeyForLink) {
  devtools_goma::ExecReq req;
  const std::vector<string> kTestOptions{
      "Xclang", "B", "I", "gcc-toolchain", "-sysroot", "resource-dir"};

  ASSERT_TRUE(TextFormat::ParseFromString(
      kExecReqToNormalizeLink, &req));
  ASSERT_TRUE(devtools_goma::VerifyExecReq(req));
  devtools_goma::NormalizeExecReqForCacheKey(
      0, true, true, kTestOptions, std::map<string, string>(), &req);
  EXPECT_EQ(1, req.command_spec().system_include_path_size());
  EXPECT_EQ("/tmp/src/third_party/include",
            req.command_spec().system_include_path(0));
  EXPECT_EQ(1, req.command_spec().cxx_system_include_path_size());
  EXPECT_EQ("/tmp/src/third_party/include",
            req.command_spec().cxx_system_include_path(0));
  EXPECT_EQ(11, req.arg_size());
  EXPECT_EQ("/tmp/src/third_party/include", req.arg(2));
  EXPECT_EQ("/tmp/src/third_party/lib/libFindBadConstructs.so", req.arg(6));
  EXPECT_EQ("-B/tmp/src/out/Release/bin", req.arg(7));
  EXPECT_EQ("--sysroot=/tmp/src/build/linux/sysroot", req.arg(8));
  EXPECT_EQ("-resource-dir=/tmp/src/third_party/clang", req.arg(9));
  EXPECT_FALSE(req.cwd().empty());
  EXPECT_FALSE(req.env().empty());
  EXPECT_EQ("PWD=/tmp/src/out/Release", req.env(0));
  EXPECT_EQ(1, req.input_size());
  EXPECT_TRUE(req.input(0).has_filename());
  EXPECT_TRUE(req.input(0).has_hash_key());
}

// cl.exe
TEST(ExecReqNormalizerTest, NormalizeExecReqForCacheKeyForClExe) {
  devtools_goma::ExecReq req;
  const std::vector<string> kTestOptions{
      "Xclang", "B", "I", "gcc-toolchain", "-sysroot", "resource-dir"};

  ASSERT_TRUE(TextFormat::ParseFromString(
      kExecReqToNormalizeWin, &req));
  ASSERT_TRUE(devtools_goma::VerifyExecReq(req));
  devtools_goma::NormalizeExecReqForCacheKey(
      0, true, false, kTestOptions, std::map<string, string>(), &req);
  EXPECT_EQ(1, req.command_spec().system_include_path_size());
  const string expected_include_path(
      "c:\\Program Files (x86)\\Microsoft Visual Studio 9.0\\VC\\INCLUDE");
  EXPECT_EQ(expected_include_path,
            req.command_spec().system_include_path(0));
  EXPECT_EQ(1, req.command_spec().cxx_system_include_path_size());
  EXPECT_EQ(expected_include_path,
            req.command_spec().cxx_system_include_path(0));
  EXPECT_FALSE(req.cwd().empty());
  EXPECT_EQ(1, req.input_size());
  EXPECT_EQ("C:\\src\\goma\\client\\test\\vc\\stdafx.cpp",
            req.input(0).filename());
  EXPECT_TRUE(req.input(0).has_hash_key());
}

// cl.exe with different cwd and /showIncludes option
TEST(ExecReqNormalizerTest, NormalizeExecReqForCacheKeyForClExeCWD) {
  devtools_goma::ExecReq alice_req, bob_req;
  const std::vector<string> kTestOptions{
      "Xclang", "B", "I", "gcc-toolchain", "-sysroot", "resource-dir"};

  ASSERT_TRUE(TextFormat::ParseFromString(
      kExecReqToNormalizeWinAlice, &alice_req));
  ASSERT_TRUE(TextFormat::ParseFromString(
      kExecReqToNormalizeWinBob, &bob_req));

  ASSERT_TRUE(devtools_goma::VerifyExecReq(alice_req));
  ASSERT_TRUE(devtools_goma::VerifyExecReq(bob_req));

  devtools_goma::NormalizeExecReqForCacheKey(
      0, true, false, kTestOptions, std::map<string, string>(), &alice_req);
  devtools_goma::NormalizeExecReqForCacheKey(
      0, true, false, kTestOptions, std::map<string, string>(), &bob_req);

  EXPECT_FALSE(MessageDifferencer::Equals(alice_req, bob_req));

  // check only |cwd| is different.
  ASSERT_TRUE(TextFormat::ParseFromString(
      kExecReqToNormalizeWinAlice, &alice_req));
  ASSERT_TRUE(TextFormat::ParseFromString(
      kExecReqToNormalizeWinBob, &bob_req));

  ASSERT_TRUE(devtools_goma::VerifyExecReq(alice_req));
  ASSERT_TRUE(devtools_goma::VerifyExecReq(bob_req));

  alice_req.clear_cwd();
  bob_req.clear_cwd();

  devtools_goma::NormalizeExecReqForCacheKey(
      0, true, false, kTestOptions, std::map<string, string>(), &alice_req);
  devtools_goma::NormalizeExecReqForCacheKey(
      0, true, false, kTestOptions, std::map<string, string>(), &bob_req);

  MessageDifferencer differencer;
  string difference_reason;
  differencer.ReportDifferencesToString(&difference_reason);
  EXPECT_TRUE(differencer.Compare(alice_req, bob_req)) << difference_reason;
}

// subprogram path cleanup.
TEST(ExecReqNormalizerTest,
     NormalizeExecReqForCacheKeyWithSubprogramPathCleanup) {
  devtools_goma::ExecReq req;
  const std::vector<string> kTestOptions{
      "Xclang", "B", "I", "gcc-toolchain", "-sysroot", "resource-dir"};

  ASSERT_TRUE(TextFormat::ParseFromString(kExecReqToNormalize, &req));
  devtools_goma::SubprogramSpec* s = req.add_subprogram();
  s->set_path("../../third_party/binutils/Linux_x64/Release/bin/as");
  s->set_binary_hash(
      "2f931b1183b807976cb304a66d1b84dcfe5a32f02b45f54c2358e5c43f9183b0");
  s = req.add_subprogram();
  s->set_path("../../third_party/binutils/Linux_x64/Release/bin/strip");
  s->set_binary_hash(
      "4956e195e962c7329c1fd0aee839d5cdbf7bb42bbc19e197be11751da1f3ea3c");
  ASSERT_TRUE(devtools_goma::VerifyExecReq(req));
  devtools_goma::NormalizeExecReqForCacheKey(
      0, false, false, std::vector<string>(), std::map<string, string>(), &req);
  EXPECT_EQ(2, req.subprogram_size());
  EXPECT_EQ("", req.subprogram(0).path());
  EXPECT_EQ("2f931b1183b807976cb304a66d1b84dcfe5a32f02b45f54c2358e5c43f9183b0",
            req.subprogram(0).binary_hash());
  EXPECT_EQ("", req.subprogram(1).path());
  EXPECT_EQ("4956e195e962c7329c1fd0aee839d5cdbf7bb42bbc19e197be11751da1f3ea3c",
            req.subprogram(1).binary_hash());
}

TEST(ExecReqNormalizeTest,
     NormalizeExecReqForCacheKeyWithDebugPrefixMap) {
  devtools_goma::ExecReq req;
  const std::vector<string> kTestOptions{
      "Xclang", "B", "I", "gcc-toolchain", "-sysroot", "resource-dir"};

  // TODO: On Windows, we should try to use Windows path?
  // Currently no one is using debug prefix map on Windows.

  // debug_prefix_map.
  const std::map<string, string> debug_prefix_map = {
    {"/tmp/src", "/ts"},
  };
  ASSERT_TRUE(TextFormat::ParseFromString(kExecReqToNormalize, &req));
  req.add_arg("-g");
  // Above debug_prefix_map variable is applied actually but
  // not to confuse NormalizeExecReqForCacheKey, let me also
  // add "-fdebug-prefix-map" here.
  req.add_arg("-fdebug-prefix-map=/tmp/src=/ts");
  ASSERT_TRUE(devtools_goma::VerifyExecReq(req));
  devtools_goma::NormalizeExecReqForCacheKey(
      0, true, false, kTestOptions, debug_prefix_map, &req);
  EXPECT_EQ(1, req.command_spec().system_include_path_size());
  EXPECT_EQ(file::JoinPath("/ts", "third_party/include"),
            req.command_spec().system_include_path(0));
  EXPECT_EQ(1, req.command_spec().cxx_system_include_path_size());
  EXPECT_EQ(file::JoinPath("/ts", "third_party/include"),
            req.command_spec().cxx_system_include_path(0));
  // +2 because "-g" and "-fdebug-prefix-map" is added.
  EXPECT_EQ(kExecReqToNormalizeArgSize + 2, req.arg_size());
  EXPECT_EQ(file::JoinPath("/ts", "third_party/include"), req.arg(2));
  EXPECT_EQ(file::JoinPath("/ts", "third_party/lib/libFindBadConstructs.so"),
            req.arg(4));
  EXPECT_EQ(file::JoinPath("/ts", "out/Release"), req.cwd());
  EXPECT_EQ(1, req.input_size());
  EXPECT_EQ(file::JoinPath("/ts", "hello.c"), req.input(0).filename());
  EXPECT_TRUE(req.input(0).has_hash_key());
}

// disable debug_prefix_map.
TEST(ExecReqNormalizerTest,
     NormalizeExecReqForCacheKeyWithDisabledDebugPrefixMap) {
  devtools_goma::ExecReq req;
  const std::vector<string> kTestOptions{
      "Xclang", "B", "I", "gcc-toolchain", "-sysroot", "resource-dir"};

  ASSERT_TRUE(TextFormat::ParseFromString(kExecReqToNormalize, &req));
  req.add_arg("-g");
  req.add_arg("-fdebug-prefix-map=/tmp/src=/ts");
  ASSERT_TRUE(devtools_goma::VerifyExecReq(req));
  // Note: passing empty debug_prefix_map means disabling the feature.
  devtools_goma::NormalizeExecReqForCacheKey(
      0, true, false, kTestOptions, std::map<string, string>(), &req);
  EXPECT_EQ(1, req.command_spec().system_include_path_size());
  EXPECT_EQ("/tmp/src/third_party/include",
            req.command_spec().system_include_path(0));
  EXPECT_EQ(1, req.command_spec().cxx_system_include_path_size());
  EXPECT_EQ("/tmp/src/third_party/include",
            req.command_spec().cxx_system_include_path(0));
  // +2 because "-g" and "-fdebug-prefix-map" is added.
  EXPECT_EQ(kExecReqToNormalizeArgSize + 2, req.arg_size());
  EXPECT_EQ("/tmp/src/third_party/include", req.arg(2));
  EXPECT_EQ("/tmp/src/third_party/lib/libFindBadConstructs.so", req.arg(4));
  EXPECT_EQ("/tmp/src/out/Release", req.cwd());
  EXPECT_EQ(1, req.input_size());
  EXPECT_EQ("/tmp/src/hello.c", req.input(0).filename());
  EXPECT_TRUE(req.input(0).has_hash_key());
}

TEST(ExecReqNormalizerTest, NormalizeExecReqShouldNormalizeWithDebugPrefixMap) {
  const std::vector<string> kTestOptions{
      "Xclang", "B", "I", "gcc-toolchain", "-sysroot", "resource-dir"};
  devtools_goma::ExecReq alice_req, bob_req;

  const std::map<string, string> expected_alice_map = {
    {"/home/alice", "/base_dir"},
  };
  const std::map<string, string> expected_bob_map = {
    {"/home/bob", "/base_dir"},
  };
  ASSERT_TRUE(TextFormat::ParseFromString(
      kExecReqToNormalizeDebugPrefixMapAlice, &alice_req));
  ASSERT_TRUE(TextFormat::ParseFromString(
      kExecReqToNormalizeDebugPrefixMapBob, &bob_req));
  ASSERT_TRUE(devtools_goma::VerifyExecReq(alice_req));
  ASSERT_TRUE(devtools_goma::VerifyExecReq(bob_req));

  std::vector<string> alice_args(alice_req.arg().begin(),
                                 alice_req.arg().end());
  std::vector<string> bob_args(bob_req.arg().begin(), bob_req.arg().end());
  devtools_goma::GCCFlags alice_flags(alice_args, alice_req.cwd());
  devtools_goma::GCCFlags bob_flags(bob_args, bob_req.cwd());
  ASSERT_EQ(expected_alice_map, alice_flags.fdebug_prefix_map());
  ASSERT_EQ(expected_bob_map, bob_flags.fdebug_prefix_map());

  devtools_goma::NormalizeExecReqForCacheKey(
      0, true, false, kTestOptions, alice_flags.fdebug_prefix_map(),
      &alice_req);
  devtools_goma::NormalizeExecReqForCacheKey(
      0, true, false, kTestOptions, bob_flags.fdebug_prefix_map(),
      &bob_req);
  string normalized_alice;
  string normalized_bob;
  ASSERT_TRUE(TextFormat::PrintToString(alice_req, &normalized_alice));
  ASSERT_TRUE(TextFormat::PrintToString(bob_req, &normalized_bob));
  EXPECT_EQ(normalized_alice, normalized_bob);
}

TEST(ExecReqNormalizerTest,
     NormalizeExecReqShouldNormalizeWithDebugPrefixMapAndCWD) {
  const std::vector<string> kTestOptions{
      "Xclang", "B", "I", "gcc-toolchain", "-sysroot", "resource-dir"};
  devtools_goma::ExecReq alice_req, bob_req;

  const std::map<string, string> kExpectedMap = {
    {"/proc/self/cwd", ""},
  };

  ASSERT_TRUE(TextFormat::ParseFromString(
      kExecReqToNormalizeDebugPrefixMapAlicePSC, &alice_req));
  ASSERT_TRUE(TextFormat::ParseFromString(
      kExecReqToNormalizeDebugPrefixMapBobPSC, &bob_req));
  ASSERT_TRUE(devtools_goma::VerifyExecReq(alice_req));
  ASSERT_TRUE(devtools_goma::VerifyExecReq(bob_req));

  std::vector<string> alice_args(alice_req.arg().begin(),
                                 alice_req.arg().end());
  std::vector<string> bob_args(bob_req.arg().begin(), bob_req.arg().end());
  devtools_goma::GCCFlags alice_flags(alice_args, alice_req.cwd());
  devtools_goma::GCCFlags bob_flags(bob_args, bob_req.cwd());
  ASSERT_EQ(kExpectedMap, alice_flags.fdebug_prefix_map());
  ASSERT_EQ(kExpectedMap, bob_flags.fdebug_prefix_map());

  ASSERT_EQ(alice_req.env().size(), 1);
  EXPECT_EQ("PWD=/proc/self/cwd", alice_req.env()[0]);

  ASSERT_EQ(bob_req.env().size(), 1);
  EXPECT_EQ("PWD=/proc/self/cwd", bob_req.env()[0]);

  devtools_goma::NormalizeExecReqForCacheKey(
      0, true, false, kTestOptions, alice_flags.fdebug_prefix_map(),
      &alice_req);
  devtools_goma::NormalizeExecReqForCacheKey(
      0, true, false, kTestOptions, bob_flags.fdebug_prefix_map(),
      &bob_req);

  EXPECT_FALSE(MessageDifferencer::Equals(alice_req, bob_req));
}

TEST(ExecReqNormalizerTest,
     NormalizeExecReqShouldNormalizeWith2DebugPrefixMapAndCWD) {
  const std::vector<string> kTestOptions{
      "Xclang", "B", "I", "gcc-toolchain", "-sysroot", "resource-dir"};
  devtools_goma::ExecReq alice_req, bob_req;

  const std::map<string, string> kExpectedMapAlice = {
    {"/proc/self/cwd", ""},
    {"/home/alice/src/", ""}
  };

  const std::map<string, string> kExpectedMapBob = {
    {"/proc/self/cwd", ""},
    {"/home/bob/src/", ""}
  };

  ASSERT_TRUE(TextFormat::ParseFromString(
      kExecReqToNormalize2DebugPrefixMapAlicePSC, &alice_req));
  ASSERT_TRUE(TextFormat::ParseFromString(
      kExecReqToNormalize2DebugPrefixMapBobPSC, &bob_req));
  ASSERT_TRUE(devtools_goma::VerifyExecReq(alice_req));
  ASSERT_TRUE(devtools_goma::VerifyExecReq(bob_req));

  std::vector<string> alice_args(alice_req.arg().begin(),
                                 alice_req.arg().end());
  std::vector<string> bob_args(bob_req.arg().begin(), bob_req.arg().end());
  devtools_goma::GCCFlags alice_flags(alice_args, alice_req.cwd());
  devtools_goma::GCCFlags bob_flags(bob_args, bob_req.cwd());
  ASSERT_EQ(kExpectedMapAlice, alice_flags.fdebug_prefix_map());
  ASSERT_EQ(kExpectedMapBob, bob_flags.fdebug_prefix_map());

  ASSERT_EQ(alice_req.env().size(), 1);
  EXPECT_EQ("PWD=/proc/self/cwd", alice_req.env()[0]);

  ASSERT_EQ(bob_req.env().size(), 1);
  EXPECT_EQ("PWD=/proc/self/cwd", bob_req.env()[0]);

  devtools_goma::NormalizeExecReqForCacheKey(
      0, true, false, kTestOptions, alice_flags.fdebug_prefix_map(),
      &alice_req);
  devtools_goma::NormalizeExecReqForCacheKey(
      0, true, false, kTestOptions, bob_flags.fdebug_prefix_map(),
      &bob_req);

  MessageDifferencer differencer;
  string difference_reason;
  differencer.ReportDifferencesToString(&difference_reason);
  EXPECT_TRUE(differencer.Compare(alice_req, bob_req)) << difference_reason;
}

TEST(ExecReqNormalizerTest,
     NormalizeExecReqShouldNormalizeWith2DebugPrefixMapAndCWDGCC) {
  const std::vector<string> kTestOptions{
    "B", "I", "gcc-toolchain", "-sysroot", "resource-dir"};
  devtools_goma::ExecReq alice_req, bob_req;

  const std::map<string, string> kExpectedMapAlice = {
    {"/proc/self/cwd", ""},
    {"/home/alice/src/", ""}
  };

  const std::map<string, string> kExpectedMapBob = {
    {"/proc/self/cwd", ""},
    {"/home/bob/src/", ""}
  };

  ASSERT_TRUE(TextFormat::ParseFromString(
      kExecReqToNormalize2DebugPrefixMapAlicePSCGCC, &alice_req));
  ASSERT_TRUE(TextFormat::ParseFromString(
      kExecReqToNormalize2DebugPrefixMapBobPSCGCC, &bob_req));
  ASSERT_TRUE(devtools_goma::VerifyExecReq(alice_req));
  ASSERT_TRUE(devtools_goma::VerifyExecReq(bob_req));

  std::vector<string> alice_args(alice_req.arg().begin(),
                                 alice_req.arg().end());
  std::vector<string> bob_args(bob_req.arg().begin(), bob_req.arg().end());
  devtools_goma::GCCFlags alice_flags(alice_args, alice_req.cwd());
  devtools_goma::GCCFlags bob_flags(bob_args, bob_req.cwd());
  ASSERT_EQ(kExpectedMapAlice, alice_flags.fdebug_prefix_map());
  ASSERT_EQ(kExpectedMapBob, bob_flags.fdebug_prefix_map());

  ASSERT_EQ(alice_req.env().size(), 1);
  EXPECT_EQ("PWD=/proc/self/cwd", alice_req.env()[0]);

  ASSERT_EQ(bob_req.env().size(), 1);
  EXPECT_EQ("PWD=/proc/self/cwd", bob_req.env()[0]);

  devtools_goma::NormalizeExecReqForCacheKey(
      0, true, false, kTestOptions, alice_flags.fdebug_prefix_map(),
      &alice_req);
  devtools_goma::NormalizeExecReqForCacheKey(
      0, true, false, kTestOptions, bob_flags.fdebug_prefix_map(),
      &bob_req);

  EXPECT_FALSE(MessageDifferencer::Equals(alice_req, bob_req));
}

TEST(ExecReqNormalizerTest,
     NormalizeExecReqShouldNormalizeWithDebugPrefixMapAndCWDGCC) {
  const std::vector<string> kTestOptions{
    "B", "I", "gcc-toolchain", "-sysroot", "resource-dir"};
  devtools_goma::ExecReq alice_req, bob_req;

  const std::map<string, string> kExpectedMap = {
    {"/proc/self/cwd", ""},
  };

  ASSERT_TRUE(TextFormat::ParseFromString(
      kExecReqToNormalizeDebugPrefixMapAlicePSCGCC, &alice_req));
  ASSERT_TRUE(TextFormat::ParseFromString(
      kExecReqToNormalizeDebugPrefixMapBobPSCGCC, &bob_req));
  ASSERT_TRUE(devtools_goma::VerifyExecReq(alice_req));
  ASSERT_TRUE(devtools_goma::VerifyExecReq(bob_req));

  std::vector<string> alice_args(alice_req.arg().begin(),
                                 alice_req.arg().end());
  std::vector<string> bob_args(bob_req.arg().begin(), bob_req.arg().end());
  devtools_goma::GCCFlags alice_flags(alice_args, alice_req.cwd());
  devtools_goma::GCCFlags bob_flags(bob_args, bob_req.cwd());
  ASSERT_EQ(kExpectedMap, alice_flags.fdebug_prefix_map());
  ASSERT_EQ(kExpectedMap, bob_flags.fdebug_prefix_map());

  ASSERT_EQ(alice_req.env().size(), 1);
  EXPECT_EQ("PWD=/proc/self/cwd", alice_req.env()[0]);

  ASSERT_EQ(bob_req.env().size(), 1);
  EXPECT_EQ("PWD=/proc/self/cwd", bob_req.env()[0]);

  devtools_goma::NormalizeExecReqForCacheKey(
      0, true, false, kTestOptions, alice_flags.fdebug_prefix_map(),
      &alice_req);
  devtools_goma::NormalizeExecReqForCacheKey(
      0, true, false, kTestOptions, bob_flags.fdebug_prefix_map(),
      &bob_req);

  EXPECT_FALSE(MessageDifferencer::Equals(alice_req, bob_req));
}

TEST(ExecReqNormalizerTest,
     NormalizeExecReqShouldNotNormalizeWithDebugPrefixMapAndCWDGCC) {
  const std::vector<string> kTestOptions{
    "B", "I", "gcc-toolchain", "-sysroot", "resource-dir"};
  devtools_goma::ExecReq alice_req, bob_req;

  const std::map<string, string> kExpectedMap = {
    {"/proc/self/cwd", ""},
  };

  ASSERT_TRUE(TextFormat::ParseFromString(
      kExecReqToNoNormalizeDebugPrefixMapAlicePSCGCC, &alice_req));
  ASSERT_TRUE(TextFormat::ParseFromString(
      kExecReqToNoNormalizeDebugPrefixMapBobPSCGCC, &bob_req));
  ASSERT_TRUE(devtools_goma::VerifyExecReq(alice_req));
  ASSERT_TRUE(devtools_goma::VerifyExecReq(bob_req));

  std::vector<string> alice_args(alice_req.arg().begin(),
                                 alice_req.arg().end());
  std::vector<string> bob_args(bob_req.arg().begin(), bob_req.arg().end());
  devtools_goma::GCCFlags alice_flags(alice_args, alice_req.cwd());
  devtools_goma::GCCFlags bob_flags(bob_args, bob_req.cwd());
  ASSERT_EQ(kExpectedMap, alice_flags.fdebug_prefix_map());
  ASSERT_EQ(kExpectedMap, bob_flags.fdebug_prefix_map());

  ASSERT_EQ(alice_req.env().size(), 1);
  EXPECT_EQ("PWD=/proc/self/cwd", alice_req.env()[0]);

  ASSERT_EQ(bob_req.env().size(), 1);
  EXPECT_EQ("PWD=/proc/self/cwd", bob_req.env()[0]);

  devtools_goma::NormalizeExecReqForCacheKey(
      0, true, false, kTestOptions, alice_flags.fdebug_prefix_map(),
      &alice_req);
  devtools_goma::NormalizeExecReqForCacheKey(
      0, true, false, kTestOptions, bob_flags.fdebug_prefix_map(),
      &bob_req);

  EXPECT_FALSE(MessageDifferencer::Equals(alice_req, bob_req));
}

TEST(ExecReqNormalizerTest,
     NormalizeExecReqShouldNotNormalizeWithDebugPrefixMapAndCWDNoPWD) {
  const std::vector<string> kTestOptions{
    "B", "I", "gcc-toolchain", "-sysroot", "resource-dir"};
  devtools_goma::ExecReq alice_req, bob_req;

  const std::map<string, string> kExpectedMap = {
    {"/proc/self/cwd", ""},
  };

  ASSERT_TRUE(TextFormat::ParseFromString(
      kExecReqToNormalizeDebugPrefixMapAlicePSCNoPWD, &alice_req));
  ASSERT_TRUE(TextFormat::ParseFromString(
      kExecReqToNormalizeDebugPrefixMapBobPSCNoPWD, &bob_req));
  ASSERT_TRUE(devtools_goma::VerifyExecReq(alice_req));
  ASSERT_TRUE(devtools_goma::VerifyExecReq(bob_req));

  std::vector<string> alice_args(alice_req.arg().begin(),
                                 alice_req.arg().end());
  std::vector<string> bob_args(bob_req.arg().begin(), bob_req.arg().end());
  devtools_goma::GCCFlags alice_flags(alice_args, alice_req.cwd());
  devtools_goma::GCCFlags bob_flags(bob_args, bob_req.cwd());
  ASSERT_EQ(kExpectedMap, alice_flags.fdebug_prefix_map());
  ASSERT_EQ(kExpectedMap, bob_flags.fdebug_prefix_map());

  ASSERT_EQ(alice_req.env().size(), 1);
  EXPECT_EQ("PWD=/home/alice/src", alice_req.env()[0]);

  ASSERT_EQ(bob_req.env().size(), 1);
  EXPECT_EQ("PWD=/home/bob/src", bob_req.env()[0]);

  devtools_goma::NormalizeExecReqForCacheKey(
      0, true, false, kTestOptions, alice_flags.fdebug_prefix_map(),
      &alice_req);
  devtools_goma::NormalizeExecReqForCacheKey(
      0, true, false, kTestOptions, bob_flags.fdebug_prefix_map(),
      &bob_req);

  EXPECT_FALSE(MessageDifferencer::Equals(alice_req, bob_req));
}

TEST(ExecReqNormalizerTest, NormalizeExecReqInputOrderForCacheKey) {
  devtools_goma::ExecReq req;

  ASSERT_TRUE(TextFormat::ParseFromString(
      kExecReqToNormalizeInputOrder, &req));
  ASSERT_TRUE(devtools_goma::VerifyExecReq(req));
  devtools_goma::NormalizeExecReqForCacheKey(
      0, false, false, std::vector<string>(), std::map<string, string>(), &req);

  EXPECT_EQ("bbbbbbbbbb", req.input(0).hash_key());
  EXPECT_EQ("aaaaaaaaaa", req.input(1).hash_key());
  EXPECT_EQ("cccccccccc", req.input(2).hash_key());
}

TEST(ExecReqNormalizerTest, NormalizeExecReqShouldClearContent) {
  devtools_goma::ExecReq req;

  ASSERT_TRUE(TextFormat::ParseFromString(
      kExecReqToNormalizeContent, &req));
  ASSERT_TRUE(devtools_goma::VerifyExecReq(req));
  ASSERT_EQ(1, req.input_size());
  ASSERT_EQ("dummy_hash_key", req.input(0).hash_key());
  ASSERT_TRUE(req.input(0).has_content());

  devtools_goma::NormalizeExecReqForCacheKey(
      0, false, false, std::vector<string>(), std::map<string, string>(), &req);

  EXPECT_EQ(1, req.input_size());
  EXPECT_EQ("dummy_hash_key", req.input(0).hash_key());
  EXPECT_FALSE(req.input(0).has_content());
}

TEST(ExecReqNormalizerTest,
     NormalizeExecReqForCacheKeyShouldNormalizeWindowsPNaClPath) {
  devtools_goma::ExecReq req;

  ASSERT_TRUE(TextFormat::ParseFromString(
      kExecReqToNormalizeWinPNaCl, &req));
  ASSERT_TRUE(devtools_goma::VerifyExecReq(req));

  devtools_goma::NormalizeExecReqForCacheKey(
      0, true, false, std::vector<string>(), std::map<string, string>(), &req);

  EXPECT_EQ(3, req.command_spec().cxx_system_include_path_size());
  EXPECT_EQ("..\\..\\pnacl_newlib\\bin\\..\\x86_64-nacl\\include\\c++\\v1",
            req.command_spec().cxx_system_include_path(0));
  EXPECT_EQ("..\\..\\pnacl_newlib\\bin\\..\\lib\\clang\\3.7.0\\include",
            req.command_spec().cxx_system_include_path(1));
  EXPECT_EQ("..\\..\\pnacl_newlib\\bin\\..\\x86_64-nacl\\include",
            req.command_spec().cxx_system_include_path(2));
  EXPECT_TRUE(req.cwd().empty());
  EXPECT_EQ(1, req.input_size());
  EXPECT_FALSE(req.input(0).has_filename());
  EXPECT_TRUE(req.input(0).has_hash_key());
}

TEST(ExecReqNormalizerTest,
     NormalizeExecReqForCacheKeyShouldNotNormalizePNaClTranslate) {
  devtools_goma::ExecReq req;

  ASSERT_TRUE(TextFormat::ParseFromString(
      kExecReqToNormalizePNaClTranslate, &req));
  ASSERT_TRUE(devtools_goma::VerifyExecReq(req));

  devtools_goma::NormalizeExecReqForCacheKey(
      0, true, false, std::vector<string>(), std::map<string, string>(), &req);

  EXPECT_EQ(3, req.command_spec().cxx_system_include_path_size());
  EXPECT_EQ("../../pnacl_newlib/bin/../x86_64-nacl/include/c++/v1",
            req.command_spec().cxx_system_include_path(0));
  EXPECT_EQ("../../pnacl_newlib/bin/../lib/clang/3.7.0/include",
            req.command_spec().cxx_system_include_path(1));
  EXPECT_EQ("../../pnacl_newlib/bin/../x86_64-nacl/include",
            req.command_spec().cxx_system_include_path(2));
  EXPECT_EQ("/dummy/out/Default", req.cwd());
  EXPECT_EQ(1, req.input_size());
  EXPECT_FALSE(req.input(0).has_filename());
  EXPECT_TRUE(req.input(0).has_hash_key());
}

TEST(ExecReqNormalizerTest, NormalizeJavac) {
  devtools_goma::ExecReq req;

  ASSERT_TRUE(TextFormat::ParseFromString(kExecReqToNormalizeJavac, &req));
  ASSERT_TRUE(devtools_goma::VerifyExecReq(req));

  // To confirm NormalizeExecReqForCacheKey omit them, let me add path that
  // won't exist in actual compile request.
  req.mutable_command_spec()->add_system_include_path("dummy");
  req.mutable_command_spec()->add_cxx_system_include_path("dummy");

  devtools_goma::NormalizeExecReqForCacheKey(
      0, true, false, std::vector<string>(), std::map<string, string>(), &req);

  EXPECT_EQ(0, req.command_spec().system_include_path_size());
  EXPECT_EQ(0, req.command_spec().cxx_system_include_path_size());

  EXPECT_EQ("", req.cwd());
  EXPECT_EQ(1, req.input_size());
  EXPECT_FALSE(req.input(0).has_filename());
  EXPECT_TRUE(req.input(0).has_hash_key());
}

TEST(ExecReqNormalizerTest, RewritePathWithDebugPrefixMap) {
  const std::map<string, string> empty_map;
  const std::map<string, string> single_rule_map = {
    {"/usr/local", "/debug"},
  };
  const std::map<string, string> value_shows_up_in_key_map = {
    {"/usr/local", "/foo"},
    {"/foo", "/bar"},
  };

  string path;
  path = "";
  EXPECT_FALSE(
      devtools_goma::RewritePathWithDebugPrefixMap(
          single_rule_map, &path));

  path = "/tmp";
  EXPECT_FALSE(
      devtools_goma::RewritePathWithDebugPrefixMap(
          empty_map, &path));

  path = "/usr/local/include/stdio.h";
  EXPECT_TRUE(
      devtools_goma::RewritePathWithDebugPrefixMap(
          single_rule_map, &path));
  EXPECT_EQ(file::JoinPath("/debug", "/include/stdio.h"), path);

  path = "/usr/local/include/stdio.h";
  EXPECT_TRUE(
      devtools_goma::RewritePathWithDebugPrefixMap(
          value_shows_up_in_key_map, &path));
  EXPECT_EQ(file::JoinPath("/foo", "include/stdio.h"), path);

  path = "/foo/local/include/stdio.h";
  EXPECT_TRUE(
      devtools_goma::RewritePathWithDebugPrefixMap(
          value_shows_up_in_key_map, &path));
  EXPECT_EQ(file::JoinPath("/bar", "local/include/stdio.h"), path);
}

TEST(ExecReqNormalizerTest, HasAmbiguityInDebugPrefixMap) {
  EXPECT_FALSE(devtools_goma::HasAmbiguityInDebugPrefixMap(
      std::map<string, string>()));
  EXPECT_FALSE(
      devtools_goma::HasAmbiguityInDebugPrefixMap(
          std::map<string, string>({
            {"/usr/local", "/debug"},
          })));
  EXPECT_TRUE(
      devtools_goma::HasAmbiguityInDebugPrefixMap(
          std::map<string, string>({
                {"/usr/local", "/debug"}, {"/usr", "/debug2"},
          })));
  EXPECT_TRUE(
      devtools_goma::HasAmbiguityInDebugPrefixMap(
          std::map<string, string>({
            {"/usr/lib", "/debug"}, {"/usr/libexec", "/debug2"},
          })));
  EXPECT_FALSE(
      devtools_goma::HasAmbiguityInDebugPrefixMap(
          std::map<string, string>({
            {"/usr/lib", "/debug"}, {"/usr//libexec", "/debug2"},
          })));
  EXPECT_TRUE(
      devtools_goma::HasAmbiguityInDebugPrefixMap(
          std::map<string, string>({
            {"/usr/local", "/debug"}, {"dummy", "dummy2"},
                {"/usr", "/debug2"},
          })));
  EXPECT_TRUE(
      devtools_goma::HasAmbiguityInDebugPrefixMap(
          std::map<string, string>({
            {"lib", "/debug"}, {"dummy", "dummy2"},
                {"lib64", "/debug2"},
          })));
  EXPECT_FALSE(
      devtools_goma::HasAmbiguityInDebugPrefixMap(
          std::map<string, string>({
            {"/home/alice/chromium/src", "."},
          })));
}

TEST(ExecReqNormalizerTest, AlwaysRemoveRequesterInfo) {
  // Test for b/38184335

  const std::vector<string> kTestOptions{
    "B", "I", "gcc-toolchain", "-sysroot", "resource-dir"};

  devtools_goma::ExecReq req;

  ASSERT_TRUE(TextFormat::ParseFromString(
      kExecReqToAmbiguaousDebugPrefixMap, &req));
  ASSERT_TRUE(devtools_goma::VerifyExecReq(req));

  const std::map<string, string> kExpectedMap = {
    {"/home/goma/chromium/src", "."},
  };

  std::vector<string> args(req.arg().begin(), req.arg().end());
  devtools_goma::GCCFlags flags(args, req.cwd());
  ASSERT_EQ(kExpectedMap, flags.fdebug_prefix_map());

  EXPECT_FALSE(
      devtools_goma::HasAmbiguityInDebugPrefixMap(flags.fdebug_prefix_map()));

  EXPECT_TRUE(req.has_requester_info());
  devtools_goma::NormalizeExecReqForCacheKey(
      0, true, false, kTestOptions, flags.fdebug_prefix_map(),
      &req);
  EXPECT_FALSE(req.has_requester_info());
}

TEST(ExecReqNormalizerTest, DropDeveloperDir) {
  devtools_goma::ExecReq req;
  ASSERT_TRUE(TextFormat::ParseFromString(kExecReqToNormalize, &req));
  ASSERT_TRUE(devtools_goma::VerifyExecReq(req));

  req.add_env("DEVELOPER_DIR=/some/where/to/developer_dir");
  bool found_developer_env = false;
  for (const auto& env : req.env()) {
    if (absl::StartsWith(env, "DEVELOPER_DIR=")) {
      found_developer_env = true;
      break;
    }
  }
  ASSERT_TRUE(found_developer_env);

  devtools_goma::NormalizeExecReqForCacheKey(
      0, false, false, std::vector<string>(), std::map<string, string>(), &req);

  found_developer_env = false;
  for (const auto& env : req.env()) {
    if (absl::StartsWith(env, "DEVELOPER_DIR=")) {
      found_developer_env = true;
      break;
    }
  }
  EXPECT_FALSE(found_developer_env);
}
