#!/usr/bin/env python
# Copyright 2019 The Goma Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Utility for processing licensing information in third_party directories.

Usage: licenses.py <goma LICENSE file> <third_party directory> <output file>

"""

import argparse
import os
import sys

# Skip these directories in third_party.
PRUNE_DIRS = [
  'config',
  'llvm-build',
]

# These libraries don't have LICENSE in the directory.
# See path instead.
SPECIAL_CASES = {
  'lss': {
    'path': 'LICENSE.lss',
  },
  'zlib': {
    'path': 'LICENSE.zlib',
  },
}


def ReadFile(path):
  """Returns file contents."""
  with open(path) as f:
    return f.read()


def IsLicenseFile(name):
  """Returns true if name looks like a license file."""
  return name in ['LICENSE', 'LICENSE.md', 'COPYING', 'COPYING.txt']


def FindLicense(third_party_dir, library_name):
  """Find a license file in {third_party_dir}/{library_name}.
  Returns its contents."""

  if library_name in SPECIAL_CASES:
    return ReadFile(os.path.join(third_party_dir,
                    SPECIAL_CASES[library_name]['path']))

  for curdir, dirs, files in os.walk(os.path.join(third_party_dir,
                                                  library_name)):
    for f in files:
      if IsLicenseFile(f):
        return ReadFile(os.path.join(curdir, f))

  # license file wa not found.
  return None


def AddLicenseFile(license, name, contents):
  """Add contents to license with title name."""

  if license != '':
    license += '\n\n'
  license += name + '\n'
  license += '=' * len(name) + '\n'
  license += contents
  return license


def main():
  parser = argparse.ArgumentParser()
  parser.add_argument('goma_license')
  parser.add_argument('third_party_dir')
  parser.add_argument('output_file')
  args = parser.parse_args()

  # First, process goma client LICENSE file.
  result = ''
  if os.path.exists(args.goma_license):
    result = AddLicenseFile(result, 'Goma Client', ReadFile(args.goma_license))

  # Then, process third_party license files.
  for d in sorted(os.listdir(args.third_party_dir)):
    if not os.path.isdir(os.path.join(args.third_party_dir, d)):
      continue
    if d in PRUNE_DIRS:
      continue
    license = FindLicense(args.third_party_dir, d)
    if not license:
      raise Exception('license file not found in {}'.format(d))
    result = AddLicenseFile(result, d, license)

  with open(args.output_file, 'w') as f:
      f.write(result)


if __name__ == '__main__':
  sys.exit(main())
