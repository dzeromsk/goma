#!/bin/bash
#
# Copyright 2012 The Goma Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

#

function httpfetch() {
  local host="$1"
  local port="$2"
  local path="$3"
  local method="$4"
  if command -v wget > /dev/null 2>&1; then
    postarg=""
    if [ "$method" = "post" ]; then
       postarg="--post-data="
    fi
    wget $postarg  -o/dev/null -O- "http://$host:$port$path"
  elif command -v curl > /dev/null 2>&1; then
    postarg=""
    if [ "$method" = "post" ]; then
      postarg="--data="
    fi
    curl $postarg -s "http://$host:$port$path"
  elif command -v nc > /dev/null 2>&1; then
    httpmethod="GET"
    if [ "$method" = "post" ]; then
      httpmethod="POST"
    fi
    printf "$httpmethod $path HTTP/1.1\r\nHost: $host:$port\r\n\r\n" | \
      nc $host $port | \
      sed -e '1,/^
/d'
  else
    echo 'wget, curl, or nc not found.' >&2
    exit 1
  fi
}

function update_compiler_proxy_port() {
  local goma_dir="$1"
  local num_tries="$2"
  if [ ! -x $goma_dir/gomacc ]; then
     echo "FATAL: $goma_dir/gomacc not found." >&2
     exit 1
  fi
  for (( i = 0; i < $num_tries; i++)) do
    port="$(GLOG_logtostderr=true $goma_dir/gomacc port 2>/dev/null)"
    if [ "$port" != "" ]; then
      export GOMA_COMPILER_PROXY_PORT=$port
      return 0
    fi
    if [ "$COMPILER_PROXY_PID" != "" ] && \
      ! kill -0 "$COMPILER_PROXY_PID" > /dev/null 2>&1; then
      return 1
    fi
    echo "waiting for compiler_proxy's unix domain socket..."
    sleep 1
  done
  return 1
}

function wait_shutdown() {
  local pid="$1"
  local num_tries=10

  for (( i = 0; i < $num_tries; i++ )); do
    if ! kill -0 "$pid" > /dev/null 2>&1; then
      return 0
    fi
    echo "waiting for compiler_proxy's shutdown..."
    sleep 1
  done
  return 1
}

function stop_compiler_proxy() {
  httpfetch localhost ${GOMA_COMPILER_PROXY_PORT} /quitquitquit
  echo

  local ipc_pid=$(fuser "$GOMA_COMPILER_PROXY_SOCKET_NAME" 2>/dev/null)
  if ! wait_shutdown $ipc_pid; then
    echo "time's up. going to kill -9 $ipc_pid"
    kill -9 "$ipc_pid"
  fi
}

# watch a URL until it returns 'ok' or 'running:*'
function watch_healthz() {
  local host="$1" # URL to watch
  local port="$2"
  local path="$3"
  local name="$4" # name of process
  local num_tries=30
  for (( i = 0; i < $num_tries; i++)) do
    status="$(httpfetch $host $port $path)"
    case "$status" in
    ok) return;;
    running:*) echo "$name is $status"; return;;
    *)
      echo "waiting for $name to start up (http://$host:$port$path)"
      sleep 1
      continue;;
    esac
  done
  echo "$name failed to start up?" >&2
  exit 1
}

function set_goma_dirs() {
  local bin_subdir="$1"

  if [ "$bin_subdir" = "" ]; then
    bin_subdir=out/Release
  fi

  if [ ! -d "$bin_subdir" ]; then
    echo "Directory $bin_subdir doesn't exist" >&2
    exit 1
  fi

  # Get the fullpath.
  goma_bin_dir=$(cd "$bin_subdir"; pwd)

  if [ "$GOMA_COMPILER_PROXY_BINARY" = "" ]; then
    export GOMA_COMPILER_PROXY_BINARY="$goma_bin_dir/compiler_proxy"
  fi

  for binary in "$GOMA_COMPILER_PROXY_BINARY" "$goma_bin_dir/gomacc"; do
    if [ ! -x $binary ]; then
      echo "$binary is not an executable" >&2
      exit 1
    fi
  done
}
