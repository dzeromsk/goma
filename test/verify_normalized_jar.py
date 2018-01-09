#!/usr/bin/env python
#
# Copyright 2017 The Goma Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Verifies normalized jar file.

% verify_normalized_jar.py <original_jar_file> <normalized_jar_file>

The program confirms:
  - timestamps of all files and directrories in normalized jar file are
    MS-DOS epoch time (1980-01-01T00:00:00).
  - normalized_jar_file has the same contents with original jar file.
"""

import datetime
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile
import time


JAR_COMMAND_PATH = '/usr/bin/jar'


def CalcFileSha256(filename):
  """Returns SHA256 of the file.

  Args:
    filename: a string file name to calculate SHA256.

  Returns:
    hexdigest string of the file content.
  """
  with open(filename, 'rb') as f:
    return hashlib.sha256(f.read()).hexdigest()


def GetListOfContents(filename):
  """Returns contents information of a given jar file.

  Args:
    filename: a filename of a jar file.

  Returns:
    a dictionary from a filename in the jar file to its information.
    e.g. {
      'META-INF/MANIFEST.MF': {'size': 76, 'sha256': 'abcdef...'},
      ...
    }
  """
  tmpdir = None
  file_to_info = {}
  try:
    tmpdir = tempfile.mkdtemp()
    os.chdir(tmpdir)
    subprocess.check_call([JAR_COMMAND_PATH, 'xf', filename])
    for root, _, files in os.walk(tmpdir):
      for name in files:
        path = os.path.join(root, name)
        file_to_info[path[len(tmpdir):]] = {
            'size': os.stat(path).st_size,
            'sha256': CalcFileSha256(path),
        }
  finally:
    shutil.rmtree(tmpdir)
  return file_to_info


def VerifyNormalizedJarFile(filename, file_infos):
  """Verifies that we have normalized the jar file timestamp.

  Args:
    filename: a normalized jar filename to verify.
    file_infos: an original jar file info got by GetListOfContents.
  """
  tmpdir = None
  msdos_epoch = time.mktime(datetime.datetime(1980, 1, 1, 0, 0, 0).timetuple())
  try:
    tmpdir = tempfile.mkdtemp()
    os.chdir(tmpdir)
    subprocess.check_call([JAR_COMMAND_PATH, 'xf', filename])
    for root, _, files in os.walk(tmpdir):
      if root != tmpdir:
        assert os.stat(root).st_mtime == msdos_epoch
      for name in files:
        path = os.path.join(root, name)
        path_info = file_infos[path[len(tmpdir):]]
        assert os.stat(path).st_mtime == msdos_epoch
        assert os.stat(path).st_size == path_info['size']
        assert CalcFileSha256(path) == path_info['sha256']
  finally:
    shutil.rmtree(tmpdir)


def main(argv):
  if len(argv) != 3:
    sys.stderr.write(''.join([
        'Usage: %s <original jar file> <normalized jar file>\n' % argv[0],
        'e.g. %s Basic.jar Basic_normalized.jar\n' % argv[0],
    ]))
    return 1
  orig, normalized = os.path.realpath(argv[1]), os.path.realpath(argv[2])
  infos = GetListOfContents(orig)
  VerifyNormalizedJarFile(normalized, infos)
  return 0


if __name__ == '__main__':
  sys.exit(main(sys.argv))
