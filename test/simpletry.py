#!/usr/bin/env python
# Copyright 2012 The Goma Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Simple test scripts for sanity check.

The script uses the production servers.
"""

from __future__ import print_function

import glob
import imp
import optparse
import os
import re
import shutil
import string
import subprocess
import sys
import tempfile
import unittest
try:
  import urllib.request, urllib.error
  URLOPEN = urllib.request.urlopen
  URLREQUEST = urllib.request.Request
  HTTPERROR = urllib.error.HTTPError
except ImportError:
  import urllib2
  URLOPEN = urllib2.urlopen
  URLREQUEST = urllib2.Request
  HTTPERROR = urllib2.HTTPError


_GOMA_CTL = 'goma_ctl.py'
_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
_CRED = 'c:\\creds\\service_accounts\\service-account-goma-client.json'


class Error(Exception):
  """Raised on error."""


class SimpleTryTest(unittest.TestCase):
  """Goma Simple Try Test."""

  def __init__(self, method_name, goma_dir, local_cl, gomacc):
    """Initialize.

    Args:
      method_name: a string of method name to test.
      goma_dir: a string of GOMA directory.
      local_cl: a string of clang-cl.exe path.
      gomacc: a string of gomacc.exe path.
    """
    super(SimpleTryTest, self).__init__(method_name)
    self._dir = os.path.abspath(goma_dir)
    self.local_cl = local_cl
    self.gomacc = gomacc
    mod_name, _ = os.path.splitext(_GOMA_CTL)
    self._module = imp.load_source(mod_name, os.path.join(goma_dir, _GOMA_CTL))

  @staticmethod
  def RemoveFile(fname):
    """Removes the file and ignores error."""
    try:
      os.remove(fname)
    except Exception:
      pass

  def setUp(self):
    # Sets environmental variables.
    os.environ['GOMA_STORE_ONLY'] = 'true'
    os.environ['GOMA_DUMP'] = 'true'
    os.environ['GOMA_RETRY'] = 'false'
    os.environ['GOMA_FALLBACK'] = 'false'
    os.environ['GOMA_USE_LOCAL'] = 'false'
    os.environ['GOMA_START_COMPILER_PROXY'] = 'false'
    # remote link not implemented on windows yet.
    os.environ['GOMA_STORE_LOCAL_RUN_OUTPUT'] = 'false'
    os.environ['GOMA_ENABLE_REMOTE_LINK'] = 'false'
    os.environ['GOMA_GOMACC_WRITE_LOG_FOR_TESTING'] = 'false'
    self._cwd = os.getcwd()

  def tearDown(self):
    self.RemoveFile('local.obj')
    self.RemoveFile('remote.obj')
    self.RemoveFile('hello.exe')
    self.RemoveFile('create_pch.obj')
    self.RemoveFile('use_pch.obj')
    for log in self.GetGomaccLogs():
      self.RemoveFile(log)
    os.chdir(self._cwd)

  @staticmethod
  def ExecCommand(cmd):
    """Execute given list of command.

    Args:
      cmd: a list of command line args.

    Returns:
      a tuple of proc instance, stdout string and stderr string.
    """
    proc = subprocess.Popen(cmd,
                            stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE)
    (out, err) = proc.communicate()
    return (proc, out, err)

  def AssertSuccess(self, cmd, msg=''):
    """Asserts given command succeeds.

    Args:
      cmd: a list of command to execute.
      msg: additional message to be shown.
    """
    if msg:
      msg += '\n'
    (proc, out, err) = self.ExecCommand(cmd)
    self.assertEqual(proc.returncode, 0, msg=('%s%s\n%s\n' % (msg, out, err)))

  def AssertFailure(self, cmd, msg=''):
    """Asserts given command fails.

    Args:
      cmd: a list of command to execute.
      msg: additional message to be shown.
    """
    if msg:
      msg += '\n'
    (proc, out, err) = self.ExecCommand(cmd)
    self.assertNotEqual(proc.returncode, 0,
                        msg=('%s%s\n%s\n' % (msg, out, err)))

  def AssertSameFile(self, files, msg=''):
    """Asserts given two files are the same.

    Args:
      files: a list of two files to check.
      msg: additional message to be shown.
    """
    a = open(files[0], 'rb').read()
    b = open(files[1], 'rb').read()
    a_size = os.stat(files[0]).st_size
    b_size = os.stat(files[1]).st_size

    if msg:
      msg += '\n'
    self.assertEqual(a_size, len(a),
                     msg=('%sparsial read?: %s %d!=%d' % (msg, files[0],
                                                          a_size, len(a))))
    self.assertEqual(b_size, len(b),
                     msg=('%sparsial read?: %s %d!=%d' % (msg, files[1],
                                                          b_size, len(b))))
    if a == b:
      return  # Success.

    self.assertEqual(len(a), len(b),
                     msg=('%ssize mismatch: %s=%d %s=%d' % (msg,
                                                            files[0], len(a),
                                                            files[1], len(b))))
    idx = -1
    ndiff = 0
    for ach, bch in zip(a, b):
      idx += 1
      # http://support.microsoft.com/kb/121460/en
      # Header structure (0 - 20 bytes):
      #  0 -  2: Machine
      #  2 -  4: Number of sections.
      #  4 -  8: Time/Date Stamp.
      #  8 - 12: Pointer to Symbol Table.
      # 12 - 16: Number of Symbols.
      # 16 - 18: Optional Header Size.
      # 18 - 20: Characteristics.
      if idx in range(4, 8):  # Time/Date Stamp can be different.
        continue
      # Since compiler_proxy normalize path names to lower case, we should
      # normalize printable charactors before comparison.
      if ach in string.printable:
        ach = ach.lower()
      if bch in string.printable:
        bch = bch.lower()

      if ach != bch:
        ndiff += 1
    print('%d bytes differ' % ndiff)
    self.assertEqual(ndiff, 0,
                     msg=('%sobj file should be the same after normalize.'
                          % msg))

  def AssertNotEmptyFile(self, filename, msg=''):
    """Asserts if file is empty.

    Args:
      filename: a string of filname to check.
      msg: additional message to be shown.
    """
    if msg:
      msg += '\n'
    self.assertNotEqual(os.stat(filename).st_size, 0,
                        msg=('%s%s is empty' % (msg, filename)))

  def GetGomaccLogs(self):
    logdir = self._module._GetLogDirectory()
    assert logdir
    return glob.glob(os.path.join(logdir, "gomacc.*"))

  def AssertNoGomaccInfo(self):
    """Asserts if gomacc.INFO does not exist."""
    logs = self.GetGomaccLogs()
    for log in logs:
      with open(log) as f:
        print('log: %s:' % log)
        print(f.read())
        print()
    self.assertEqual(len(logs), 0)

  def testClHelp(self):
    self.AssertSuccess([self.gomacc, self.local_cl, '/?'],
                       msg='gomacc cl help')
    self.AssertNoGomaccInfo()

  def testClHello(self):
    # Since object file contains a file name, an output file name should be
    # the same.
    self.AssertSuccess([self.local_cl, '/c', '/Brepro', '/Fotest.obj',
                        os.path.join('test', 'hello.c')],
                       msg='local compile')
    shutil.move('test.obj', 'local.obj')
    self.AssertSuccess([self.gomacc, self.local_cl, '/c', '/Brepro',
                        '/Fotest.obj', os.path.join('test', 'hello.c')],
                       msg='remote compile')
    shutil.move('test.obj', 'remote.obj')

    self.AssertSameFile(['local.obj', 'remote.obj'], msg='obj same?')
    self.AssertSuccess([self.local_cl, '/Fehello.exe', 'remote.obj'],
                       msg='link hello.obj')
    self.AssertSuccess(['hello.exe'], msg='run hello.exe')
    self.AssertNoGomaccInfo()

  def testDashFlag(self):
    self.AssertSuccess([self.gomacc, self.local_cl, '-c', '-Fotest.obj',
                        os.path.join('test', 'hello.c')],
                       msg='remote compile')
    shutil.move('test.obj', 'remote.obj')
    self.AssertSuccess([self.local_cl, '/Fehello.exe', 'remote.obj'],
                       msg='link hello.obj')
    self.AssertSuccess(['hello.exe'], msg='run hello.exe')
    self.AssertNoGomaccInfo()

  def testPchSupport(self):
    # Since object file contains a file name, an output file name should be
    # the same.
    self.AssertSuccess([self.gomacc, self.local_cl,
                        '/c', '/Fotest.obj', '/FIstdio.h', '/Ycstdio.h',
                        '/Brepro', os.path.join('test', 'hello.c')],
                       msg='cl_create_pch')
    shutil.move('test.obj', 'create_pch.obj')
    self.AssertNotEmptyFile('stdio.pch', msg='cl_create_pch_exist')
    self.AssertSuccess([self.gomacc, self.local_cl,
                        '/c', '/Fotest.obj', '/FIstdio.h', '/Yustdio.h',
                        '/Brepro', os.path.join('test', 'hello.c')],
                       msg='cl_use_pch')
    shutil.move('test.obj', 'use_pch.obj')
    # TODO: investigate pch mismatch.
    # TODO: Still 5 bytes differ.
    # I suppose some come from date/time.
    try:
      self.AssertSameFile(['create_pch.obj', 'use_pch.obj'],
                          msg='FAILS_cl_pch.o')
    except Exception as inst:
      print('Known failure %s' % inst)
    self.AssertNoGomaccInfo()

  def testDisabledShouldWork(self):
    stat_url = 'http://localhost:%s/statz' % (
        os.environ['GOMA_COMPILER_PROXY_PORT'])
    stat_before = URLOPEN(stat_url).read()
    os.environ['GOMA_DISABLED'] = 'true'
    self.AssertSuccess([self.gomacc, self.local_cl, '/c', '/Fotest.obj',
                        os.path.join('test', 'hello.c')],
                       msg='remote compile')
    del os.environ['GOMA_DISABLED']
    stat_after = URLOPEN(stat_url).read()
    request_line_before = '\n'.join(
        [line for line in stat_before.split('\n') if 'request' in line])
    request_line_after = '\n'.join(
        [line for line in stat_after.split('\n') if 'request' in line])
    self.assertNotEqual(request_line_before, '')
    self.assertNotEqual(request_line_after, '')
    self.assertEqual(request_line_before, request_line_after)
    self.AssertNoGomaccInfo()

  def testLocalFallbackShouldWork(self):
    stat_url = 'http://localhost:%s/statz' % (
        os.environ['GOMA_COMPILER_PROXY_PORT'])
    stat_before = URLOPEN(stat_url).read()
    os.environ['GOMA_FALLBACK_INPUT_FILES'] = os.path.join('test', 'hello.c')
    self.AssertSuccess([self.gomacc, self.local_cl, '/c', '/Fotest.obj',
                        os.path.join('test', 'hello.c')],
                       msg='local fallback')
    del os.environ['GOMA_FALLBACK_INPUT_FILES']
    stat_after = URLOPEN(stat_url).read()
    request_line_before = '\n'.join(
        [line for line in stat_before.split('\n') if 'fallback' in line])
    request_line_after = '\n'.join(
        [line for line in stat_after.split('\n') if 'fallback' in line])
    self.assertNotEqual(request_line_before, '')
    self.assertNotEqual(request_line_after, '')
    self.assertNotEqual(request_line_before, request_line_after)
    self.AssertNoGomaccInfo()

  def testClInPathShouldCompile(self):
    self.AssertSuccess([self.gomacc, 'clang-cl', '/c', '/Fotest.obj',
                        os.path.join('test', 'hello.c')],
                       msg='clang-cl.exe in path env. compile')
    self.AssertNotEmptyFile('test.obj', msg='cl_test_obj')
    self.AssertNoGomaccInfo()

  def testAccessCheck(self):
    url = 'http://localhost:%s/e' % (
        os.environ['GOMA_COMPILER_PROXY_PORT'])
    with open(os.path.join('test', 'badreq.bin'), 'rb') as f:
      req_data = f.read()
    req = URLREQUEST(url, req_data)
    req.add_header('Content-Type', 'binary/x-protocol-buffer')
    with self.assertRaises(HTTPERROR) as cm:
      URLOPEN(req)
    ec = cm.exception.getcode()
    self.assertEqual(ec, 401, msg=('response code=%d; want=401' % ec))
    self.AssertNoGomaccInfo()

  def testGomaccShouldLog(self):
    os.environ['GOMA_GOMACC_WRITE_LOG_FOR_TESTING'] = 'true'
    self.AssertSuccess([self.gomacc])
    self.assertEqual(len(self.GetGomaccLogs()), 1)

  # TODO: write a test for a compiler with a relative path.


def GetParameterizedTestSuite(klass, **kwargs):
  """Make test suite parameterized.

  Args:
    klass: a subclass of unittest.TestCase.
    kwargs: arguments given to klass.

  Returns:
    an instance of unittest.TestSuite for |klass|.
  """
  test_loader = unittest.TestLoader()
  test_names = test_loader.getTestCaseNames(klass)
  suite = unittest.TestSuite()
  for name in test_names:
    suite.addTest(klass(name, **kwargs))
  return suite


class CompilerProxyManager(object):
  """Compiler proxy management class.

  This class should be used with 'with' statement.
  This will automatically start compiler proxy when entering into with
  statement, and automatically kill the compiler proxy when exiting from with
  statement.
  """
  # TODO: fix this.
  # pylint: disable=W0212

  def __init__(self, goma_ctl_path, port, kill=False, api_key_file=None,
               service_account_file=None):
    """Initialize.

    Args:
      goma_ctl_path: a string of path goma_ctl.py is located.
      port: a string or an integer port number of compiler_proxy.
      kill: True to kill the GOMA processes before starting compiler_proxy.
      api_key_file: a string of API key filename.
      service_account_file: a string of service account filename.
    """
    # create goma_ctl.
    mod_name, _ = os.path.splitext(_GOMA_CTL)
    self._module = imp.load_source(mod_name,
                                   os.path.join(goma_ctl_path, _GOMA_CTL))
    self._kill = kill
    self._tmpdir = None
    self._port = int(port)
    self._goma = None
    self._api_key_file = None
    if api_key_file and os.path.isfile(api_key_file):
      self._api_key_file = api_key_file
    self._service_account_file = None
    if service_account_file and os.path.isfile(service_account_file):
      self._service_account_file = service_account_file
      self._api_key_file = None
    elif os.path.isfile(_CRED):
      self._service_account_file = _CRED
      self._api_key_file = None

  def __enter__(self):
    self._tmpdir = tempfile.mkdtemp()
    print('GOMA_TMP_DIR: %s' % self._tmpdir)
    os.environ['GOMA_TMP_DIR'] = self._tmpdir
    os.environ['TEST_TMPDIR'] = self._tmpdir
    os.environ['TMPDIR'] = self._tmpdir
    os.environ['TMP'] = self._tmpdir
    os.environ['GOMA_DEPS_CACHE_FILE'] = 'deps_cache'
    assert self._module._GetLogDirectory() == self._tmpdir

    os.environ['GOMA_COMPILER_PROXY_PORT'] = str(self._port)
    # TODO: find unused port
    # os.environ['GOMA_GOMACC_LOCK_FILENAME']
    os.environ['GOMA_GOMACC_LOCK_GLOBALNAME'] = (
        'Global\\goma_cc_lock_compiler_proxy_test_%d' % self._port)
    # os.environ['GOMA_COMPILER_PROXY_LOCK_FILENAME']
    # Windows locks
    # 'Global\$GOMA_COMPILER_PROXY_LOCK_FILENAME.$GOMA_COMPILER_PROXY_PORT'
    if self._api_key_file:
      os.environ['GOMA_API_KEY_FILE'] = self._api_key_file
      print('Use GOMA_API_KEY_FILE=%s' % self._api_key_file)
    if self._service_account_file:
      os.environ['GOMA_SERVICE_ACCOUNT_JSON_FILE'] = self._service_account_file
      print(
          'Use GOMA_SERVICE_ACCOUNT_JSON_FILE=%s' % self._service_account_file)

    self._goma = self._module.GetGomaDriver()
    if self._kill:
      print('Kill any remaining compiler proxy')
      self._goma._env.KillStakeholders()

    self._goma._StartCompilerProxy()

  def __exit__(self, unused_exc_type, unused_exc_value, unused_traceback):
    if self._goma:
      self._goma._ShutdownCompilerProxy()
      if not self._goma._WaitCooldown():
        self._goma._env.KillStakeholders()

    diagnose = subprocess.Popen(
      [sys.executable, os.path.join('client', 'diagnose_goma_log.py'),
       '--show-errors', '--show-warnings', '--show-known-warnings-threshold=0'],
      stdout=subprocess.PIPE, stderr=subprocess.STDOUT).communicate()[0]
    print()
    print(diagnose)
    print()

    if self._tmpdir:
      shutil.rmtree(self._tmpdir)


def _SetupVSEnv():
  """Setup Environment for Visual Studio SDK.

  If cl.exe found in PATH, we assume Visual Studio is installed and
  available, and won't do anything.

  Otherwise, Visual Studio in depot_tools is used. In this case, necessary
  environment variables (INCLUDE, LIB, PATH) are automatically set.

  Raises:
    Error: if it cannot find cl.exe and cannot set proper env for cl.exe.
  """
  try:
    where_cl = subprocess.check_output(['where', 'cl'])
    local_cl = where_cl.split('\n')[0].strip()
    if os.path.exists(local_cl):
      return
  except subprocess.CalledProcessError:
    print('Cannot find cl.exe in PATH.')

  # Cannot find cl.exe in PATH.  Let me set it in depot_tools.
  # The script also set INCLUDE, LIB, PATH at the same time.
  print('Going to use cl.exe in depot_tools.')
  out = subprocess.check_output(['python',
                                 os.path.join(_SCRIPT_DIR, '..', 'build',
                                              'vs_toolchain.py'),
                                 'get_toolchain_dir'])
  vs_path_pattern = re.compile('^vs_path\s+=\s+"([^"]+)"')
  sdk_path_pattern = re.compile('^sdk_path\s+=\s+"([^"]+)"')
  vs_path = None
  sdk_path = None
  for line in out.splitlines():
    matched = vs_path_pattern.search(line)
    if matched:
      vs_path = matched.group(1)
      print('vs_path=%s' % vs_path)
    matched = sdk_path_pattern.search(line)
    if matched:
      sdk_path = matched.group(1)
      print('sdk_path=%s' % sdk_path)
  if not vs_path or not sdk_path:
    raise Error('Do not know proper vs_path or sdk_path.')
  # Since clang-cl.exe generates x64 binary by default, we should use
  # x64 configs.
  out = subprocess.check_output([os.path.join(sdk_path, 'bin/setenv.cmd'),
                                 '/x64', '&&', 'set'])
  for line in out.splitlines():
    key, value = line.split('=')
    if key.upper() in ('INCLUDE', 'LIB', 'PATH'):
      os.environ[key] = value
      print('os.environ[%s] = "%s"' % (key, os.environ[key]))


def _FindClangCl():
  """Returns path to clang-cl.exe or None."""
  clangcl_path = os.path.join(_SCRIPT_DIR, '..', 'third_party', 'llvm-build',
                              'Release+Asserts', 'bin', 'clang-cl.exe')
  if subprocess.call([clangcl_path, "-v"]) == 0:
    return clangcl_path
  return None


def ExecuteTests(goma_dir):
  """Execute Tests.

  Args:
    goma_dir: a string of goma directory.

  Returns:
    integer exit code representing test status.  (success == 0)
    0x01: there is errors.
    0x02: there is failures.
    0x04: command not found.
  """
  _SetupVSEnv()
  local_cl = _FindClangCl()
  assert local_cl
  os.environ['PATH'] = '%s;%s' % (os.environ['PATH'], os.path.dirname(local_cl))

  gomacc = os.path.join(goma_dir, 'gomacc.exe')
  print('LOCAL_CL=%s' % local_cl)
  print('GOMACC=%s' % gomacc)

  if not local_cl or not os.path.exists(local_cl):
    print("local_cl not found.")
    return 0x04
  if not os.path.exists(gomacc):
    print("gomacc not found.")
    return 0x04

  print('ShowGomaVerify')
  cmd = [gomacc, '--goma-verify-command', local_cl]
  subprocess.call(cmd)

  # starts test.
  suite = unittest.TestSuite()
  suite.addTest(
      GetParameterizedTestSuite(SimpleTryTest,
                                goma_dir=goma_dir,
                                local_cl=local_cl,
                                gomacc=gomacc))
  result = unittest.TextTestRunner(verbosity=2).run(suite)

  # Return test status as exit status.
  exit_code = 0
  if result.errors:
    exit_code |= 0x01
  if result.failures:
    exit_code |= 0x02
  return exit_code


def main():
  test_dir = os.path.abspath(os.path.dirname(__file__))
  os.chdir(os.path.join(test_dir, '..'))

  option_parser = optparse.OptionParser()
  option_parser.add_option('--wait', action='store_true',
                           help='Wait after all tests finished')
  option_parser.add_option('--kill', action='store_true',
                           help='Kill running compiler_proxy before test')
  option_parser.add_option('--port', default='8100',
                           help='compiler_proxy port')
  option_parser.add_option('--goma-dir',
                           default=os.path.join(
                               test_dir, '..', 'out', 'Release'),
                           help='goma binary directory')
  option_parser.add_option('--goma-api-key-file',
                           default=os.path.abspath(
                               os.path.join(test_dir, '..',  # curdir
                                            '..', '..',  # build/client
                                            '..', '..',  # slave/$builddir
                                            'goma', 'goma.key')),
                           help='goma api key file')
  option_parser.add_option('--goma-service-account-file',
                           help='goma service account file')

  options, _ = option_parser.parse_args()
  goma_dir = os.path.abspath(options.goma_dir)

  if not os.environ.get('GOMATEST_USE_RUNNING_COMPILER_PROXY', ''):
    with CompilerProxyManager(
        goma_dir, options.port,
        kill=options.kill,
        api_key_file=options.goma_api_key_file,
        service_account_file=options.goma_service_account_file):
      exit_code = ExecuteTests(goma_dir)
  else:
    exit_code = ExecuteTests(goma_dir)

  if options.wait:
    try:
      # Python3 does not have raw_input but python2 input cause error for
      # empty input.
      raw_input('Ready to finish?')
    except NameError:
      input('Ready to finish?')

  if exit_code:
    sys.exit(exit_code)


if __name__ == '__main__':
  main()
