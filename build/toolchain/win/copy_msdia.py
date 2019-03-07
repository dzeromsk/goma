# Copyright (c) 2019 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import shutil
import sys

sys.path.insert(0, os.path.join(
    os.path.dirname(os.path.realpath(__file__)), os.pardir, os.pardir))

import vs_toolchain


def main(target_dir):
  """Copy msdia140.dll into the requested directory.
  msdia140.dll is necessary for breakpad in dump_sym.exe

  Arguments:
    target_dir (string) target directory
  """

  vs_toolchain.SetEnvironmentAndGetRuntimeDllDirs()
  vs_path = vs_toolchain.NormalizePath(os.environ['GYP_MSVS_OVERRIDE_PATH'])

  msdia_path = os.path.join(vs_path, 'DIA SDK', 'bin', 'amd64', 'msdia140.dll')

  target_path = os.path.join(target_dir, 'msdia140.dll')
  shutil.copy2(msdia_path, target_path)


if __name__ == '__main__':
    sys.exit(main(sys.argv[1]))
