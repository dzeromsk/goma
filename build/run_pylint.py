#!/usr/bin/env python

# Copyright 2012 The Goma Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Script for running pylint for python scripts."""



import glob
import os
import subprocess
import sys


class FileNotFoundError(Exception):
  pass


def FindPath(target, paths):
  """Finds target from paths

  Args:
    target: a filename
    paths: a list of paths.
  Returns:
    a path name of the target
  """
  for path in paths:
    fname = os.path.join(path, target)
    if os.path.exists(fname):
      return fname
  raise FileNotFoundError('%s not found' % target)


def main():
  result = []
  err = 0
  pylint = FindPath('pylint.py', os.environ.get('PATH', '').split(os.pathsep))
  base_dir = os.path.dirname(os.path.abspath(__file__))
  os.chdir(os.path.join(base_dir, '..'))

  print 'run %s */*.py at %s' % (pylint, os.path.abspath('.'))

  # depot_tools/pylint set this env.
  os.environ['PYTHONDONTWRITEBYTECODE'] = '1'

  for py in glob.iglob(os.path.join('*', '*.py')):
    proc = subprocess.Popen([sys.executable, pylint, py],
                            stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT)
    output = proc.communicate()[0]
    print 'pylint %s...' % py
    print output
    if proc.returncode:
      # pylint --long-help shows pylint output status code, which is bit-ORed
      err_type = []
      is_test = py.endswith('_test.py')
      if is_test:
        err_type.append('TEST')
      if proc.returncode & 1:
        err_type.append('fatal')
      if proc.returncode & 2:
        err_type.append('error')
      if proc.returncode & 4:
        err_type.append('warning')
      if proc.returncode & 8:
        err_type.append('refactor')
      if proc.returncode & 16:
        err_type.append('convention')
      if proc.returncode & 32:
        err_type.append('usage')
      result.append('%s: %s' % (','.join(err_type), py))
      if not is_test:
        err |= proc.returncode
    else:
      result.append('OK: %s' % py)
    print
  print 'run_pylint.py results:'
  for r in result:
    print r
  # failed for fatal/error messages were issued.
  return err & 3


if __name__ == '__main__':
  sys.exit(main())
