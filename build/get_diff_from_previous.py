#!/usr/bin/env python

# Copyright 2016 The Goma Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Script to show diff from previous commit."""

from __future__ import print_function

import argparse
import json
import os
import subprocess

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))


def main():
  parser = argparse.ArgumentParser()
  parser.add_argument('-o', '--output-json',
                      help=('A path to output filenames in JSON.'))
  options = parser.parse_args()

  git = 'git.bat' if os.name == 'nt' else 'git'
  out = subprocess.check_output([git, 'diff', '--name-only', 'HEAD~1'],
                                cwd=SCRIPT_DIR)

  if options.output_json:
    with open(options.output_json, 'w') as f:
      result = out.splitlines()
      json.dump(result, f)
  print(out)


if __name__ == '__main__':
  main()
