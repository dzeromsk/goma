#!/bin/bash
#
# Copyright 2012 The Goma Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

#

test_dir=$(cd $(dirname $0); pwd)

. $test_dir/gomatest.sh

echo Kill any remaining compiler proxy
killall compiler_proxy

export GOMA_COMPILER_PROXY_PORT=8100
export GOMA_COMPILER_PROXY_NUM_FIND_PORTS=1

set_goma_dirs "$1"

echo "Starting $GOMA_COMPILER_PROXY_BINARY with tsan..."
( cd /tmp && \
  tsan --ignore=$test_dir/tsan-ign.txt \
  $GOMA_COMPILER_PROXY_BINARY > tsan.log 2>&1 &
)
update_compiler_proxy_port $(dirname $GOMA_COMPILER_PROXY_BINARY) 10
watch_healthz localhost ${GOMA_COMPILER_PROXY_PORT} /healthz \
  ${GOMA_COMPILER_PROXY_BINARY}

function at_exit() {
  rm -f /tmp/goma-test-tmp.c /tmp/goma-test-tmp.o
  stop_compiler_proxy
  wait
  echo 'Done. See /tmp/tsan.log'
}

trap at_exit exit sighup sigpipe

cat <<EOF > /tmp/goma-test-tmp.c
#include <stdio.h>
int main() {
  puts("hello-");
}
EOF

# TODO: It seems reversing the order of them will change the result.
#               Investigate a way which can check more cases.
GOMA_USE_LOCAL=0 $goma_bin_dir/gomacc gcc -c /tmp/goma-test-tmp.c
$goma_bin_dir/gomacc gcc -c /tmp/goma-test-tmp.c

curl http://localhost:$GOMA_COMPILER_PROXY_PORT/ > /dev/null
curl -d '' http://localhost:$GOMA_COMPILER_PROXY_PORT/api/taskz > /dev/null
