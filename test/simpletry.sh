#!/bin/bash
#
# Copyright 2010 The Goma Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

#
# Simple test scripts for sanity check. Runs against production
# servers.
#
# Run this like:
#  % GOMA_RPC_EXTRA_PARAMS="?${USERNAME}_$cell" ./test/simpletry.sh out/Debug
# in order to test your personal canary with binaries in out/Debug.
# If the binary directory isn't specified, out/Release will be used.
#
#  % ./test/simpletry.sh -w
# will wait after all tests finished, so you could investigate
# outputs or compiler proxy status page.
#
# % ./test/simpletry.sh -k
# will kill running compiler_proxy before test to make sure compiler_proxy
# is actually invoked for the test only.
# Without -k, it will try own compiler_proxy (isolated with GOMA_* flags)
#
# By default, it will allocate port 8100 (or later)
# You can set port number with -p option.
# % ./test/simpletry.sh -p 8200
#
# with -d dumpfile option, you'll get task.json and task's ExecReq in
# dumpfile (tgz format)
# % ./test/simpletry.sh -d /tmp/simpletry.tgz
#
# If CLANG_PATH is specified, and $CLANG_PATH/clang and $CLANG_PATH/clang++
# exists, it will test with clang and clang++.
# Note that it doesn't support old clang that doesn't support -dumpmachine.
#

test_dir=$(cd $(dirname $0); pwd)
goma_top_dir=${test_dir}/..
tmpdir=$(mktemp -d /tmp/tmp.XXXXXXXX)
chmod 0700 $tmpdir

. $test_dir/gomatest.sh

is_color=0
if tput init && test -t 1; then
  is_color=1
fi
function test_term() {
  test "$is_color" = 1
}
function tput_reset() {
  test_term && tput sgr0
  return 0
}
function echo_title() {
  if test_term; then
    tput bold; tput setaf 4
  fi
  echo "$@"
  tput_reset
}
function echo_bold() {
  test_term && tput bold
  echo "$@"
  tput_reset
}
function echo_ok() {
  if test_term; then
    tput bold; tput setaf 2
  fi
  echo "$@"
  tput_reset
}
function echo_known_fail() {
  if test_term; then
    tput setab 1
  fi
  echo "$@"
  tput_reset
}
function echo_fail() {
  if test_term; then
    tput bold; tput setaf 1
  fi
  echo "$@"
  tput_reset
}
function echo_warn() {
  if test_term; then
   tput bold; tput setaf 5
  fi
  echo "$@"
  tput_reset
}

function at_exit() {
  # cleanup function.
  rm -f a.out a.out2 out.o out2.o out_plain.o has_include.o
  rm -f test/compile_error.o
  rm -f test/compile_error*.out test/compile_error*.err
  rm -f cmd_out cmd_err
  stop_compiler_proxy

  $goma_top_dir/client/diagnose_goma_log.py \
    --show-errors --show-warnings --show-known-warnings-threshold=0 \
    --fail-tasks-threshold=2 \
    || true
  if [ -n "${GLOG_log_dir:-}" ]; then
    echo "Gomacc logs:"
    cat ${GLOG_log_dir}/gomacc.* || true
  fi
  rm -rf $tmpdir
  tput_reset
}

function is_cros_gcc() {
  local compiler=$1

  local version=$($compiler --version)
  case "$version" in
    *_cos_*)
      echo "yes"
      ;;
    *)
      echo "no"
      ;;
  esac
}

# Note: all code in this script is expected to be executed from $goma_top_dir.
cd $goma_top_dir

FLAGS_wait=0
FLAGS_kill=0
FLAGS_port=8100
FLAGS_dump=
while getopts kwp:d: opt; do
 case $opt in
 k) FLAGS_kill=1 ;;
 w) FLAGS_wait=1 ;;
 p) FLAGS_port="$OPTARG";;
 d) FLAGS_dump="$OPTARG";;
 ?) echo "Usage: $0 [-w] [-k] [-p port] [-d tgz] [goma_dir]\n" >&2; exit 1;;
 esac
done
shift $(($OPTIND - 1))

set_goma_dirs "$1"

# Flags for gomacc
export GOMA_STORE_ONLY=true
export GOMA_DUMP=true
export GOMA_RETRY=false
export GOMA_FALLBACK=false
export GOMA_USE_LOCAL=false
export GOMA_START_COMPILER_PROXY=false
export GOMA_STORE_LOCAL_RUN_OUTPUT=true
export GOMA_ENABLE_REMOTE_LINK=true
export GOMA_HERMETIC=error
export GOMA_FALLBACK_INPUT_FILES=""

# Set service account JSON file if exists.
CRED="/creds/service_accounts/service-account-goma-client.json"
if [ -z "$GOMA_SERVICE_ACCOUNT_JSON_FILE" -a -f "$CRED" ]; then
  export GOMA_SERVICE_ACCOUNT_JSON_FILE="$CRED"
