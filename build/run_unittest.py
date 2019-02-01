#!/usr/bin/env python

# Copyright 2012 The Goma Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Script for running all goma unit tests.
Use -h to see its usage.
"""

from __future__ import print_function



import argparse
import find_depot_tools
import os
import sys

SCRIPT_DIR = os.path.dirname(__file__)

sys.path.append(os.path.join(SCRIPT_DIR, os.pardir, "third_party",
                             "subprocess32"))
import subprocess32


CLIENT_ABS_DIR = os.path.abspath(os.path.join(SCRIPT_DIR, '..'))
OUT_ABS_DIR = os.path.abspath(os.path.join(CLIENT_ABS_DIR, 'out'))

TEST_DIRS = ('base', 'lib', 'client')


def TestNames(case_key):
  """TestNames returns test names for the case key (in TEST_DIRS).

  Args:
    case_key: test case key. one of TEST_DIRS.
  Returns
    test case names.
  """
  gn_py_path = os.path.join(find_depot_tools.DEPOT_TOOLS_PATH, 'gn.py')
  output = subprocess32.check_output([sys.executable,
                                      gn_py_path,
                                      'ls', '.', '//%s/*' % case_key,
                                      '--testonly=true',
                                      '--type=executable',
                                      '--as=output'])
  return [test for test in output.split() if test != 'vstestrun.exe']


class TestError(Exception):
  pass


def SetupClang():
  clang_path = os.path.join(CLIENT_ABS_DIR, 'third_party', 'llvm-build',
                            'Release+Asserts', 'bin', 'clang')
  if subprocess32.call([clang_path, "-v"]) == 0:
    os.environ['GOMATEST_CLANG_PATH'] = clang_path
    print('GOMATEST_CLANG_PATH=' + os.environ['GOMATEST_CLANG_PATH'])
  else:
    print('clang is not runnable here. disable clang test')


def RunTest(build_dir, target, case_opt, non_stop):
  tests_passed = 0
  expected_passes = 0
  failed_tests = []

  config_dir = os.path.join(build_dir, target)
  try:
    os.chdir(config_dir)
  except OSError:
    sys.stdout.write("\nERROR: folder not found: " + target)
    return (tests_passed, expected_passes, failed_tests)

  for case_key in TEST_DIRS:
    case_names = TestNames(case_key)
    if case_opt != "all" and case_opt != case_key:
      continue
    expected_passes += len(case_names)
    for case in case_names:
      try:
        sys.stdout.write("\nINFO: <" + target + "> case: " + case + "\n")
        return_code = subprocess32.call(os.path.join('.', case),
                                        stdout=sys.stdout, stderr=sys.stderr,
                                        timeout=60)
        if return_code != 0:
          error_message = case + " failed"
          raise TestError(error_message)
        tests_passed += 1
      except Exception as ex:
        sys.stdout.write("\nERROR: " + str(ex))
        failed_tests.append('target:' + target + ' test:' + case)
        if not non_stop:
          return (tests_passed, expected_passes, failed_tests)
  return (tests_passed, expected_passes, failed_tests)


def main():
  parser = argparse.ArgumentParser(description='Unittest driver')
  parser.add_argument('--build-dir', help='output folder',
                      default=OUT_ABS_DIR,
                      metavar='path')
  parser.add_argument('--target', default='Release',
                      help='build config in output folder to test')
  parser.add_argument('--test-cases',
                      choices=['all'] + list(TEST_DIRS),
                      default='all', help='test cases to run')
  parser.add_argument('-n', '--non-stop', dest='non_stop', action='store_true',
                      help='do not stop when errors occur in test cases')
  args = parser.parse_args()

  is_windows = (sys.platform == 'cygwin' or sys.platform.startswith('win'))
  if not is_windows:
    SetupClang()

  passed, expected, failed_tests = RunTest(
      args.build_dir, args.target, args.test_cases, args.non_stop)
  sys.stdout.write("\nINFO: Total tests passed: " + str(passed) +
                   " expected: " + str(expected) + "\n")
  if passed != expected:
    sys.stdout.write("ERROR: Test failed\n")
    for failed in failed_tests:
      sys.stdout.write(" " + failed + "\n")
    sys.exit(1)
  else:
    sys.stdout.write("INFO: All tests passed\n")

if __name__ == "__main__":
  main()
