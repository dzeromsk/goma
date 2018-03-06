#!/usr/bin/env python

# Copyright 2012 The Goma Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Script for running all goma unit tests.
Use -h to see its usage.
"""



import getopt
import os
import subprocess
import sys

script_dir = os.path.dirname(__file__)
script_absdir = os.path.abspath(script_dir)
client_absdir = os.path.abspath(os.path.join(script_dir, '..'))
is_windows = (sys.platform == 'cygwin' or sys.platform.startswith('win'))
is_mac = sys.platform == 'darwin'

TEST_CASES = [
  ("lib", [
    "cmdline_parser_unittest",
    "compress_util_unittest",
    "compiler_flags_test",
    "execreq_normalizer_unittest",
    "execreq_verifier_unittest",
    "file_reader_unittest",
    "flag_parser_unittest",
    "goma_file_unittest",
    "goma_hash_unittest",
    "lockhelper_unittest",
    "path_resolver_unittest",
    "path_unittest",
    "path_util_unittest",
    ]
  ),
  ("client", [
    "arfile_reader_unittest",
    "atomic_stats_counter_unittest",
    "base64_unittest",
    "callback_unittest",
    "compilation_database_reader_unittest",
    "compile_task_unittest",
    "compiler_info_cache_unittest",
    "compiler_info_unittest",
    "content_cursor_unittest",
    "cpp_parser_unittest",
    "cpp_tokenizer_unittest",
    "cpu_unittest",
    "deps_cache_unittest",
    "directive_filter_unittest",
    "env_flags_unittest",
    "filename_id_table_unittest",
    "goma_ipc_unittest",
    "gomacc_argv_unittest",
    "hash_rewrite_parser_unittest",
    "histogram_unittest",
    "http_rpc_unittest",
    "http_unittest",
    "include_cache_unittest",
    "include_file_utils_unittest",
    "include_processor_unittest",
    "ioutil_unittest",
    "jar_parser_unittest",
    "jarfile_reader_unittest",
    "jwt_unittest",
    "linked_unordered_map_unittest",
    "linker_script_parser_unittest",
    "local_output_cache_unittest",
    "log_cleaner_unittest",
    "luci_context_unittest",
    "machine_info_unittest",
    "mypath_unittest",
    "oauth2_unittest",
    "openssl_engine_unittest",
    "rand_util_unittest",
    "simple_timer_unittest",
    "static_darray_unittest",
    "subprocess_task_unittest",
    "threadpool_http_server_unittest",
    "trustedipsmanager_unittest",
    "util_unittest",
    "worker_thread_manager_unittest",
    "worker_thread_unittest",
    ]
  ),
]

if is_mac:
  TEST_CASES[1][1].append('mac_version_unittest')

if is_windows:
  TEST_CASES[0][1].append('socket_helper_win_unittest')
  TEST_CASES[1][1].append('named_pipe_client_win_unittest')
  TEST_CASES[1][1].append('named_pipe_server_win_unittest')
  TEST_CASES[1][1].append('posix_helper_win_unittest')
  TEST_CASES[1][1].append('spawner_win_unittest')
else:
  TEST_CASES[1][1].append('arfile_unittest')
  TEST_CASES[1][1].append('compiler_flags_util_unittest')
  TEST_CASES[1][1].append('linker_input_processor_unittest')
  if sys.platform.startswith('linux'):
    TEST_CASES[1][1].append('elf_parser_unittest')
    TEST_CASES[1][1].append('library_path_resolver_unittest')
    TEST_CASES[1][1].append('goma-make_unittest')


class TestError(Exception):
  pass


def Usage():
  sys.stdout.write("Usage: python run_unittest.py [options]\n")
  sys.stdout.write("--build-dir=<path>  output folder\n")
  sys.stdout.write("--target=<Release (Default)|(or any)>  "
                   "build config in output folder to test\n")
  sys.stdout.write("--test-cases=<all (default)|lib|client>  "
                   "test cases to run\n")
  sys.stdout.write("-n, --non-stop  "
                   "do not stop when errors occur in test cases\n")
  sys.stdout.write("-h, --help  display Usage\n")
  sys.exit(2)


def SetupClang():
  clang_path = os.path.join(client_absdir, 'third_party', 'llvm-build',
                            'Release+Asserts', 'bin', 'clang')
  if subprocess.call([clang_path, "-v"]) == 0:
    os.environ['GOMATEST_CLANG_PATH'] = clang_path
    print 'GOMATEST_CLANG_PATH=' + os.environ['GOMATEST_CLANG_PATH']
  else:
    print 'clang is not runnable here. disable clang test'


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

  for case_key, case_names in TEST_CASES:
    if case_opt != "all" and case_opt != case_key:
      continue
    expected_passes += len(case_names)
    for case in case_names:
      try:
        sys.stdout.write("\nINFO: <" + target + "> case: " + case + "\n")
        return_code = subprocess.call(os.path.join('.', case),
                                      stdout=sys.stdout, stderr=sys.stderr)
        if return_code != 0:
          error_message = case + " failed"
          raise TestError(error_message)
        tests_passed += 1
      except Exception, ex:
        sys.stdout.write("\nERROR: " + str(ex))
        failed_tests.append('target:' + target + ' test:' + case)
        if not non_stop:
          return (tests_passed, expected_passes, failed_tests)
  return (tests_passed, expected_passes, failed_tests)


def main():
  # parse command line options
  try:
    opts, _ = getopt.getopt(sys.argv[1:], "nh", [
        "build-dir=", "target=", "test-cases=", "non-stop", "help"
    ])
  except getopt.GetoptError, err:
    # print help information and exit
    sys.stdout.write(str(err) + "\n")
    Usage()

  build_dir = script_absdir
  case_value = "all"
  target_value = "Release"
  non_stop = False
  for key, value in opts:
    if key == "--build-dir":
      build_dir = value
    elif key == "--target":
      target_value = value
    elif key == "--test-cases":
      case_value = value
    elif key == "--non-stop" or key == "-n":
      non_stop = True
    elif key == "--help" or key == "-h":
      Usage()
    else:
      sys.stderr.write('Unknown option:' + key)
      Usage()

  if not is_windows:
    SetupClang()

  passed, expected, failed_tests = RunTest(
      build_dir, target_value, case_value, non_stop)
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