fi
if [ -n "$GOMA_SERVICE_ACCOUNT_JSON_FILE" -a \
  ! -f "$GOMA_SERVICE_ACCOUNT_JSON_FILE" ]; then
  echo "GOMA_SERVICE_ACCOUNT_JSON_FILE $GOMA_SERVICE_ACCOUNT_JSON_FILE " \
    "not found." >&2
  unset GOMA_SERVICE_ACCOUNT_JSON_FILE
fi

# on buildslave:/b/build/slave/$builddir/build/client
# api key can be found at /b/build/goma/goma.key
if [ -d "$goma_top_dir/../../../../goma" ]; then
  bot_goma_dir=$(cd "$goma_top_dir/../../../../goma"; pwd)
  GOMA_API_KEY_FILE=${GOMA_API_KEY_FILE:-$bot_goma_dir/goma.key}
fi
if [ -n "$GOMA_SERVICE_ACCOUNT_JSON_FILE" ]; then
  echo "Use GOMA_SERVICE_ACCOUNT_JSON_FILE=$GOMA_SERVICE_ACCOUNT_JSON_FILE"
  unset GOMA_API_KEY_FILE
elif [ -f "$GOMA_API_KEY_FILE" ]; then
  echo "Use GOMA_API_KEY_FILE=$GOMA_API_KEY_FILE"
  export GOMA_API_KEY_FILE
elif [ -n "$GOMA_API_KEY_FILE" ]; then
  echo "GOMA_API_KEY_FILE $GOMA_API_KEY_FILE not found." >&2
  unset GOMA_API_KEY_FILE
fi

if [ "$GOMATEST_USE_RUNNING_COMPILER_PROXY" = ""  ]; then
  # --exec_compiler_proxy is deprecated. Use GOMA_COMPILER_PROXY_BINARY instead.
  if ! [ -x ${GOMA_COMPILER_PROXY_BINARY} ]; then
    echo "compiler_proxy($GOMA_COMPILER_PROXY_BINARY) is not executable" >&2
    exit 1
  fi
  echo "Starting $GOMA_COMPILER_PROXY_BINARY..."

  trap at_exit exit sighup sigpipe
  export GOMA_COMPILER_PROXY_PORT=$FLAGS_port

  if [ "$FLAGS_kill" = 1 ]; then
    echo Kill any remaining compiler proxy
    killall compiler_proxy
  else
    echo "GOMA_TMP_DIR: $tmpdir"
    export GOMA_TMP_DIR=$tmpdir
    export TMPDIR=$tmpdir
    export GLOG_log_dir=$tmpdir
    export GOMA_DEPS_CACHE_FILE=deps_cache
    export GOMA_COMPILER_PROXY_SOCKET_NAME=$tmpdir/goma.ipc
    export GOMA_GOMACC_LOCK_FILENAME=$tmpdir/gomacc.lock
    export GOMA_COMPILER_PROXY_LOCK_FILENAME=$tmpdir/goma_compiler_proxy.lock
    # Test uses SSL by default.
    export GOMA_USE_SSL=true
    export GOMA_STUBBY_PROXY_PORT=443
  fi
  (cd /tmp && ${GOMA_COMPILER_PROXY_BINARY} & )
  update_compiler_proxy_port $(dirname $GOMA_COMPILER_PROXY_BINARY) 10
  watch_healthz localhost ${GOMA_COMPILER_PROXY_PORT} /healthz \
     ${GOMA_COMPILER_PROXY_BINARY}
fi

if [ "$CLANG_PATH" = "" ]; then
  clang_path="$goma_top_dir/third_party/llvm-build/Release+Asserts/bin"
  if [ -d "$clang_path" ]; then
     if "$clang_path/clang" -v; then
       CLANG_PATH="$clang_path"
     else
       echo "clang is not runnable, disable clang test" 1>&2
     fi
  fi
fi

if [ -n "${GLOG_log_dir:-}" ]; then
  echo "removing gomacc logs."
  rm -f "${GLOG_log_dir}/gomacc.*"
fi

# if build env doesn't not use hermetic gcc,
# set HERMETIC_GCC=FAIL_ for workaround.
HERMETIC_GCC=

DEFAULT_CC=gcc
DEFAULT_CXX=g++
if [ "$(uname)" = "Darwin" ]; then
  # recent macosx uses llvm-gcc as gcc, but goma doesn't support it.
  # test with chromium clang by default.
  DEFAULT_CC=clang
  DEFAULT_CXX=clang++
  if [ "$GOMATEST_USE_SYSTEM_CLANG" = "" ]; then
    PATH=$CLANG_PATH:$PATH
    GOMATEST_USE_CHROMIUM_CLANG=1
  fi
  # Should set SDKROOT if we use non system clang.
  export SDKROOT="$("$goma_top_dir"/build/mac/find_sdk.py \
    --print_sdk_path 10.7 | head -1)"
