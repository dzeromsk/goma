#!/usr/bin/python
#
# Copyright 2015 The Goma Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Get compiler_proxy git revision."""

from __future__ import print_function

import argparse
import os
import subprocess

_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))


def GetRevisionNumber(args):
  """Obtain a number to represent revision of source code.

  Args:
    args: an instance of argparse.namespace.
  """
  # <commit hash>@<committer date unix timestamp>
  git_hash = subprocess.check_output(
      ['git', 'log', '-1', '--pretty=format:%H@%ct'],
      cwd=_SCRIPT_DIR).strip()
  if not git_hash:
    print('No git hash set. use unknown as fallback.')
    git_hash = 'unknown'

  if args.output_file:
    with open(args.output_file, 'w') as f:
      f.write(git_hash)
  else:
    print(git_hash)


def main():
  parser = argparse.ArgumentParser()
  parser.add_argument('-o', '--output-file', help='Output filename')
  args = parser.parse_args()
  GetRevisionNumber(args)


if __name__ == '__main__':
  main()
