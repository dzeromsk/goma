#!/bin/sh
#
# Copyright 2012 The Goma Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

#
# A simple script which reports the information of this machine.
# This would help users to report their environment.
#

uname=$(uname)
kernel=$(uname -sr)

if [ "x$uname" = "x" ]; then
  echo 'Failed to run uname'
  exit 1
fi

net=$(ping -c 3 apidata.googleusercontent.com | sed '
/^64 bytes from .*ttl=/!d
s///
s/time=//
' | awk '
{
  ttl += $1;
  time += $2;
}
END {
  printf "ttl=%d %.1fms", ttl / 3, time / 3;
}
')

if [ $uname = "Linux" ]; then
  if [ -f /etc/issue.net ]; then
    dist="$(head -n 1 /etc/issue.net) "
  elif [ -f /etc/issue ]; then
    dist="$(head -n 1 /etc/issue) "
  fi
  cpu=$(sed -n '/^model name\s*:\s*/{s///;p;q}' /proc/cpuinfo)
  ncores=$(grep -E '^processor\s*:' /proc/cpuinfo | wc -l)
  ram=$(awk '/^MemTotal: *[0-9]/ {printf "%.1f", $2/1024/1024}' /proc/meminfo)
elif [ $uname = "Darwin" ]; then
  cpu=$(sysctl -n machdep.cpu.brand_string)
  ncores=$(sysctl -i -n hw.availcpu)
  # Yosemite does not have hw.availcpu.
  if [ -z "$ncores" ]; then
    ncores=$(sysctl -n hw.logicalcpu)
  fi
  ram=$(sysctl -n hw.memsize | awk '{printf "%.1f", $1 / 1024/1024/1024}')
elif [ $uname = "FreeBSD" ]; then
  cpu=$(sysctl -n hw.model)
  ncores=$(sysctl -n hw.ncpu)
  ram=$(sysctl -n hw.physmem | awk '{printf "%.1f", $1 / 1024/1024/1024}')
else
  echo "$kernel (unknown OS)"
  exit 0
fi
echo "${dist}${kernel} ${cpu} x${ncores} ${ram}GB ${net}"