fi

CC=${CC:-$DEFAULT_CC}
CXX=${CXX:-$DEFAULT_CXX}

LOCAL_CC=$(command -v ${CC})
LOCAL_CXX=$(command -v ${CXX})
LOCAL_CXX_DIR=$(dirname ${LOCAL_CXX})
GOMA_CC=${goma_bin_dir}/${CC}
GOMA_CXX=${goma_bin_dir}/${CXX}
GOMACC=$goma_bin_dir/gomacc

# Build determinism is broken on ChromeOS gcc, and since ChromeOS uses
# clang as a default compiler (b/31105358), I do not think we need to
# guarantee build determinism for it.  (b/64499036)
if [[ "$CC" =~ ^g(cc|\+\+)$ && "$(is_cros_gcc $CC)" = "yes" ]]; then
  HERMETIC_GCC="FAIL_"
fi

echo_title "CC=${CC} CXX=${CXX}"
echo_title "LOCAL CC=${LOCAL_CC} CXX=${LOCAL_CXX}"
echo_title "GOMA CC=${GOMA_CC} CXX=${GOMA_CXX}"

${GOMACC} --goma-verify-command ${LOCAL_CC} -v
TASK_ID=1

# keep the list of failed tests in an array
FAIL=()
KNOWN_FAIL=()

function fail() {
  local testname="$1"
  case "$testname" in
  FAIL_*)
      echo_known_fail "FAIL"
      KNOWN_FAIL+=($testname);;
  *)
      echo_fail "FAIL"
      FAIL+=($testname);;
  esac
}

function ok() {
  echo_ok "OK"
}

function assert_success() {
  local cmd="$1"
  if eval $cmd; then
    return
  else
    echo_fail "FAIL in $cmd"
    exit 1
  fi
}

function dump_request() {
  local cmd="$1"
  if [ "$FLAGS_dump" = "" ]; then
    return
  fi
  set -- $cmd
  cmd=$1
  case "$cmd" in
  "$GOMA_CC"|"$GOMA_CXX"|"$GOMACC")
    echo "[dump:$TASK_ID]"
    httpfetch 127.0.0.1 "$GOMA_COMPILER_PROXY_PORT" \
     "/api/taskz?id=$TASK_ID&dump=req" post > /dev/null
    if [ -d $GOMA_TMP_DIR/task_request_$TASK_ID ]; then
      httpfetch 127.0.0.1 "$GOMA_COMPILER_PROXY_PORT" \
       "/api/taskz?id=$TASK_ID" post \
         > $GOMA_TMP_DIR/task_request_$TASK_ID/task.json
    fi
    TASK_ID=$((TASK_ID+1))
    ;;
   *)
    echo "[nodump]";;
  esac
}

function expect_success() {
  local testname="$1"
  local cmd="$2"
  echo_bold -n "TEST: "
  echo -n "${testname}..."
  if eval $cmd >$tmpdir/cmd_out 2>$tmpdir/cmd_err; then
    ok
  else
    fail $testname
    echo_bold "cmd: $cmd"
    cat $tmpdir/cmd_out
    cat $tmpdir/cmd_err
  fi
  dump_request "$cmd"
  rm -f cmd_out cmd_err
}

function expect_failure() {
  local testname="$1"
  local cmd="$2"
  echo_bold -n "TEST: "
  echo -n "${testname}..."
  if eval $cmd >cmd_out 2>cmd_err; then
    fail $testname
    echo_bold "cmd: $cmd"
    cat cmd_out
    cat cmd_err
  else
    ok
  fi
  dump_request "$cmd"
  rm -f cmd_out cmd_err
}

function objcmp() {
 local want=$1
 local got=$2
 if command -v readelf > /dev/null 2>&1; then
    readelf --headers $want > $want.elf
    readelf --headers $got > $got.elf
    diff -u $want.elf $got.elf
    rm -f $want.elf $got.elf
 fi
 cmp $want $got
}

expect_success "${CC}_v" "${GOMA_CC} -v"
# test $CC
rm -f out_plain.o
# build a control binary to test against.
assert_success "${LOCAL_CC} test/hello.c -c -o out_plain.o"
rm -f out.o
expect_success "${CC}_hello" "${GOMA_CC} test/hello.c -c -o out.o"
expect_success "${HERMETIC_GCC}${CC}_hello.o" "objcmp out_plain.o out.o"
rm -f a.out
expect_success "${CC}_hello_run" \
     "${LOCAL_CC} out.o -o a.out && test \"\$(./a.out)\" = \"Hello world\""

