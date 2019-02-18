#!/usr/bin/env python
#
# Copyright 2015 The Goma Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Dump executable's breakpad symbols.

Usage:
  % dump_syms.py --dump_syms ../../dump_syms --input compiler_proxy \
    --output compiler_proxy.sym
"""

from __future__ import print_function

import argparse
import os
import platform
import re
import subprocess
import sys

class Error(Exception):
  """Raised on Error."""


class DumpSyms(object):
  """General purpose dump syms class."""

  def __init__(self, dump_syms, src, dst):
    """Initialize dump_sym.

    Args:
      dump_syms: dump_syms command in full path.
      src: a file name to dump symbols.
      dst: an output file to dump symbols.
    """
    self._dump_syms = dump_syms
    self._src = src
    self._dst = dst

  def Dump(self):
    """Dump symbols for breakpad."""
    with open(self._dst, 'w') as f:
      subprocess.check_call([self._dump_syms, self._src], stdout=f)


class MacDumpSyms(DumpSyms):
  """Dump syms for mac."""

  def Dump(self):
    """Dump symbols for breakpad."""
    dsym_file = self._src + '.dSYM'
    subprocess.check_call(['dsymutil', self._src, '-o', dsym_file])
    with open(self._dst, 'w') as f:
      p = subprocess.Popen(
        [self._dump_syms, '-g', dsym_file, self._src],
        stdout=f, stderr=subprocess.PIPE)
      _, stderr_data = p.communicate()

      # Filtering noisy warnings.
      # b/17405320
      # https://crbug.com/392648
      filter_re = re.compile(
        r'^.*: warning: function at offset 0x[a-f\d]+ has no name$' +
        r'|^.*: the DIE at offset 0x[a-f\d]+ has a DW_AT_.*$' +
        r'|^.*: warning: failed to demangle [\.\w]+$' +
        r'|^.*: in compilation unit .* \(offset 0x[a-f\d]+\):$')
      for line in stderr_data.split('\n'):
        if line != '' and not filter_re.match(line):
          print(line, file=sys.stderr)

def GetDumpSyms(dump_syms, src, dst):
  if platform.system() == 'Darwin':
    return MacDumpSyms(dump_syms, src, dst)
  return DumpSyms(dump_syms, src, dst)

def main():
  parser = argparse.ArgumentParser(description='dump breakpad symbols')
  parser.add_argument('--dump_syms', help='path to dump_syms command',
                      required=True)
  parser.add_argument('--input',
                      help=('input for dump_syms command. '
                            'input should be a binary with debug symbols'),
                      required=True)
  parser.add_argument('--output', help='sym filename', required=True)
  args = parser.parse_args()

  if not os.path.exists(args.dump_syms):
    raise Error('dump_syms %s does not exist.' % args.dump_syms)
  if not os.path.exists(args.input):
    raise Error('input binary %s does not exist.' % args.input)
  ds = GetDumpSyms(args.dump_syms, args.input, args.output)
  ds.Dump()


if __name__ == '__main__':
  main()
