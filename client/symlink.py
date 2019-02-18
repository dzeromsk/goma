#!/usr/bin/env python
#
# Copyright 2015 The Goma Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Generate symlinks from one to the others.

Usage:
  % symlink.py --target gomacc "gcc,g++,javac"
"""

import argparse
import errno
import os


def main():
  parser = argparse.ArgumentParser(description='create symlink')
  parser.add_argument('--force', action='store_true',
                      help='remove symlink if exist')
  parser.add_argument('--target', help='symlink target name', required=True)
  parser.add_argument('links', metavar='files', nargs='+', help='link names')
  args = parser.parse_args()

  for f in args.links:
    if args.force:
      # this is "rm -f", it is ok to fail.
      try:
        os.remove(f)
      except OSError as err:
        if err.errno != errno.ENOENT:
          raise
    os.symlink(args.target, f)

if __name__ == '__main__':
  main()