GOMA_FALLBACK=true
expect_success "${CC}_hello_fallback" "${GOMA_CC} test/hello.c -c -o out.o"
GOMA_USE_LOCAL=true
expect_success "${CC}_hello_fallback_use_local" \
    "${GOMA_CC} test/hello.c -c -o out.o"
GOMA_FALLBACK=false
expect_success "${CC}_hello_use_local" "${GOMA_CC} test/hello.c -c -o out.o"
GOMA_USE_LOCAL=false

GOMA_FALLBACK_INPUT_FILES="test/hello.c"
curl -s http://localhost:$GOMA_COMPILER_PROXY_PORT/statz | grep fallback > stat_before.txt
expect_success "${CC}_hello_enforce_fallback" \
  "${GOMA_CC} test/hello.c -c -o out.o"
curl -s http://localhost:$GOMA_COMPILER_PROXY_PORT/statz | grep fallback > stat_after.txt
expect_failure "check_fallback" "diff -u stat_before.txt stat_after.txt"
GOMA_FALLBACK_INPUT_FILES=""

rm -f a.out2
expect_success "FAIL_${CC}_hello_remote_link" \
     "${GOMA_CC} out.o -o a.out2 && test \"\$(./a.out2)\" = \"Hello world\""

rm -f out_plain.o
assert_success "${LOCAL_CC} -std=c99 test/hello.c -c -o out_plain.o"
expect_success "${CC}_stdc99_hello" \
    "${GOMA_CC} -std=c99 test/hello.c -c -o out.o"
expect_success "${HERMETIC_GCC}${CC}_stdc99_hello.o" "objcmp out_plain.o out.o"

rm -f out.o out_plain.o
assert_success "${LOCAL_CC} -m32 test/hello.c -c -o out_plain.o"
expect_success "${CC}_m32" "${GOMA_CC} -m32 test/hello.c -c -o out.o"
expect_success "${HERMETIC_GCC}${CC}_m32_out.o" "objcmp out_plain.o out.o"

# test $CXX
rm -f out_plain.o out.o out2.o
# build a control binary to test against.
assert_success "${LOCAL_CXX} test/oneinclude.cc -c -o out_plain.o"

expect_success "${CXX}_oneinclude" \
    "${GOMA_CXX} test/oneinclude.cc -c -o out.o"
expect_success "${HERMETIC_GCC}${CXX}_oneinclude.o" \
    "objcmp out_plain.o out.o"
expect_success "${CXX}_oneinclude_run" \
     "${LOCAL_CXX} out.o -o a.out && test \"\$(./a.out)\" = \"Hello world\""
rm -f a.out2
expect_success "FAIL_${CXX}_oneinclude_remote_link" \
     "${GOMA_CXX} out.o -o a.out2 && test \"\$(./a.out2)\" = \"Hello world\""

rm -f out.o
expect_success "gomacc_${CXX}" \
     "${GOMACC} $CXX test/oneinclude.cc -c -o out.o"
expect_success "${HERMETIC_GCC}gomacc_${CXX}_oneinclude.o" \
    "objcmp out_plain.o out.o"
rm -f out.o
expect_success "gomacc_local_${CXX}" \
     "${GOMACC} $LOCAL_CXX test/oneinclude.cc -c -o out.o"
expect_success "${HERMETIC_GCC}gomacc_local_${CXX}_oneinclude.o" \
    "objcmp out_plain.o out.o"
rm -f out.o out_plain.o
CURRENT_DIR_BACKUP=$PWD
cd $LOCAL_CXX_DIR
assert_success "${LOCAL_CXX} $CURRENT_DIR_BACKUP/test/oneinclude.cc \
  -c -o $CURRENT_DIR_BACKUP/out_plain.o"
expect_success "gomacc_relative_path_${CXX}" \
     "${GOMACC} ./${CXX} $CURRENT_DIR_BACKUP/test/oneinclude.cc \
     -c -o $CURRENT_DIR_BACKUP/out.o"
expect_success "${HERMETIC_GCC}gomacc_relative_path_${CXX}_oneinclude.o" \
    "objcmp ${CURRENT_DIR_BACKUP}/out_plain.o ${CURRENT_DIR_BACKUP}/out.o"
cd $CURRENT_DIR_BACKUP

rm -f out2.o out_plain.o
assert_success "${LOCAL_CXX} -xc++ - -c -o out_plain.o < test/oneinclude.cc"
expect_success "${CXX}_oneinclude_from_stdin" \
     "${GOMA_CXX} -xc++ - -c -o out2.o < test/oneinclude.cc"
expect_success "${HERMETIC_GCC}${CXX}_oneinclude.o_from_stdin" \
     "objcmp out_plain.o out2.o"


# oneinclude2
rm -f out.o
# - no precompiled header
expect_success "${CXX}_oneinclude2" \
   "${GOMA_CXX} -xc++ -Itest -c -o out.o test/oneinclude2.cc"
