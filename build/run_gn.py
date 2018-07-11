#!/usr/bin/env python
# Copyright 2016 The Goma Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Wrapper script to run gn in this directory."""

import os
import sys
import subprocess
import find_depot_tools

_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))


def main(args):
  gn_py_path = os.path.join(find_depot_tools.DEPOT_TOOLS_PATH, 'gn.py')
  subprocess.check_call(
      [sys.executable, gn_py_path] + args[1:],
      cwd=_SCRIPT_DIR)


if __name__ == '__main__':
  sys.exit(main(sys.argv))
