#!/usr/bin/env python
# Copyright 2016 The Goma Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Wrapper script to run gn in this directory."""

import os
import sys
import subprocess

_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))


def GetDepotToolsPath():
  """Returns path to depot_tools."""
  paths = os.environ.get('PATH', '').split(os.path.pathsep)
  for path in paths:
    if os.path.basename(path) == 'depot_tools':
      return path


def main(args):
  depot_tools_path = GetDepotToolsPath()
  if not depot_tools_path:
    raise Exception('depot_tools path not found in PATH')
  subprocess.check_call(
      [sys.executable, os.path.join(depot_tools_path, 'gn.py')] + args[1:],
      cwd=_SCRIPT_DIR)


if __name__ == '__main__':
  sys.exit(main(sys.argv))