# - precompile header
rm -rf test/tmp
mkdir -p test/tmp
expect_success "${CXX}_precompile_common" \
   "${GOMA_CXX} -xc++-header -c -o test/tmp/common.h.gch test/common.h"
expect_success "${CXX}_precompile_common_local_output" \
   "test -f test/tmp/common.h.gch"
expect_success "${CXX}_precompile_common_remote_output" \
   "test -f test/tmp/common.h"
rm -rf test/tmp
mkdir -p test/tmp
expect_success "${CXX}_no_x_precompile_common" \
   "${GOMA_CXX} -c -o test/tmp/common.h.gch test/common.h"
expect_success "${CXX}_no_x_precompile_common_local_output" \
   "test -f test/tmp/common.h.gch"
expect_success "${CXX}_no_x_precompile_common_remote_output" \
   "test -f test/tmp/common.h"

rm -f out.o out_local.o
expect_success "${CXX}_oneinclude2_with_precompiled_common" \
   "${GOMA_CXX} -xc++ -Itest/tmp -c -o out.o test/oneinclude2.cc"
expect_success "${CXX}_oneinclude2_with_local_precompiled_common" \
   "${LOCAL_CXX} -xc++ -Itest/tmp -c -o out_local.o test/oneinclude2.cc"
rm -rf test/tmp out.o out_local.o

# If TSAN tests succeed with LOCAL_CXX, they should also succeed with GOMA_CXX.
if (${LOCAL_CXX} -DTHREAD_SANITIZER -fsanitize=thread -fPIC \
    -mllvm -tsan-blacklist=test/tsan-ign.txt \
    -o out.o -c test/oneinclude.cc \
    >/dev/null 2>/dev/null); then
  expect_success "${CXX}_tsan_blacklist" \
   "${GOMA_CXX} -DTHREAD_SANITIZER -fsanitize=thread -fPIC \
    -mllvm -tsan-blacklist=test/tsan-ign.txt \
    -o out.o -c test/oneinclude.cc"
fi
if (${LOCAL_CXX} -DTHREAD_SANITIZER -fsanitize=thread -fPIC \
    -fsanitize-blacklist=test/tsan-ign.txt \
    -o out.o -c test/oneinclude.cc \
    >/dev/null 2>/dev/null); then
  expect_success "${CXX}_thread_sanitize_blacklist" \
   "${GOMA_CXX} -DTHREAD_SANITIZER -fsanitize=thread -fPIC \
    -fsanitize-blacklist=test/tsan-ign.txt \
    -o out.o -c test/oneinclude.cc"
fi

if [ "$CXX" = "clang++" ]; then
  # See: b/16826568
  ext=".so"
  if [ "$(uname -s)" == "Darwin" ]; then
    ext=".dylib"
  fi

  expect_success "${CXX}_load_plugin_in_relative_path" \
  "${GOMACC} ${LOCAL_CXX} -Xclang -load -Xclang \
   third_party/llvm-build/Release+Asserts/lib/libFindBadConstructs${ext} \
   -o out.o -c test/oneinclude.cc"
fi

if [ "$CXX" = "g++" ]; then
  # CQ of goma client uses gcc 4.8.4 and has_include is not supported.
  # TODO: Remove this if we update gcc.
  MAYBE_FAIL="FAIL_"
fi

expect_success "${MAYBE_FAIL}has_include" \
  "${LOCAL_CXX} -c test/has_include.cc -o has_include.o"
expect_success "${MAYBE_FAIL}has_include" \
  "${GOMACC} ${CXX} -c test/has_include.cc -o has_include.o"
rm -f has_include.o

MAYBE_FAIL=

# TODO: From 2015-07-22, -fprofile-generate looks creating
# default.profraw instead of test.profdata. We need to convert test.profraw
# to test.profdata with llvm-profdata to use it with -fprofile-use.
# However, chromium clang does not provide it yet. So, this test might fail.
# See http://b/22723864

if [ "$CXX" = "clang++" ]; then
  # chrome's clang doesn't have libprofile_rt.a in lib, so it will fail
  # /usr/bin/ld: error: cannot open
  #   /path/to/llvm-build/Release+Asserts/bin/../lib/libprofile_rt.a:
  #   No such file or directory
  # clang: error: linker command failed with exit code 1
  MAYBE_FAIL="FAIL_"
fi
expect_success "${MAYBE_FAIL}${CXX}_fprofile_generate" \
   "${LOCAL_CXX} -xc++ -fprofile-generate test/hello.c"

./a.out > /dev/null
expect_success "${MAYBE_FAIL}${CXX}_fprofile_use" \
   "${GOMA_CXX} -xc++ -c -fprofile-use test/hello.c 2> warning"
expect_success "${MAYBE_FAIL}${CXX}_fprofile_use_local" \
   "${LOCAL_CXX} -xc++ -c -fprofile-use test/hello.c 2> warning.local"
