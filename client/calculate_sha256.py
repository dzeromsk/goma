#!/usr/bin/env python
#
# Copyright 2016 The Goma Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Calculate checksum of files.

Usage:
  % calculate_sha256.py --output sha256.json compiler_proxy gomacc
"""

import argparse
import hashlib
import json
import os


class Error(Exception):
  """Raised on Error."""


def CalculateFileSHA256(filename):
  """Returns sha256sum of file."""
  with open(filename, 'rb') as f:
    return hashlib.sha256(f.read()).hexdigest()


def main():
  parser = argparse.ArgumentParser(description='calculate sha256')
  parser.add_argument('inputs', metavar='FILENAME', type=str, nargs='+',
                      help='input file')
  parser.add_argument('--output', help='json filename', required=True)
  args = parser.parse_args()

  sha256 = {}
  for input_file in args.inputs:
    base = os.path.basename(input_file)
    sha256[base] = CalculateFileSHA256(input_file)

  with open(args.output, 'w') as f:
    json.dump(sha256, f)


if __name__ == '__main__':
  main()