expect_success "${MAYBE_FAIL}${CXX}_fprofile_use_warning" \
   "cmp warning warning.local"
diff -u warning warning.local
rm -f out.o a.out hello.o hello.gcda warning.local warning a.out \
   default.profraw test.profdata

MAYBE_FAIL=

if [ "$(uname)" = "Darwin" ]; then
  rm -f out.o
  # failure without fallback
  expect_failure "${CXX}_multi_arch_no_fallback" \
   "${GOMA_CXX} -arch i386 -arch x86_64 -c -o out.o test/hello.c"
  rm -f out.o
fi


rm -f test/compile_error.{out,err} test/compile_error_fallback.{out,err}
expect_failure "${CXX}_compile_error.cc" \
  "${GOMA_CXX} test/compile_error.cc -c -o test/compile_error.o \
    > test/compile_error.out 2> test/compile_error.err"

GOMA_FALLBACK=true  # run local when remote failed.
GOMA_USE_LOCAL=false  # don't run local when idle.
expect_failure "${CXX}_fail_fallback" \
  "${GOMA_CXX} test/compile_error.cc -c -o test/compile_error.o \
  > test/compile_error_fallback.out 2> test/compile_error_fallback.err"

expect_success "compile_error_out" \
  "cmp test/compile_error.out test/compile_error_fallback.out"
expect_success "compile_error_err" \
  "cmp test/compile_error.err test/compile_error_fallback.err"

if [ "$(uname)" = "Darwin" ]; then
  rm -f out.o
  expect_success "${CXX}_multi_arch_fallback" \
   "${GOMA_CXX} -arch i386 -arch x86_64 -c -o out.o test/hello.c"
  rm -f out.o
fi

expect_success "no_path_env" \
  "(unset PATH; ${goma_bin_dir}/gomacc ${LOCAL_CXX} -c -o out.o test/hello.c)"
rm -f out.o
expect_success "empty_path_env" \
  "PATH= ${goma_bin_dir}/gomacc ${LOCAL_CXX} -c -o out.o test/hello.c"
rm -f out.o

expect_failure "gomacc_gomacc" \
  "${GOMACC} ${GOMA_CC} -c -o out.o test/hello.c"
rm -f out.o
expect_success "gomacc_path_gomacc" \
  "PATH=${goma_bin_dir}:$PATH \
   ${GOMACC} ${CC} -c -o out.o \
   test/hello.c"
rm -f out.o

expect_failure "disabled_true_masquerade_gcc" \
  "GOMA_DISABLED=1 \
   ${GOMA_CC} -c -o out.o test/hello.c"
rm -f out.o

curl -s http://localhost:$GOMA_COMPILER_PROXY_PORT/statz | grep request > stat_before.txt
expect_success "disabled_true_gomacc_local_path_gcc" \
  "GOMA_DISABLED=1 \
   ${GOMACC} ${LOCAL_CC} -c -o out.o test/hello.c"
curl -s http://localhost:$GOMA_COMPILER_PROXY_PORT/statz | grep request > stat_after.txt
expect_success "disabled_true_gomacc_local_path_gcc_not_delivered" \
   "cmp stat_before.txt stat_after.txt"
diff -u stat_before.txt stat_after.txt

rm -f out.o
rm -f stat_before.txt
rm -f stat_after.txt

expect_success "disabled_true_gomacc_masquerade_gcc" \
  "GOMA_DISABLED=1 \
   PATH=${goma_bin_dir}:$PATH \
   ${GOMACC} ${CC} -c -o out.o test/hello.c"
rm -f out.o

expect_success "disabled_true_gomacc_gcc_in_local_path" \
  "GOMA_DISABLED=1 \
   PATH=$(dirname ${LOCAL_CC}) \
   ${GOMACC} ${CC} -c -o out.o test/hello.c"
rm -f out.o

# GOMA_HERMETIC=error

if [ "$(uname)" = "Linux" ]; then
  AS=$test_dir/third_party/binutils/Linux_x64/Release/bin/as
  if [ ! -f $AS ]; then
    AS=$test_dir/third_party/binutils/Linux_ia32/Release/bin/as
  fi
  #if as does not exist, fallbacks to system's as.
  if [ ! -f $AS ]; then
    AS=$(which as)
  fi
  echo "Using as: ${AS}" 1>&2
  cp -p ${AS} ./as
  expect_success "${CC}_unmodified_as_with_hermetic" \
    "${GOMACC} ${LOCAL_CC} -gsplit-dwarf -B. -c -o out.o test/hello.c"
  rm -f ./as
  rm -f out.o

  # create as with different SHA256.
  cp -p ${AS} ./as
  echo >> ./as
  if [ "$CC" = "gcc" -a "$(is_cros_gcc $CC)" = "yes" ]; then
    expect_success "unknown_as_with_hermetic_for_cros_gcc" \
      "${GOMACC} ${LOCAL_CC} -B. -c -o out.o test/hello.c"
  elif [ "$CC" = "clang" ]; then
    expect_failure "${CC}_unknown_as_with_hermetic" \
      "${GOMACC} ${LOCAL_CC} -no-integrated-as -B. -c -o out.o test/hello.c"
  else
    expect_failure "${CC}_unknown_as_with_hermetic" \
      "${GOMACC} ${LOCAL_CC} -B. -c -o out.o test/hello.c"
  fi
  rm -f ./as

  cp -p ${AS} ./as
  expect_success "${CC}_after_unknown_as_with_hermetic" \
    "${GOMACC} ${LOCAL_CC} -gsplit-dwarf -B. -c -o out.o test/hello.c"
  rm -f ./as
  rm -f out.o

  # check PWD=/proc/self/cwd gcc -fdebug-prefix-map=/proc/self/cwd=
  # http://b/27487704
  mkdir dir1 dir2
  cp test/hello.c dir1
  (cd dir1; expect_success "${CC}_no_pwd_in_dir1" \
     "PWD=/proc/self/cwd ${GOMACC} ${LOCAL_CC} \
     -fdebug-prefix-map=/proc/self/cwd= -g -c -o out.o hello.c")
  cp test/hello.c dir2
  (cd dir2; expect_success "${CC}_no_pwd_in_dir2" \
     "PWD=/proc/self/cwd ${GOMACC} ${LOCAL_CC} \
      -fdebug-prefix-map=/proc/self/cwd= -g -c -o out.o hello.c")
  expect_success "${CC}_deterministic_no_pwd" \
     "cmp dir1/out.o dir2/out.o"
  readelf --debug-dump dir1/out.o > dir1/out.debug
  readelf --debug-dump dir2/out.o > dir2/out.debug
  diff -u dir1/out.debug dir2/out.debug
  rm -rf dir1 dir2

  # check PWD=/proc/self/cwd ~/goma/gomacc linux-x86/clang-2690385/bin/clang -c
  # -g -fdebug-prefix-map=/proc/self/cwd= -no-canonical-prefixes test.c
  # b/28088682
  if [ "$GOMATEST_USE_CHROMIUM_CLANG" = "1" ]; then
    clang_path="../$(basename "$(dirname "$(dirname ${LOCAL_CC})")")/bin/clang"
    cp -rp "$(dirname "$(dirname ${LOCAL_CC})")" .
    # Local case.
    mkdir dir1 dir2
    cp test/test_pwd_hack.c dir1
    (cd dir1; expect_success "${CC}_no_pwd_in_include_wo_goma_local" \
      "GOMA_USE_LOCAL=true PWD=/proc/self/cwd ${clang_path} -c -g \
      -fdebug-prefix-map=/proc/self/cwd= -no-canonical-prefixes \
      -o out.o test_pwd_hack.c")
    cp test/test_pwd_hack.c dir2
    (cd dir2; expect_success "${CC}_no_pwd_in_include_with_goma_local" \
      "GOMA_USE_LOCAL=true PWD=/proc/self/cwd ${GOMACC} ${clang_path} \
      -c -g -fdebug-prefix-map=/proc/self/cwd= -no-canonical-prefixes \
      -o out.o test_pwd_hack.c")
    expect_success "${CC}_deterministic_no_pwd_in_include_local" \
      "cmp dir1/out.o dir2/out.o"
    readelf --debug-dump dir1/out.o > dir1/out.debug
    readelf --debug-dump dir2/out.o > dir2/out.debug
    diff -u dir1/out.debug dir2/out.debug
    rm -rf dir1 dir2

    # TODO: implement this when the issue for remote case fixed.
  fi
fi

if [ "$GOMATEST_USE_CHROMIUM_CLANG" = "1" ]; then
  # automatically detects .so and .dylib.
  CLANG_PLUGIN=$(echo $(dirname $LOCAL_CXX)/../lib/libFindBadConstructs.*)
  CLANG_PLUGIN_BASE=$(basename $CLANG_PLUGIN)

  cp -p ${CLANG_PLUGIN} ./${CLANG_PLUGIN_BASE}
  expect_success "${CXX}_unmodified_plugin_with_hermetic" \
    "${GOMACC} ${LOCAL_CXX} -Xclang -load -Xclang ./${CLANG_PLUGIN_BASE} \
    -c -o out.o test/hello.c"
  rm -f ${CLANG_PLUGIN_BASE}
  rm -f out.o

  cp -p ${CLANG_PLUGIN} ./${CLANG_PLUGIN_BASE}
  echo >> ./${CLANG_PLUGIN_BASE}
  expect_failure "${CXX}_unknown_plugin_with_hermetic" \
    "${GOMACC} ${LOCAL_CXX} -Xclang -load -Xclang ./${CLANG_PLUGIN_BASE} \
    -c -o out.o test/hello.c"
  rm -f ${CLANG_PLUGIN_BASE}
  rm -f out.o

  cp -p ${CLANG_PLUGIN} ./${CLANG_PLUGIN_BASE}
  expect_success "${CXX}_after_unknown_plugin_with_hermetic" \
    "${GOMACC} ${LOCAL_CXX} -Xclang -load -Xclang ./${CLANG_PLUGIN_BASE} \
    -c -o out.o test/hello.c"
  rm -f ${CLANG_PLUGIN_BASE}
  rm -f out.o
fi

GOMA_USE_LOCAL=false
GOMA_FALLBACK=false
expect_success "${CXX}_compile_with_umask_remote" \
  "(umask 777; ${GOMACC} ${LOCAL_CC} -o out.o -c test/hello.c)"
expect_success "${CXX}_expected_umask_remote" \
  "[ \"$(ls -l out.o | awk '{ print $1}')\" = \"----------\" ]"
rm -f out.o

GOMA_USE_LOCAL=true
expect_success "${CXX}_compile_with_umask_local" \
  "(umask 777; ${GOMACC} ${LOCAL_CC} -o out.o -c test/hello.c)"
expect_success "${CXX}_expected_umask_local" \
  "[ \"$(ls -l out.o | awk '{ print $1}')\" = \"----------\" ]"
rm -f out.o

# TODO
# write a test to send a compiler binary to the backend.

curl --dump-header header.out \
  -X POST --data-binary @${test_dir}/badreq.bin \
  -H 'Content-Type: binary/x-protocol-buffer' \
  http://localhost:${GOMA_COMPILER_PROXY_PORT}/e
expect_success "access_rejected" \
  "head -1 header.out | grep -q 'HTTP/1.1 401 Unauthorized'"
rm -f header.out

if [ -n "${GLOG_log_dir:-}" ]; then
  # Smoke test to confirm gomacc does not create logs.
  # I know there are several tests that make gomacc to write logs but should
  # not be so much.
  expect_success "smoke_test_gomacc_does_not_create_logs_much" \
    "[ \"$(echo ${GLOG_log_dir}/gomacc.* | wc -w)\" -lt "20" ]"
fi

# Gomacc should write log to GLOG_log_dir.
mkdir -p "$tmpdir/gomacc_test"
expect_success "gomacc_should succeed_with_write_log_flag" \
    "GOMA_GOMACC_WRITE_LOG_FOR_TESTING=true \
     GLOG_log_dir=${tmpdir}/gomacc_test ${GOMACC}"
expect_success "gomacc_should_create_log_file" \
  "[ \"$(echo ${tmpdir}/gomacc_test/gomacc.* | wc -w)\" -eq "2" ]"

if [ "${#FAIL[@]}" -ne 0 ]; then
  echo_fail "Failed tests: ${FAIL[@]}"
fi
if [ "${#KNOWN_FAIL[@]}" -ne 0 ]; then
  echo_known_fail "Known failed tests: ${KNOWN_FAIL[@]}"
fi
if [ "${#FAIL[@]}" -eq 0 -a "${#KNOWN_FAIL[@]}" -eq 0 ]; then
  echo_ok "All tests passed: $CC $CXX"
fi

if [ "$GOMATEST_USE_CHROMIUM_CLANG" = "" ]; then
 if [ -x "$CLANG_PATH/clang" -a -x "$CLANG_PATH/clang++" ]; then
   PATH=$CLANG_PATH:$PATH
   # clang (clang version 1.1) shipped in ubuntu/lucid are too old and
   # don't support -dumpmachine option.
   if clang -v > /dev/null 2>&1 && clang++ -v > /dev/null 2>&1 && \
      clang -dumpmachine > /dev/null 2>&1 && \
      clang++ -dumpmachine > /dev/null 2>&1 ; then
     GOMATEST_USE_RUNNING_COMPILER_PROXY=1 \
      GOMATEST_USE_CHROMIUM_CLANG=1 \
      CC=clang CXX=clang++ \
       $test_dir/$(basename $0)
     if [ "$?" != 0 ]; then
       FAIL+=("clang");
     fi
   else
     echo_warn "WARNING: clang in $CLANG_PATH is too old."
   fi
 else
   echo_warn "WARNING: no clang in $CLANG_PATH"
 fi
fi

if [ "$FLAGS_dump" != "" ]; then
   (cd $GOMA_TMP_DIR && tar zcf $FLAGS_dump task_request_*)
   echo "task dump in $FLAGS_dump"
fi

if [ "$FLAGS_wait" = "1" ]; then
  echo -n "Ready to finish? "
  read
fi
echo exit "${#FAIL[@]} # ${CC} ${CXX}"
exit "${#FAIL[@]}"
