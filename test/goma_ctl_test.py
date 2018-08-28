#!/usr/bin/env python

# Copyright 2012 The Goma Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Tests for goma_ctl."""



import imp
import json
import optparse
import os
import shutil
import stat
import string
import StringIO
import sys
import tempfile
import time
import unittest

_GOMA_CTL = 'goma_ctl.py'


class PlatformSpecific(object):
  """class for platform specific commands / data."""

  def __init__(self, platform):
    self._platform = platform

  def GetPlatform(self):
    """Returns platform name."""
    return self._platform

  @staticmethod
  def GetDefaultGomaCtlPath(test_dir):
    """Returns platform name.

    Args:
      test_dir: a string of directory of this file.

    Returns:
      a string of the directory contains goma_ctl.py by default.
    """
    raise NotImplementedError('GetDefaultGomaCtlPath should be implemented.')

  @staticmethod
  def SetCompilerProxyEnv(tmp_dir, port):
    """Configure compiler_proxy env.

    Args:
      tmp_dir: a string of temporary directory path.
      port: an integer of compiler proxy port.
    """
    raise NotImplementedError('GetDefaultGomaCtlPath should be implemented.')

  def GetCred(self):
    if os.path.isfile(self._CRED):
      return self._CRED
    return None


class WindowsSpecific(PlatformSpecific):
  """class for Windows specific commands / data."""

  _CRED = 'c:\\creds\\service_accounts\\service-account-goma-client.json'

  @staticmethod
  def GetDefaultGomaCtlPath(test_dir):
    return os.path.join(test_dir, '..', 'out', 'Release')

  @staticmethod
  def SetCompilerProxyEnv(tmp_dir, port):
    os.environ['GOMA_TMP_DIR'] = tmp_dir
    os.environ['GOMA_COMPILER_PROXY_PORT'] = str(port)


class PosixSpecific(PlatformSpecific):
  """class for Windows specific commands / data."""

  _CRED = '/creds/service_accounts/service-account-goma-client.json'

  @staticmethod
  def GetDefaultGomaCtlPath(test_dir):
    return os.path.join(test_dir, '..', 'out', 'Release')

  @staticmethod
  def SetCompilerProxyEnv(tmp_dir, port):
    os.environ['GOMA_TMP_DIR'] = tmp_dir
    os.environ['GOMA_COMPILER_PROXY_PORT'] = str(port)


def GetPlatformSpecific(platform):
  """Get PlatformSpecific class for |platform|.

  Args:
    platform: platform name to be returned.

  Returns:
    an instance of a subclass of PlatformSpecific class.

  Raises:
    ValueError: if platform is None or not supported.
  """
  if platform in ('win', 'win64'):
    return WindowsSpecific(platform)
  elif platform in ('linux', 'mac', 'goobuntu', 'chromeos'):
    return PosixSpecific(platform)
  raise ValueError('You should specify supported platform name.')


class FakeGomaEnv(object):
  """Fake GomaEnv class for test."""
  # pylint: disable=R0201
  # pylint: disable=W0613

  def AutoUpdate(self):
    pass

  def BackupCurrentPackage(self, backup_dir='dummy'):
    pass

  def CalculateChecksum(self, _, update_dir=''):
    return 'dummy_checksum'

  def CanAutoUpdate(self):
    return True

  def CheckConfig(self):
    pass

  def ControlCompilerProxy(self, command, check_running=True, need_pids=False):
    if command == '/healthz':
      return {'status': True, 'message': 'ok', 'url': 'dummy_url',
              'pid': 'unknown'}
    return {'status': True, 'message': 'dummy', 'url': 'dummy_url',
            'pid': 'unknown'}

  def CompilerProxyRunning(self):
    return True

  def ExecCompilerProxy(self):
    pass

  def ExtractPackage(self, src, dst):
    return True

  @staticmethod
  def FindLatestLogFile(command_name, log_type):
    return 'dummy_info'

  def GetCacheDirectory(self):
    return 'dummy_cache_dir'

  def GetCrashDumpDirectory(self):
    return 'dummy_crash_dump_dir'

  def GetCrashDumps(self):
    return []

  def GetCompilerProxyVersion(self):
    return 'fake@version'

  def GetGomaCtlScriptName(self):
    return 'fake-script'

  def GetGomaTmpDir(self):
    return 'dummy_tmp_dir'

  def GetPackageName(self):
    return 'goma-fake'

  def GetPlatform(self):
    return 'fake'

  def GetScriptDir(self):
    return 'fake'

  def GetUsername(self):
    return 'fakeuser'

  def HttpDownload(self, url,
                   rewrite_url=None, headers=None, destination_file=None):
    if destination_file:
      return
    if 'MANIFEST' in url:
      return 'VERSION=1'
    return 'fake'

  def InstallPackage(self, _):
    return True

  def IsDirectoryExist(self, _):
    return True

  def EnsureDirectoryOwnedByUser(self, _):
    return True

  def IsGomaInstalledBefore(self):
    return False

  def IsOldFile(self, _):
    return True

  def IsProductionBinary(self):
    return True

  def IsValidManifest(self, _):
    return False

  def IsValidMagic(self, _):
    return True

  def KillStakeholders(self):
    pass

  def LoadChecksum(self, update_dir=''):
    return {}

  def MakeDirectory(self, _):
    pass

  def MayUsingDefaultIPCPort(self):
    return True

  def IsManifestModifiedRecently(self, directory='', threshold=4*60*60):
    return False

  def ReadManifest(self, path=''):
    if path:
      return {'VERSION': 1}
    return {}

  def RemoveDirectory(self, _):
    pass

  def WriteFile(self, filename, content):
    pass

  def CopyFile(self, from_file, to_file):
    pass

  def MakeTgzFromDirectory(self, dir_name, output_filename):
    pass

  def RemoveFile(self, _):
    pass

  def RollbackUpdate(self, backup_dir='dummy'):
    pass

  def SetDefaultKey(self, protocol):
    pass


  def WarnNonProtectedFile(self, filename):
    pass

  def WriteManifest(self, manifest, filename=''):
    pass


class FakeGomaBackend(object):
  """Fake GomaBackend class for test."""
  # pylint: disable=R0201
  # pylint: disable=W0613

  def GetDownloadBaseUrl(self):
    return 'https://example.com'

  def RewriteRequest(self, req):
    return req

  def GetHeaders(self):
    return {}


def _ClearGomaEnv():
  """Clear goma-related environmental variables."""
  to_delete = []
  for e in os.environ:
    if e.startswith('GOMA_'):
      to_delete.append(e)
  for e in to_delete:
    del os.environ[e]
  if os.environ.has_key('GOMAMODE'):
    del os.environ['GOMAMODE']
  if os.environ.has_key('PLATFORM'):
    del os.environ['PLATFORM']

  proxy_env_names = ['HTTP_PROXY', 'http_proxy', 'HTTPS_PROXY', 'https_proxy']
  for proxy_env_name in proxy_env_names:
    if os.environ.has_key(proxy_env_name):
      del os.environ[proxy_env_name]


class GomaCtlTestCommon(unittest.TestCase):
  """Common features for goma_ctl.py test."""
  # test should be able to access protected members and variables.
  # pylint: disable=W0212

  _TMP_SUBDIR_NAME = 'goma'

  def __init__(self, method_name, goma_ctl_path, platform_specific):
    """Initialize GomaCtlTest.

    To be ready for accidentally write files in a test, initializer will
    create a directory for test.

    Args:
      method_name: a string of test method name to execute.
      goma_ctl_path: a string of goma directory name.
      platform_specific: a object for providing platform specific behavior.
    """
    super(GomaCtlTestCommon, self).__init__(method_name)
    self._goma_ctl_path = goma_ctl_path
    self._platform_specific = platform_specific

  def setUp(self):
    _ClearGomaEnv()

    # suppress stdout and make it available from test.
    sys.stdout = StringIO.StringIO()

    mod_name, _ = os.path.splitext(_GOMA_CTL)
    # Copy GOMA client commands to a temporary directory.
    # The directory should be removed at tearDown.
    # TODO: copy same files as archive.py?
    self._tmp_dir = tempfile.mkdtemp()
    shutil.copytree(self._goma_ctl_path,
                    os.path.join(self._tmp_dir, self._TMP_SUBDIR_NAME),
                    symlinks=True,
                    ignore=shutil.ignore_patterns('lib', 'lib.target', 'obj',
                                                  'obj.*', '*_unittest*',
                                                  '*proto*', '.deps'))
    self._module = imp.load_source(mod_name,
                                   os.path.join(self._tmp_dir,
                                                self._TMP_SUBDIR_NAME,
                                                _GOMA_CTL))

  def tearDown(self):
    _ClearGomaEnv()
    shutil.rmtree(self._tmp_dir)


class GomaCtlSmallTest(GomaCtlTestCommon):
  """Small tests for goma_ctl.py.

  All tests in this class use test doubles and do not expected to affect
  external environment.
  """
  # test should be able to access protected members and variables.
  # pylint: disable=W0212

  def setUp(self):
    super(GomaCtlSmallTest, self).setUp()
    # Since we use test doubles, we do not have to wait.
    self._module._COOLDOWN_SLEEP = 0

  def CreateSpyControlCompilerProxy(self):
    class SpyGomaEnv(FakeGomaEnv):
      """Spy GomaEnv to capture ControlCompilerProxy command."""

      def __init__(self):
        super(SpyGomaEnv, self).__init__()
        self.command = ''

      def ControlCompilerProxy(self, command, check_running=True,
                               need_pids=False):
        self.command = command
        return super(SpyGomaEnv, self).ControlCompilerProxy(command,
                                                            check_running,
                                                            need_pids)
    return SpyGomaEnv()

  def testIsGomaFlagTrueShouldShowTrueForVariousTruePatterns(self):
    flag_test_name = 'FLAG_TEST'
    self.assertFalse(self._module._IsGomaFlagTrue(flag_test_name))
    os.environ['GOMA_%s' % flag_test_name] = 'T'
    self.assertTrue(self._module._IsGomaFlagTrue(flag_test_name))
    os.environ['GOMA_%s' % flag_test_name] = 'true'
    self.assertTrue(self._module._IsGomaFlagTrue(flag_test_name))
    os.environ['GOMA_%s' % flag_test_name] = 'y'
    self.assertTrue(self._module._IsGomaFlagTrue(flag_test_name))
    os.environ['GOMA_%s' % flag_test_name] = 'Yes'
    self.assertTrue(self._module._IsGomaFlagTrue(flag_test_name))
    os.environ['GOMA_%s' % flag_test_name] = '1'
    self.assertTrue(self._module._IsGomaFlagTrue(flag_test_name))

  def testIsGomaFlagTrueShouldShowFalseForVariousFalsePatterns(self):
    flag_test_name = 'FLAG_TEST'
    os.environ['GOMA_%s' % flag_test_name] = 'F'
    self.assertFalse(self._module._IsGomaFlagTrue(flag_test_name))
    os.environ['GOMA_%s' % flag_test_name] = 'false'
    self.assertFalse(self._module._IsGomaFlagTrue(flag_test_name))
    os.environ['GOMA_%s' % flag_test_name] = 'n'
    self.assertFalse(self._module._IsGomaFlagTrue(flag_test_name))
    os.environ['GOMA_%s' % flag_test_name] = 'No'
    self.assertFalse(self._module._IsGomaFlagTrue(flag_test_name))
    os.environ['GOMA_%s' % flag_test_name] = '0'
    self.assertFalse(self._module._IsGomaFlagTrue(flag_test_name))

  def testIsGomaFlagTrueShouldFollowDefaultIfEnvNotSet(self):
    flag_test_name = 'FLAG_TEST'
    self.assertFalse(self._module._IsGomaFlagTrue(flag_test_name,
                                                  default=False))
    self.assertTrue(self._module._IsGomaFlagTrue(flag_test_name, default=True))

  def testSetGomaFlagDefaultValueIfEmptyShouldSetIfEmpty(self):
    flag_test_name = 'FLAG_TEST'
    flag_test_value = 'test'
    self.assertFalse(os.environ.has_key('GOMA_%s' % flag_test_name))
    self._module._SetGomaFlagDefaultValueIfEmpty(flag_test_name,
                                                 flag_test_value)
    self.assertTrue(os.environ.has_key('GOMA_%s' % flag_test_name))
    self.assertEqual(os.environ['GOMA_%s' % flag_test_name], flag_test_value)

  def testSetGomaFlagDefaultValueIfEmptyShouldNotSetIfNotEmpty(self):
    flag_test_name = 'FLAG_TEST'
    flag_test_value = 'test'
    flag_orig_value = 'original'
    os.environ['GOMA_%s' % flag_test_name] = flag_orig_value
    self._module._SetGomaFlagDefaultValueIfEmpty(flag_test_name,
                                                 flag_test_value)
    self.assertEqual(os.environ['GOMA_%s' % flag_test_name], flag_orig_value)

  def testParseManifestContentsShouldReturnEmptyForEmptyLine(self):
    self.assertEqual(self._module._ParseManifestContents(''), {})

  def testParseManifestContentsShouldParseOneLine(self):
    parsed = self._module._ParseManifestContents('key=val')
    self.assertEqual(len(parsed.keys()), 1)
    self.assertTrue(parsed.has_key('key'))
    self.assertEqual(parsed['key'], 'val')

  def testParseManifestContentsShouldParseMultipleLines(self):
    parsed = self._module._ParseManifestContents('key0=val0\nkey1=val1')
    self.assertEqual(len(parsed.keys()), 2)
    self.assertTrue(parsed.has_key('key0'))
    self.assertEqual(parsed['key0'], 'val0')
    self.assertTrue(parsed.has_key('key1'))
    self.assertEqual(parsed['key1'], 'val1')

  def testParseManifestContentsShouldShowEmptyValueIfEndWithEqual(self):
    parsed = self._module._ParseManifestContents('key=')
    self.assertEqual(len(parsed.keys()), 1)
    self.assertTrue(parsed.has_key('key'))
    self.assertEqual(parsed['key'], '')

  def testParseManifestContentsShouldParseLineWithMultipleEquals(self):
    parsed = self._module._ParseManifestContents('key=label=value')
    self.assertEqual(len(parsed.keys()), 1)
    self.assertTrue(parsed.has_key('key'))
    self.assertEqual(parsed['key'], 'label=value')

  def testParseManifestContentsShouldIgnoreLineWitoutEquals(self):
    parsed = self._module._ParseManifestContents('key')
    self.assertEqual(len(parsed.keys()), 0)
    self.assertFalse(parsed.has_key('key'))

  def testIsBadVersionReturnsFalseForEmptyBadVersion(self):
    self.assertFalse(self._module._IsBadVersion(1, ''))

  def testIsBadVersionForExactMatch(self):
    self.assertTrue(self._module._IsBadVersion(1, '1'))

  def testIsBadVersionReturnsFalseForPartialMatchInCurVer(self):
    self.assertFalse(self._module._IsBadVersion(10, '1'))
    self.assertFalse(self._module._IsBadVersion(21, '1'))

  def testIsBadVersionInList(self):
    self.assertTrue(self._module._IsBadVersion(67, '65|67'))
    self.assertTrue(self._module._IsBadVersion(67, '65|67|69'))
    self.assertTrue(self._module._IsBadVersion(67, '67|69'))

  def testIsBadVersionReturnsFalseNotInList(self):
    self.assertFalse(self._module._IsBadVersion(5, '65|67|69'))
    self.assertFalse(self._module._IsBadVersion(66, '65|67'))
    self.assertFalse(self._module._IsBadVersion(56, '65|67|69'))
    self.assertFalse(self._module._IsBadVersion(6, '65|67|69'))
    self.assertFalse(self._module._IsBadVersion(7, '65|67|69'))
    self.assertFalse(self._module._IsBadVersion(76, '65|67|69'))
    self.assertFalse(self._module._IsBadVersion(9, '65|67|69'))

  def testShouldUpdateWithNoBadVersion(self):
    self.assertTrue(self._module._ShouldUpdate(1, 2, ''))
    self.assertTrue(self._module._ShouldUpdate(1, 3, ''))
    self.assertTrue(self._module._ShouldUpdate(66, 67, ''))

  def tesstShouldUpdateReturnsFalseForDowngradeWithNoBadVersion(self):
    self.assertFalse(self._module._ShouldUpdate(1, 1, ''))
    self.assertFalse(self._module._ShouldUpdate(2, 1, ''))
    self.assertFalse(self._module._ShouldUpdate(3, 1, ''))
    self.assertFalse(self._module._ShouldUpdate(67, 66, ''))

  def testShouldUpdateFromBadVersion(self):
    self.assertTrue(self._module._ShouldUpdate(67, 68, '67'))

  def testShouldUpdateForDowngradeFromBadVersion(self):
    self.assertTrue(self._module._ShouldUpdate(2, 1, '2'))
    self.assertTrue(self._module._ShouldUpdate(2, 1, '0|2'))
    self.assertTrue(self._module._ShouldUpdate(2, 1, '2|3'))
    self.assertTrue(self._module._ShouldUpdate(2, 1, '0|2|3'))
    self.assertTrue(self._module._ShouldUpdate(67, 66, '67'))

  def testShouldUpdateWithDifferentBadVersion(self):
    self.assertTrue(self._module._ShouldUpdate(66, 68, '67'))

  def testShouldUpdateReturnsFalseForDowngradeWithDifferentBadVersion(self):
    self.assertFalse(self._module._ShouldUpdate(2, 1, '3'))

  def testShouldUpdateReturnsFalseForTheSameVersion(self):
    self.assertFalse(self._module._ShouldUpdate(1, 1, '1'))

  def testParseSpaceSeparatedValuesShouldParse(self):
    test = (
        'COMMAND PID\n'
        'bash      1\n'
        'tcsh      2\n'
        )
    expected = [
        {'COMMAND': 'bash', 'PID': '1'},
        {'COMMAND': 'tcsh', 'PID': '2'},
        ]
    parsed = self._module._ParseSpaceSeparatedValues(test)
    self.assertEqual(parsed, expected)

  def testParseSpaceSeparatedValuesShouldParseEmpty(self):
    parsed = self._module._ParseSpaceSeparatedValues('')
    self.assertEqual(parsed, [])

  def testParseSpaceSeparatedValuesShouldSkipBlankLines(self):
    test = (
        'COMMAND PID\n'
        'bash      1\n'
        'tcsh      2\n'
        '\n'
        )
    expected = [
        {'COMMAND': 'bash', 'PID': '1'},
        {'COMMAND': 'tcsh', 'PID': '2'},
        ]
    parsed = self._module._ParseSpaceSeparatedValues(test)
    self.assertEqual(parsed, expected)

  def testParseSpaceSeparatedValuesShouldIgnoreWhiteSpaces(self):
    test = (
        'COMMAND                           PID            \n'
        '   bash \t     1  \n'
        '  \t  \n'
        '\ttcsh    \t  2\t\n'
        '\n'
        )
    expected = [
        {'COMMAND': 'bash', 'PID': '1'},
        {'COMMAND': 'tcsh', 'PID': '2'},
        ]
    parsed = self._module._ParseSpaceSeparatedValues(test)
    self.assertEqual(parsed, expected)

  def testParseLsofShouldParse(self):
    test = ('u1\n'
            'p2\n'
            'nlocalhost:8088\n'
            'u3\n'
            'p4\n'
            'n/tmp/foo.txt\n')
    expected = [{'uid': 1L, 'pid': 2L, 'name': 'localhost:8088'},
                {'uid': 3L, 'pid': 4L, 'name': '/tmp/foo.txt'}]
    parsed = self._module._ParseLsof(test)
    self.assertEqual(parsed, expected)

  def testParseLsofShouldIgnoreUnknown(self):
    test = ('x\n'
            'y\n'
            'u1\n'
            'p2\n'
            'nlocalhost:8088\n')
    expected = [{'uid': 1L, 'pid': 2L, 'name': 'localhost:8088'}]
    parsed = self._module._ParseLsof(test)
    self.assertEqual(parsed, expected)

  def testParseLsofShouldIgnoreEmptyLine(self):
    test = ('\n'
            '\t\t\n'
            '  \n'
            'u1\n'
            'p2\n'
            'nlocalhost:8088\n')
    expected = [{'uid': 1L, 'pid': 2L, 'name': 'localhost:8088'}]
    parsed = self._module._ParseLsof(test)
    self.assertEqual(parsed, expected)

  def testParseLsofShouldIgnoreTypeStream(self):
    test = ('u1\n'
            'p2\n'
            'n/tmp/goma.ipc type=STREAM\n')
    expected = [{'uid': 1L, 'pid': 2L, 'name': '/tmp/goma.ipc'}]
    parsed = self._module._ParseLsof(test)
    self.assertEqual(parsed, expected)

  def testGetEnvMatchedConditionShouldReturnForEmptyCandidates(self):
    default_value = 'default'
    result = self._module._GetEnvMatchedCondition([],
                                                  lambda x: True,
                                                  default_value)
    self.assertEqual(result, default_value)

  def testGetEnvMatchedConditionShouldReturnIfNothingMatched(self):
    default_value = 'default'
    flag_test_name = 'FLAG_TEST'
    flag_value = 'not matched value'
    os.environ['GOMA_%s' % flag_test_name] = flag_value
    result = self._module._GetEnvMatchedCondition(['GOMA_%s' % flag_test_name],
                                                  lambda x: False,
                                                  default_value)
    self.assertEqual(result, default_value)

  def testGetEnvMatchedConditionShouldReturnMatchedValue(self):
    default_value = 'default'
    flag_test_name = 'FLAG_TEST'
    flag_value = 'expected_value'
    os.environ['GOMA_%s' % flag_test_name] = flag_value
    result = self._module._GetEnvMatchedCondition(['GOMA_%s' % flag_test_name],
                                                  lambda x: True,
                                                  default_value)
    self.assertEqual(result, flag_value)

  def testGetEnvMatchedConditionShouldReturnTheFirstCandidate(self):
    default_value = 'default'
    flag_test_name_1 = 'FLAG_TEST_1'
    flag_value_1 = 'value_01'
    os.environ['GOMA_%s' % flag_test_name_1] = flag_value_1
    flag_test_name_2 = 'FLAG_TEST_2'
    flag_value_2 = 'value_02'
    os.environ['GOMA_%s' % flag_test_name_2] = flag_value_2
    result = self._module._GetEnvMatchedCondition(
        ['GOMA_%s' % i for i in [flag_test_name_1, flag_test_name_2]],
        lambda x: True, default_value)
    self.assertEqual(result, flag_value_1)

  def testGetEnvMatchedConditionShouldReturnEarlierCandidateInList(self):
    default_value = 'default'
    flag_name_1 = 'FLAG_TEST_1'
    flag_value_1 = 'value_01'
    os.environ['GOMA_%s' % flag_name_1] = flag_value_1
    flag_name_2 = 'FLAG_TEST_2'
    flag_value_2 = 'match_02'
    os.environ['GOMA_%s' % flag_name_2] = flag_value_2
    flag_name_3 = 'FLAG_TEST_3'
    flag_value_3 = 'match_03'
    os.environ['GOMA_%s' % flag_name_3] = flag_value_3
    result = self._module._GetEnvMatchedCondition(
        ['GOMA_%s' % i for i in [flag_name_1, flag_name_2, flag_name_3]],
        lambda x: x.startswith('match'), default_value)
    self.assertEqual(result, flag_value_2)

  def testParseFlagzShouldParse(self):
    test_data = ('GOMA_COMPILER_PROXY_DAEMON_MODE=true\n'
                 'GOMA_COMPILER_PROXY_LOCK_FILENAME=goma_compiler_proxy.lock\n')
    parsed_data = self._module._ParseFlagz(test_data)
    expected = {
        'GOMA_COMPILER_PROXY_DAEMON_MODE': 'true',
        'GOMA_COMPILER_PROXY_LOCK_FILENAME': 'goma_compiler_proxy.lock',
    }
    self.assertEqual(parsed_data, expected)

  def testParseFlagzShouldIgnoreAutoConfigured(self):
    test_data = ('GOMA_BURST_MAX_SUBPROCS=64 (auto configured)\n'
                 'GOMA_COMPILER_PROXY_LOCK_FILENAME=goma_compiler_proxy.lock\n')
    parsed_data = self._module._ParseFlagz(test_data)
    expected = {
        'GOMA_COMPILER_PROXY_LOCK_FILENAME': 'goma_compiler_proxy.lock',
    }
    self.assertEqual(parsed_data, expected)

  def testParseFlagzShouldIgnoreNewlineOnlyLine(self):
    test_data = ('\n'
                 'GOMA_COMPILER_PROXY_DAEMON_MODE=true\n'
                 '\n\n'
                 '\r\n'
                 'GOMA_COMPILER_PROXY_LOCK_FILENAME=goma_compiler_proxy.lock\n'
                 '\n')
    parsed_data = self._module._ParseFlagz(test_data)
    expected = {
        'GOMA_COMPILER_PROXY_DAEMON_MODE': 'true',
        'GOMA_COMPILER_PROXY_LOCK_FILENAME': 'goma_compiler_proxy.lock',
    }
    self.assertEqual(parsed_data, expected)

  def testParseFlagzShouldIgnoreWhiteSpaces(self):
    test_data = (' \t  GOMA_COMPILER_PROXY_DAEMON_MODE \t = \t true  \n')
    parsed_data = self._module._ParseFlagz(test_data)
    expected = {
        'GOMA_COMPILER_PROXY_DAEMON_MODE': 'true',
    }
    self.assertEqual(parsed_data, expected)

  def testIsGomaFlagUpdatedShouldReturnFalseIfNothingHasSet(self):
    self.assertFalse(self._module._IsGomaFlagUpdated({}))

  def testIsGomaFlagUpdatedShouldReturnTrueIfNewFlag(self):
    os.environ['GOMA_TEST'] = 'test'
    self.assertTrue(self._module._IsGomaFlagUpdated({}))

  def testIsGomaFlagUpdatedShouldReturnTrueIfFlagRemoved(self):
    self.assertTrue(self._module._IsGomaFlagUpdated({'GOMA_TEST': 'test'}))

  def testIsGomaFlagUpdatedShouldReturnFalseIfNoUpdate(self):
    expected = {'GOMA_TEST': 'test'}
    for key, value in expected.iteritems():
      os.environ[key] = value
    self.assertFalse(self._module._IsGomaFlagUpdated(expected))

  def testPullShouldUpdateManifestInLatestDir(self):
    class SpyGomaEnv(FakeGomaEnv):
      """Spy GomaEnv to provide MANIFEST files and capture WriteManifest."""

      def __init__(self):
        super(SpyGomaEnv, self).__init__()
        self.written_manifest = {}
        self._downloaded = False

      def HttpDownload(self, url,
                       rewrite_url=None, headers=None, destination_file=None):
        if 'MANIFEST' in url:
          return 'VERSION=2'
        if destination_file:
          # ReadManifest should show the latest version only when the file has
          # been downloaded.
          self._downloaded = True

      def ReadManifest(self, _=None):
        if self._downloaded:
          return {'VERSION': '2'}
        else:
          return {'VERSION': '1'}

      def WriteManifest(self, manifest, _=None):
        self.written_manifest = manifest

    env = SpyGomaEnv()
    driver = self._module.GomaDriver(env, FakeGomaBackend())
    driver._Pull()
    self.assertTrue(env.written_manifest)
    self.assertEqual(env.written_manifest['PLATFORM'], 'fake')
    self.assertEqual(env.written_manifest['VERSION'], '2')

  def testPullShouldUpdateManifestInLatestDirToRollbackRelease(self):
    class SpyGomaEnv(FakeGomaEnv):
      """Spy GomaEnv to provide MANIFEST files and capture WriteManifest."""

      def __init__(self):
        super(SpyGomaEnv, self).__init__()
        self.written_manifest = {}
        self._downloaded = False

      def HttpDownload(self, url,
                       rewrite_url=None, headers=None, destination_file=None):
        if 'MANIFEST' in url:
          return 'VERSION=1\nbad_version=2'
        if destination_file:
          # ReadManifest should show the latest version only when the file has
          # been downloaded.
          self._downloaded = True

      def ReadManifest(self, _=None):
        if self._downloaded:
          return {'VERSION': '1', 'bad_version': '2'}
        else:
          return {'VERSION': '2'}

      def WriteManifest(self, manifest, _=None):
        self.written_manifest = manifest

    env = SpyGomaEnv()
    driver = self._module.GomaDriver(env, FakeGomaBackend())
    driver._Pull()
    self.assertTrue(env.written_manifest)
    self.assertEqual(env.written_manifest['PLATFORM'], 'fake')
    self.assertEqual(env.written_manifest['VERSION'], '1')

  def testPullShouldUpdateIfFilesAreNotValid(self):
    class SpyGomaEnv(FakeGomaEnv):
      """Spy GomaEnv to provide MANIFEST files and capture WriteManifest."""

      def __init__(self):
        super(SpyGomaEnv, self).__init__()
        self.is_valid_magic = False
        self.writte_manifest = False

      def IsValidMagic(self, _):
        self.is_valid_magic = True
        return False

      def WriteManifest(self, manifest, _=None):
        self.writte_manifest = True

    env = SpyGomaEnv()
    driver = self._module.GomaDriver(env, FakeGomaBackend())
    driver._Pull()
    self.assertTrue(env.is_valid_magic)
    self.assertTrue(env.writte_manifest)

  def testPullShouldUpdateIfManifestIsEmpty(self):
    class SpyGomaEnv(FakeGomaEnv):
      """Spy GomaEnv to provide MANIFEST files and capture WriteManifest."""

      def __init__(self):
        super(SpyGomaEnv, self).__init__()
        self.read_manifest_latest = False
        self.writte_manifest = False
        self._downloaded = False

      def HttpDownload(self, url,
                       rewrite_url=None, headers=None, destination_file=None):
        if 'MANIFEST' in url:
          return 'VERSION=2'
        if destination_file:
          self._downloaded = True

      def ReadManifest(self, latest=None):
        if latest == 'latest':
          self.read_manifest_latest = True
          return {}
        if self._downloaded:
          return {'VERSION': '2'}
        else:
          return {'VERSION': '1'}

      def WriteManifest(self, manifest, _=None):
        self.writte_manifest = True

    env = SpyGomaEnv()
    driver = self._module.GomaDriver(env, FakeGomaBackend())
    driver._Pull()
    self.assertTrue(env.read_manifest_latest)
    self.assertTrue(env.writte_manifest)

  def testPullShouldUpdateIfManifestIsBroken(self):
    class SpyGomaEnv(FakeGomaEnv):
      """Spy GomaEnv to provide MANIFEST files and capture WriteManifest."""

      def __init__(self):
        super(SpyGomaEnv, self).__init__()
        self.read_manifest_latest = True
        self.writte_manifest = False
        self._downloaded = False

      def HttpDownload(self, url,
                       rewrite_url=None, headers=None, destination_file=None):
        if 'MANIFEST' in url:
          return 'VERSION=2'
        if destination_file:
          self._downloaded = True

      def ReadManifest(self, latest=None):
        if latest == 'latest':
          self.read_manifest_latest = True
          return {'VERSION': 'broken'}
        return {'VERSION': '1'}

      def WriteManifest(self, manifest, _=None):
        self.writte_manifest = True

    env = SpyGomaEnv()
    driver = self._module.GomaDriver(env, FakeGomaBackend())
    driver._Pull()
    self.assertTrue(env.read_manifest_latest)
    self.assertTrue(env.writte_manifest)

  def testStartCompilerProxyShouldRun(self):
    driver = self._module.GomaDriver(FakeGomaEnv(), FakeGomaBackend())
    driver._StartCompilerProxy()

  def testGetStatusShouldCallControlCompilerProxyWithHealthz(self):
    env = self.CreateSpyControlCompilerProxy()
    driver = self._module.GomaDriver(env, FakeGomaBackend())
    driver._GetStatus()
    self.assertEqual(env.command, '/healthz')

  def testShutdownCompilerProxyShouldCallControlCompilerProxyWith3Quit(self):
    env = self.CreateSpyControlCompilerProxy()
    driver = self._module.GomaDriver(env, FakeGomaBackend())
    driver._ShutdownCompilerProxy()
    self.assertEqual(env.command, '/quitquitquit')

  def testPrintStatisticsShouldCallControlCompilerProxyWithStatz(self):
    env = self.CreateSpyControlCompilerProxy()
    driver = self._module.GomaDriver(env, FakeGomaBackend())
    driver._PrintStatistics()
    self.assertEqual(env.command, '/statz')

  def testPrintHistogramShouldCallControlCompilerProxyWithStatz(self):
    env = self.CreateSpyControlCompilerProxy()
    driver = self._module.GomaDriver(env, FakeGomaBackend())
    driver._PrintHistogram()
    self.assertEqual(env.command, '/histogramz')

  def testGetJsonStatusShouldCallControlCompilerProxyWithErrorz(self):
    env = self.CreateSpyControlCompilerProxy()
    driver = self._module.GomaDriver(env, FakeGomaBackend())
    driver._GetJsonStatus()
    self.assertEqual(env.command, '/errorz')

  def testPrintLatestVersionShouldRun(self):
    driver = self._module.GomaDriver(FakeGomaEnv(), FakeGomaBackend())
    driver._PrintLatestVersion()

  def testGetProxyEnvShouldReturnEmptyDictIfNoEnvConfigured(self):
    self.assertFalse(self._module._GetProxyEnv())

  def testReportMakeTgz(self):
    class SpyGomaEnv(FakeGomaEnv):
      """Spy GomaEnv to provide WriteFile, CopyFile and MakeTgzFromDirectory"""

      def __init__(self):
        super(SpyGomaEnv, self).__init__()
        self.output_files = []
        self.tgz_source_dir = None
        self.tgz_file = None
        self.written = False
        self.find_latest_info_file = False

      def WriteFile(self, filename, content):
        self.output_files.append(filename)

      def CopyFile(self, from_file, to_file):
        self.output_files.append(to_file)

      @staticmethod
      def FindLatestLogFile(command_name, log_type):
        return None

      def MakeTgzFromDirectory(self, dir_name, output_filename):
        self.tgz_source_dir = dir_name
        self.tgz_file = output_filename
        self.written = True

    env = SpyGomaEnv()
    driver = self._module.GomaDriver(env, FakeGomaBackend())
    driver._Report()
    self.assertTrue(env.written)
    for f in env.output_files:
      self.assertTrue(f.startswith(env.tgz_source_dir))
    self.assertTrue(env.tgz_file.startswith(self._module._GetTempDirectory()))

  def testReportMakeTgzWithoutCompilerProxyRunning(self):
    class SpyGomaEnv(FakeGomaEnv):
      """Spy GomaEnv to provide WriteFile, CopyFile and MakeTgzFromDirectory.
         Also, compiler_proxy is not running in this env."""

      def __init__(self):
        super(SpyGomaEnv, self).__init__()
        self.output_files = []
        self.tgz_source_dir = None
        self.tgz_file = None
        self.written = False

      def ControlCompilerProxy(self, command, check_running=False,
                               need_pids=False):
        if command == '/healthz':
          return {
              'status': False,
              'message': 'compiler proxy is not running',
              'url': 'fake',
              'pid': 'unknown',
          }
        # /compilerz, /histogramz, /serverz, or /statz won't be called.
        if command in ['/compilerz', '/histogramz', '/serverz', '/statz']:
          raise Exception('Unexpected command is called')
        return super(SpyGomaEnv, self).ControlCompilerProxy(command,
                                                            check_running,
                                                            need_pids)

      def WriteFile(self, filename, content):
        self.output_files.append(filename)

      def CopyFile(self, from_file, to_file):
        self.output_files.append(to_file)

      @staticmethod
      def FindLatestLogFile(command_name, log_type):
        return None

      def MakeTgzFromDirectory(self, dir_name, output_filename):
        self.tgz_source_dir = dir_name
        self.tgz_file = output_filename
        self.written = True

    env = SpyGomaEnv()
    driver = self._module.GomaDriver(env, FakeGomaBackend())
    driver._Report()
    self.assertTrue(env.written)
    for f in env.output_files:
      self.assertTrue(f.startswith(env.tgz_source_dir))
    self.assertTrue(env.tgz_file.startswith(self._module._GetTempDirectory()))

  def testReportMakeTgzCompilerProxyDeadAfterHealthz(self):
    class SpyGomaEnv(FakeGomaEnv):
      """Spy GomaEnv to provide WriteFile, CopyFile and MakeTgzFromDirectory.
         compiler_proxy dies after the first /healthz call."""

      def __init__(self):
        super(SpyGomaEnv, self).__init__()
        self.output_files = []
        self.tgz_source_dir = None
        self.tgz_file = None
        self.written = False
        self.is_dead = False

      def ControlCompilerProxy(self, command, check_running=False,
                               need_pids=False):
        if self.is_dead:
          return {
              'status': False,
              'message': 'compiler_proxy is not running',
              'url': 'dummy',
              'pid': 'unknown',
          }
        # Die after /healthz is called. The first /healthz should be
        # processed correctly.
        if command == '/healthz':
          self.is_dead = True
        return super(SpyGomaEnv, self).ControlCompilerProxy(command,
                                                            check_running,
                                                            need_pids)

      def WriteFile(self, filename, content):
        self.output_files.append(filename)

      def CopyFile(self, from_file, to_file):
        self.output_files.append(to_file)

      @staticmethod
      def FindLatestLogFile(command_name, log_type):
        return None

      def MakeTgzFromDirectory(self, dir_name, output_filename):
        self.tgz_source_dir = dir_name
        self.tgz_file = output_filename
        self.written = True

    env = SpyGomaEnv()
    driver = self._module.GomaDriver(env, FakeGomaBackend())
    driver._Report()
    self.assertTrue(env.written)
    for f in env.output_files:
      self.assertTrue(f.startswith(env.tgz_source_dir))
    self.assertTrue(env.tgz_file.startswith(self._module._GetTempDirectory()))

  def testGetProxyEnvShouldReturnDictIfEnvIsSet(self):
    proxy_env_names = ['HTTP_PROXY', 'http_proxy', 'HTTPS_PROXY', 'https_proxy']
    for name in proxy_env_names:
      self.assertFalse(os.environ.has_key(name))
      os.environ[name] = 'http://example.org:3128/'
      proxy_env = self._module._GetProxyEnv()
      self.assertTrue(proxy_env, msg=('proxy env=%s' % name))
      self.assertEqual(proxy_env, {'host': 'example.org', 'port': '3128'})
      del os.environ[name]

  def testGetProxyEnvShouldRaiseForHttps(self):
    os.environ['HTTP_PROXY'] = 'https://example.org:3128/'
    self.assertRaises(self._module.ConfigError, self._module._GetProxyEnv)

  def testGetProxyEnvShouldRaiseForProxyWithPassword(self):
    os.environ['HTTP_PROXY'] = 'http://user:pass@example.org:3128'
    self.assertRaises(self._module.ConfigError, self._module._GetProxyEnv)

  def testGetProxyEnvShouldRaiseEnvWithoutPort(self):
    os.environ['HTTP_PROXY'] = 'http://example.org/'
    self.assertRaises(self._module.ConfigError, self._module._GetProxyEnv)

  def testGetProxyEnvShouldAllowEnvWithoutScheme(self):
    os.environ['HTTP_PROXY'] = 'example.org:3128'
    proxy_env = self._module._GetProxyEnv()
    self.assertTrue(proxy_env)
    self.assertEqual(proxy_env, {'host': 'example.org', 'port': '3128'})

  def testGetProxyEnvShouldRaiseEnvWithoutSchemeAndPort(self):
    os.environ['HTTP_PROXY'] = 'example.org'
    self.assertRaises(self._module.ConfigError, self._module._GetProxyEnv)

  def testAutoUpdate(self):
    class SpyGomaEnv(FakeGomaEnv):
      """Spy GomaEnv to provide MANIFEST files and capture Update called."""

      def __init__(self):
        super(SpyGomaEnv, self).__init__()
        self._downloaded = False
        self.auto_updated = False
        self.read_manifest_before_update = False
        self.read_manifest_after_update = False

      def HttpDownload(self, url,
                       rewrite_url=None, headers=None, destination_file=None):
        return 'VERSION=2'

      def CanAutoUpdate(self):
        return True

      def ReadManifest(self, _=None):
        if self.auto_updated:
          self.read_manifest_after_update = True
          return {'VERSION': '2'}
        self.read_manifest_before_update = True
        return {'VERSION': '1'}

      def AutoUpdate(self):
        self.auto_updated = True

    env = SpyGomaEnv()
    driver = self._module.GomaDriver(env, FakeGomaBackend())
    driver._StartCompilerProxy()
    self.assertTrue(env.auto_updated)
    self.assertTrue(env.read_manifest_before_update)
    self.assertTrue(env.read_manifest_after_update)
    self.assertIn('VERSION', driver._manifest)
    self.assertEqual(driver._manifest['VERSION'], '2')
    self.assertEqual(driver._version, 2)

  def testShouldNotAutoUpdateIfAlreadyUpToDate(self):
    class SpyGomaEnv(FakeGomaEnv):
      def __init__(self):
        super(SpyGomaEnv, self).__init__()
        self.auto_updated = False
        self.http_downloaded = False

      def HttpDownload(self, url,
                       rewrite_url=None, headers=None, destination_file=None):
        self.http_downloaded = True
        return 'VERSION=1'

      def CanAutoUpdate(self):
        return True

      def ReadManifest(self, _=None):
        return {'VERSION': '1'}

      def AutoUpdate(self):
        self.auto_updated = True

    env = SpyGomaEnv()
    driver = self._module.GomaDriver(env, FakeGomaBackend())
    driver._StartCompilerProxy()
    self.assertTrue(env.http_downloaded)
    self.assertFalse(env.auto_updated)

  def testShouldNotAutoUpdateIfCanAutoUpdateIsFalse(self):
    class SpyGomaEnv(FakeGomaEnv):
      def __init__(self):
        super(SpyGomaEnv, self).__init__()
        self.auto_updated = False
        self.http_downloaded = False

      def HttpDownload(self, url,
                       rewrite_url=None, headers=None, destination_file=None):
        self.http_downloaded = True
        return 'VERSION=1'

      def CanAutoUpdate(self):
        return False

      def AutoUpdate(self):
        self.auto_updated = True

    env = SpyGomaEnv()
    driver = self._module.GomaDriver(env, FakeGomaBackend())
    driver._StartCompilerProxy()
    self.assertFalse(env.http_downloaded)
    self.assertFalse(env.auto_updated)

  def testUpdateShouldUpdateIfFindTheNewVersion(self):
    class SpyGomaEnv(FakeGomaEnv):
      def __init__(self):
        super(SpyGomaEnv, self).__init__()
        self.http_downloaded = False
        self.install_package = False
        self.rollback = False

      def HttpDownload(self, url,
                       rewrite_url=None, headers=None, destination_file=None):
        self.http_downloaded = True
        return 'VERSION=1'

      def InstallPackage(self, _):
        self.install_package = True
        return True  # install success.

    env = SpyGomaEnv()
    driver = self._module.GomaDriver(env, FakeGomaBackend())
    driver._Update()
    self.assertTrue(env.http_downloaded)
    self.assertTrue(env.install_package)

  def testUpdateShouldNotUpdateIfCurrentPacakgeIsLatest(self):
    class SpyGomaEnv(FakeGomaEnv):
      def __init__(self):
        super(SpyGomaEnv, self).__init__()
        self.http_downloaded = False
        self.install_package = False
        self.rollback = False

      def HttpDownload(self, url,
                       rewrite_url=None, headers=None, destination_file=None):
        self.http_downloaded = True
        return 'VERSION=1'

      def ReadManifest(self, _=None):
        return {'VERSION': '1'}

      def InstallPackage(self, _):
        self.install_package = True
        return True  # install success.

    env = SpyGomaEnv()
    driver = self._module.GomaDriver(env, FakeGomaBackend())
    driver._Update()
    self.assertTrue(env.http_downloaded)
    self.assertFalse(env.install_package)

  def testUpdateShouldNotUpdateIfCurrentPacakgeIsNewerThanLatest(self):
    class SpyGomaEnv(FakeGomaEnv):
      def __init__(self):
        super(SpyGomaEnv, self).__init__()
        self.http_downloaded = False
        self.manifests = {}
        self.install_package = False
        self.rollback = False

      def HttpDownload(self, url,
                       rewrite_url=None, headers=None, destination_file=None):
        self.http_downloaded = True
        if destination_file:
          dirname = os.path.dirname(destination_file)
          manifest = {'VERSION': '1'}
          self.manifests[dirname] = manifest
          return
        return 'VERSION=1'

      def ReadManifest(self, path=None):
        if path and self.manifests[path]:
          return self.manifests[path]
        return {'VERSION': '2'}

      def InstallPackage(self, _):
        self.install_package = True
        return True  # install success.

    env = SpyGomaEnv()
    driver = self._module.GomaDriver(env, FakeGomaBackend())
    driver._Update()
    self.assertTrue(env.http_downloaded)
    self.assertFalse(env.install_package)

  def testUpdateShouldUpdateIfCurrentPacakgeIsMarkedAsBad(self):
    class SpyGomaEnv(FakeGomaEnv):
      def __init__(self):
        super(SpyGomaEnv, self).__init__()
        self.http_downloaded = False
        self.manifests = {}
        self.install_package = False
        self.rollback = False

      def HttpDownload(self, url,
                       rewrite_url=None, headers=None, destination_file=None):
        self.http_downloaded = True
        if destination_file:
          dirname = os.path.dirname(destination_file)
          manifest = {'VERSION': '1', 'bad_version': '2'}
          self.manifests[dirname] = manifest
          return
        return 'VERSION=1\nbad_version=2'

      def ReadManifest(self, path=None):
        if path and self.manifests[path]:
          return self.manifests[path]
        return {'VERSION': '2'}

      def InstallPackage(self, _):
        self.install_package = True
        return True  # install success.

    env = SpyGomaEnv()
    driver = self._module.GomaDriver(env, FakeGomaBackend())
    driver._Update()
    self.assertTrue(env.http_downloaded)
    self.assertTrue(env.install_package)

  def testUpdateShouldUpdateIfCurrentPacakgeIsMarkedAsOneOfBad(self):
    class SpyGomaEnv(FakeGomaEnv):
      def __init__(self):
        super(SpyGomaEnv, self).__init__()
        self.http_downloaded = False
        self.manifests = {}
        self.install_package = False
        self.rollback = False

      def HttpDownload(self, url,
                       rewrite_url=None, headers=None, destination_file=None):
        self.http_downloaded = True
        if destination_file:
          dirname = os.path.dirname(destination_file)
          manifest = {'VERSION': '1', 'bad_version': '2|3'}
          self.manifests[dirname] = manifest
          return
        return 'VERSION=1\nbad_version=2|3'

      def ReadManifest(self, path=None):
        if path and self.manifests[path]:
          return self.manifests[path]
        return {'VERSION': '3'}

      def InstallPackage(self, _):
        self.install_package = True
        return True  # install success.

    env = SpyGomaEnv()
    driver = self._module.GomaDriver(env, FakeGomaBackend())
    driver._Update()
    self.assertTrue(env.http_downloaded)
    self.assertTrue(env.install_package)

  def testUpdateShouldUpdateIfCurrentPacakgeIsNotMarkedAsBad(self):
    class SpyGomaEnv(FakeGomaEnv):
      def __init__(self):
        super(SpyGomaEnv, self).__init__()
        self.http_downloaded = False
        self.manifests = {}
        self.install_package = False
        self.rollback = False

      def HttpDownload(self, url,
                       rewrite_url=None, headers=None, destination_file=None):
        self.http_downloaded = True
        if destination_file:
          dirname = os.path.dirname(destination_file)
          manifest = {'VERSION': '3', 'bad_version': '1|4'}
          self.manifests[dirname] = manifest
          return
        return 'VERSION=3\nbad_version=1|4'

      def ReadManifest(self, path=None):
        if path and self.manifests[path]:
          return self.manifests[path]
        return {'VERSION': '2'}

      def InstallPackage(self, _):
        self.install_package = True
        return True  # install success.

    env = SpyGomaEnv()
    driver = self._module.GomaDriver(env, FakeGomaBackend())
    driver._Update()
    self.assertTrue(env.http_downloaded)
    self.assertTrue(env.install_package)

  def testUpdateShouldRollbackIfAuditFailed(self):
    class SpyGomaEnv(FakeGomaEnv):
      def __init__(self):
        super(SpyGomaEnv, self).__init__()
        self.http_downloaded = False
        self.load_checksum = False
        self.calculate_checksum = False
        self.rollback = False
        self._update_dir = ''

      def HttpDownload(self, url,
                       rewrite_url=None, headers=None, destination_file=None):
        self.http_downloaded = True
        return 'VERSION=1'

      def RollbackUpdate(self, backup_dir=None):
        self.rollback = True

      def LoadChecksum(self, update_dir=''):
        self.load_checksum = True
        if not update_dir:
          raise Exception('update_dir should be specified')
        self._update_dir = update_dir
        return {'gomacc': 'dummy'}

      def CalculateChecksum(self, _, update_dir=''):
        self.calculate_checksum = True
        if update_dir != self._update_dir:
          raise Exception('unexpected update_dir given.'
                          '%s != %s' % (update_dir, self._update_dir))
        return 'invalid'  # wrong checksum.

    env = SpyGomaEnv()
    driver = self._module.GomaDriver(env, FakeGomaBackend())
    self.assertRaises(self._module.Error, driver._Update)
    self.assertTrue(env.http_downloaded)
    self.assertTrue(env.load_checksum)
    self.assertTrue(env.calculate_checksum)
    self.assertTrue(env.rollback)

  def testUpdateShouldRollbackIfUpdateFailed(self):
    class SpyGomaEnv(FakeGomaEnv):
      def __init__(self):
        super(SpyGomaEnv, self).__init__()
        self.http_downloaded = False
        self.install_package = False
        self.rollback = False

      def HttpDownload(self, url,
                       rewrite_url=None, headers=None, destination_file=None):
        self.http_downloaded = True
        return 'VERSION=1'

      def RollbackUpdate(self, backup_dir=None):
        self.rollback = True

      def InstallPackage(self, _):
        self.install_package = True
        return False  # install failure.

    env = SpyGomaEnv()
    driver = self._module.GomaDriver(env, FakeGomaBackend())
    self.assertRaises(self._module.Error, driver._Update)
    self.assertTrue(env.http_downloaded)
    self.assertTrue(env.install_package)
    self.assertTrue(env.rollback)

  def testUpdateShouldRestartIfCompilerProxyRanBeforeUpdate(self):
    class SpyGomaEnv(FakeGomaEnv):
      def __init__(self):
        super(SpyGomaEnv, self).__init__()
        self.http_downloaded = False
        self.installed_before = False
        self.kill_stakeholders = False
        self.exec_compiler_proxy = False

      def HttpDownload(self, url,
                       rewrite_url=None, headers=None, destination_file=None):
        self.http_downloaded = True
        return 'VERSION=1'

      def IsGomaInstalledBefore(self):
        self.installed_before = True
        return True

      def KillStakeholders(self):
        self.kill_stakeholders = True

      def CompilerProxyRunning(self):
        if self.kill_stakeholders:
          return False
        else:
          return True

      def ExecCompilerProxy(self):
        self.exec_compiler_proxy = True

    env = SpyGomaEnv()
    driver = self._module.GomaDriver(env, FakeGomaBackend())
    driver._Update()
    self.assertTrue(env.http_downloaded)
    self.assertTrue(env.installed_before)
    self.assertTrue(env.kill_stakeholders)
    self.assertTrue(env.exec_compiler_proxy)

  def testUpdateShouldNotStartIfCompilerProxyDidNotRunBeforeUpdate(self):
    class SpyGomaEnv(FakeGomaEnv):
      def __init__(self):
        super(SpyGomaEnv, self).__init__()
        self.compiler_proxy_running = False
        self.http_downloaded = False
        self.installed_before = False
        self.kill_stakeholders = False
        self.exec_compiler_proxy = False

      def HttpDownload(self, url,
                       rewrite_url=None, headers=None, destination_file=None):
        self.http_downloaded = True
        return 'VERSION=1'

      def IsGomaInstalledBefore(self):
        self.installed_before = True
        return True

      def KillStakeholders(self):
        self.kill_stakeholders = True

      def CompilerProxyRunning(self):
        self.compiler_proxy_running = True
        return False

      def ExecCompilerProxy(self):
        self.exec_compiler_proxy = True

    env = SpyGomaEnv()
    driver = self._module.GomaDriver(env, FakeGomaBackend())
    driver._Update()
    self.assertTrue(env.http_downloaded)
    self.assertTrue(env.compiler_proxy_running)
    self.assertTrue(env.installed_before)
    self.assertFalse(env.kill_stakeholders)
    self.assertFalse(env.exec_compiler_proxy)

  def testUpdatePackageShouldKillStackeholdersIfGomaInstalledBefore(self):
    class SpyGomaEnv(FakeGomaEnv):
      def __init__(self):
        super(SpyGomaEnv, self).__init__()
        self.read_manifest = False
        self.is_installed_before = False
        self.kill_all_goma_processes = False
        self.install_package = False

      def ReadManifest(self, _=None):
        self.read_manifest = True
        return {'VERSION': '1'}

      def ExtractPackage(self, package_file, update_dir):
        # package_file and update_dir are dummy.
        # pylint: disable=W0613
        return True

      def IsGomaInstalledBefore(self):
        self.is_installed_before = True
        return True

      def KillStakeholders(self):
        self.kill_all_goma_processes = True

      def InstallPackage(self, _):
        self.install_package = True
        return True

    env = SpyGomaEnv()
    driver = self._module.GomaDriver(env, FakeGomaBackend())
    driver._UpdatePackage()
    self.assertTrue(env.read_manifest)
    self.assertTrue(env.is_installed_before)
    self.assertTrue(env.kill_all_goma_processes)
    self.assertTrue(env.install_package)

  def testRestartCompilerProxyShouldRun(self):
    driver = self._module.GomaDriver(FakeGomaEnv(), FakeGomaBackend())
    driver._RestartCompilerProxy()

  def testEnsureStartShouldStartCompilerProxy(self):
    class SpyGomaEnv(FakeGomaEnv):
      def __init__(self):
        super(SpyGomaEnv, self).__init__()
        self.can_auto_update = False
        self.compiler_proxy_running = False
        self.exec_compiler_proxy = False

      def CanAutoUpdate(self):
        self.can_auto_update = True
        return False

      def CompilerProxyRunning(self):
        self.compiler_proxy_running = True
        return False

      def ExecCompilerProxy(self):
        self.exec_compiler_proxy = True

    env = SpyGomaEnv()
    driver = self._module.GomaDriver(env, FakeGomaBackend())
    driver._EnsureStartCompilerProxy()
    self.assertTrue(env.can_auto_update)
    self.assertTrue(env.compiler_proxy_running)
    self.assertTrue(env.exec_compiler_proxy)

  def testGomaStatusShouldTrueForOK(self):
    class SpyGomaEnv(FakeGomaEnv):
      def __init__(self):
        super(SpyGomaEnv, self).__init__()
        self.compiler_proxy_healthz_called = False
      def ControlCompilerProxy(self, command, check_running=False,
                               need_pids=False):
        if command == '/healthz':
          self.compiler_proxy_healthz_called = True
          return {'status': True, 'message': 'ok', 'url': 'fake',
                  'pid': 'unknown'}
        return super(SpyGomaEnv, self).ControlCompilerProxy(command,
                                                            check_running,
                                                            need_pids)

    env = SpyGomaEnv()
    driver = self._module.GomaDriver(env, FakeGomaBackend())
    self.assertTrue(driver._GetStatus())
    self.assertTrue(env.compiler_proxy_healthz_called)

  def testGomaStatusShouldTrueForRunning(self):
    class SpyGomaEnv(FakeGomaEnv):
      def __init__(self):
        super(SpyGomaEnv, self).__init__()
        self.compiler_proxy_healthz_called = False
      def ControlCompilerProxy(self, command, check_running=False,
                               need_pids=False):
        if command == '/healthz':
          self.compiler_proxy_healthz_called = True
          return {'status': True, 'message': 'running: had some error',
                  'url': 'fake', 'pid': 'unknown'}
        return super(SpyGomaEnv, self).ControlCompilerProxy(command,
                                                            check_running,
                                                            need_pids)

    env = SpyGomaEnv()
    driver = self._module.GomaDriver(env, FakeGomaBackend())
    self.assertTrue(driver._GetStatus())
    self.assertTrue(env.compiler_proxy_healthz_called)

  def testGomaStatusShouldFalseForError(self):
    class SpyGomaEnv(FakeGomaEnv):
      def __init__(self):
        super(SpyGomaEnv, self).__init__()
        self.compiler_proxy_healthz_called = False
      def ControlCompilerProxy(self, command, check_running=False,
                               need_pids=False):
        if command == '/healthz':
          self.compiler_proxy_healthz_called = True
          return {'status': True, 'message': 'error: had some error',
                  'url': 'fake', 'pid': 'unknown'}
        return super(SpyGomaEnv, self).ControlCompilerProxy(command,
                                                            check_running,
                                                            need_pids)

    env = SpyGomaEnv()
    driver = self._module.GomaDriver(env, FakeGomaBackend())
    self.assertFalse(driver._GetStatus())
    self.assertTrue(env.compiler_proxy_healthz_called)

  def testGomaStatusShouldFalseForUnresponseHealthz(self):
    class SpyGomaEnv(FakeGomaEnv):
      def __init__(self):
        super(SpyGomaEnv, self).__init__()
        self.compiler_proxy_healthz_called = False
      def ControlCompilerProxy(self, command, check_running=False,
                               need_pids=False):
        if command == '/healthz':
          self.compiler_proxy_healthz_called = True
          return {'status': False, 'message': '', 'url': 'fake',
                  'pid': 'unknown'}
        return super(SpyGomaEnv, self).ControlCompilerProxy(command,
                                                            check_running,
                                                            need_pids)

    env = SpyGomaEnv()
    driver = self._module.GomaDriver(env, FakeGomaBackend())
    self.assertFalse(driver._GetStatus())
    self.assertTrue(env.compiler_proxy_healthz_called)

  def testEnsureStartShouldNotKillCompilerProxyWithoutUpdate(self):
    class SpyGomaEnv(FakeGomaEnv):
      def __init__(self):
        super(SpyGomaEnv, self).__init__()
        self.can_auto_update = False
        self.kill_all = False
        self.using_default = False
        self.control_with_quit = False
        self.control_with_health = False
        self.control_with_version = False
        self.get_version = False
        self.compiler_proxy_running = False

      def CanAutoUpdate(self):
        self.can_auto_update = True
        return False

      def KillStakeholders(self):
        self.kill_all = True

      def MayUsingDefaultIPCPort(self):
        self.using_default = True
        return True

      def CompilerProxyRunning(self):
        self.compiler_proxy_running = True
        return True

      def GetCompilerProxyVersion(self):
        self.get_version = True
        return 'GOMA version dummy_version'

      def ControlCompilerProxy(self, command, check_running=False,
                               need_pids=False):
        if command == '/quitquitquit':
          self.control_with_quit = True
        elif command == '/healthz':
          self.control_with_health = True
        elif command == '/versionz':
          self.control_with_version = True
          return {'status': True, 'message': 'dummy_version', 'url': 'fake',
                  'pid': 'unknown'}
        elif command == '/flagz':
          return {'status': True, 'message': '', 'pid': 'unknown'}
        else:
          raise Exception('Unknown command given.')
        return super(SpyGomaEnv, self).ControlCompilerProxy(command,
                                                            check_running,
                                                            need_pids)

    env = SpyGomaEnv()
    driver = self._module.GomaDriver(env, FakeGomaBackend())
    driver._EnsureStartCompilerProxy()
    self.assertFalse(env.using_default)
    self.assertFalse(env.kill_all)
    self.assertTrue(env.can_auto_update)
    self.assertTrue(env.compiler_proxy_running)
    self.assertTrue(env.get_version)
    self.assertTrue(env.control_with_health)
    self.assertTrue(env.control_with_version)
    self.assertFalse(env.control_with_quit)

  def testEnsureStartShouldUpdateCompilerProxy(self):
    class SpyGomaEnv(FakeGomaEnv):
      def __init__(self):
        super(SpyGomaEnv, self).__init__()
        self.can_auto_update = False
        self.read_manifest = False
        self.http_downloaded = False
        self.auto_update = False

      def CanAutoUpdate(self):
        self.can_auto_update = True
        return True

      def ReadManifest(self, _=None):
        self.read_manifest = True
        return {'VERSION': '1'}

      def HttpDownload(self, url,
                       rewrite_url=None, headers=None, destination_file=None):
        self.http_downloaded = True
        return 'VERSION=2'

      def AutoUpdate(self):
        self.auto_update = True

    env = SpyGomaEnv()
    driver = self._module.GomaDriver(env, FakeGomaBackend())
    driver._EnsureStartCompilerProxy()
    self.assertTrue(env.can_auto_update)
    self.assertTrue(env.read_manifest)
    self.assertTrue(env.http_downloaded)
    self.assertTrue(env.auto_update)

  def testEnsureStartShouldNotUpdateCPIfManifestModifiedRecently(self):
    class SpyGomaEnv(FakeGomaEnv):
      def __init__(self):
        super(SpyGomaEnv, self).__init__()
        self.can_auto_update = False
        self.read_manifest = False
        self.http_downloaded = False
        self.auto_update = False

      def CanAutoUpdate(self):
        self.can_auto_update = True
        return True

      def IsManifestModifiedRecently(self, directory='', threshold=4*60*60):
        return True

      def ReadManifest(self, _=None):
        self.read_manifest = True
        return {'VERSION': '1'}

      def HttpDownload(self, url,
                       rewrite_url=None, headers=None, destination_file=None):
        self.http_downloaded = True
        return 'VERSION=2'

      def AutoUpdate(self):
        self.auto_update = True

    env = SpyGomaEnv()
    driver = self._module.GomaDriver(env, FakeGomaBackend())
    driver._EnsureStartCompilerProxy()
    self.assertTrue(env.can_auto_update)
    self.assertTrue(env.read_manifest)
    self.assertFalse(env.http_downloaded)
    self.assertFalse(env.auto_update)

  def testEnsureStartShouldUpdateCompilerProxyToRollbackRelease(self):
    class SpyGomaEnv(FakeGomaEnv):
      def __init__(self):
        super(SpyGomaEnv, self).__init__()
        self.can_auto_update = False
        self.read_manifest = False
        self.http_downloaded = False
        self.manifests = {}
        self.auto_update = False

      def CanAutoUpdate(self):
        self.can_auto_update = True
        return True

      def ReadManifest(self, path=None):
        self.read_manifest = True
        if path and (path in self.manifests) and self.manifest[path]:
          return self.manifests[path]
        return {'VERSION': '2'}

      def HttpDownload(self, url,
                       rewrite_url=None, headers=None, destination_file=None):
        self.http_downloaded = True
        if destination_file:
          dirname = os.path.dirname(destination_file)
          manifest = {'VERSION': '1', 'bad_version': '2'}
          self.manifests[dirname] = manifest
          return
        return 'VERSION=1\nbad_version=2'

      def AutoUpdate(self):
        self.auto_update = True

    env = SpyGomaEnv()
    driver = self._module.GomaDriver(env, FakeGomaBackend())
    driver._EnsureStartCompilerProxy()
    self.assertTrue(env.can_auto_update)
    self.assertTrue(env.read_manifest)
    self.assertTrue(env.http_downloaded)
    self.assertTrue(env.auto_update)

  def testEnsureStartShouldBeSurelyRunCompilerProxyWhenUpdate(self):
    class SpyGomaEnv(FakeGomaEnv):
      def __init__(self):
        super(SpyGomaEnv, self).__init__()
        self.can_auto_update = False
        self.compiler_proxy_running = False
        self.exec_compiler_proxy = False
        self.read_manifest = False
        self.http_downloaded = False
        self.auto_update = False

      def CanAutoUpdate(self):
        self.can_auto_update = True
        return True

      def CompilerProxyRunning(self):
        self.compiler_proxy_running = True
        return False

      def ExecCompilerProxy(self):
        self.exec_compiler_proxy = True

      def ReadManifest(self, _=None):
        self.read_manifest = True
        return {'VERSION': '1'}

      def HttpDownload(self, url,
                       rewrite_url=None, headers=None, destination_file=None):
        self.http_downloaded = True
        return 'VERSION=2'

      def AutoUpdate(self):
        self.auto_update = True

    env = SpyGomaEnv()
    driver = self._module.GomaDriver(env, FakeGomaBackend())
    driver._EnsureStartCompilerProxy()
    self.assertTrue(env.can_auto_update)
    self.assertTrue(env.read_manifest)
    self.assertTrue(env.http_downloaded)
    self.assertTrue(env.auto_update)
    self.assertTrue(env.compiler_proxy_running)
    self.assertTrue(env.exec_compiler_proxy)

  def testEnsureStartShouldRestartCompilerProxyIfBinaryHasUpdated(self):
    class SpyGomaEnv(FakeGomaEnv):
      def __init__(self):
        super(SpyGomaEnv, self).__init__()
        self.compiler_proxy_running = False
        self.control_with_quit = False
        self.control_with_health = False
        self.control_with_version = False
        self.get_version = False
        self.kill_stakeholders = False

      def CompilerProxyRunning(self):
        self.compiler_proxy_running = True
        return True

      def GetCompilerProxyVersion(self):
        self.get_version = True
        return 'GOMA version This version should not be matched.'

      def ControlCompilerProxy(self, command, check_running=False,
                               need_pids=False):
        if command == '/quitquitquit':
          self.control_with_quit = True
        elif command == '/healthz':
          self.control_with_health = True
        elif command == '/versionz':
          self.control_with_version = True
        elif command == '/flagz':
          return {'status': True, 'message': '', 'pid': 'unknown'}
        else:
          raise Exception('Unknown command given.')
        return super(SpyGomaEnv, self).ControlCompilerProxy(command,
                                                            check_running,
                                                            need_pids)

      def KillStakeholders(self):
        self.kill_stakeholders = True

    env = SpyGomaEnv()
    driver = self._module.GomaDriver(env, FakeGomaBackend())
    driver._EnsureStartCompilerProxy()
    self.assertTrue(env.compiler_proxy_running)
    self.assertTrue(env.get_version)
    self.assertTrue(env.control_with_health)
    self.assertTrue(env.control_with_version)
    self.assertTrue(env.control_with_quit)
    self.assertTrue(env.kill_stakeholders)

  def testEnsureStartShouldRestartCompilerProxyIfHealthzFailed(self):
    class SpyGomaEnv(FakeGomaEnv):
      def __init__(self):
        super(SpyGomaEnv, self).__init__()
        self.compiler_proxy_running = False
        self.control_with_quit = False
        self.control_with_health = False
        self.control_with_version = False
        self.get_version = False
        self.kill_stakeholders = False
        self.exec_compiler_proxy = False
        self.status_compiler_proxy_running = True
        self.status_healthy = False

      def CompilerProxyRunning(self):
        self.compiler_proxy_running = True
        return self.status_compiler_proxy_running

      def GetCompilerProxyVersion(self):
        self.get_version = True
        return 'GOMA version dummy_version'

      def ControlCompilerProxy(self, command, check_running=False,
                               need_pids=False):
        if command == '/quitquitquit':
          self.control_with_quit = True
        elif command == '/healthz':
          self.control_with_health = True
          if self.status_healthy:
            return {'status': True,
                    'message': 'ok',
                    'url': 'dummy',
                    'pid': 'unknown',
                    }
          else:
            return {'status': False,
                    'message': 'connect failed',
                    'url': '',
                    'pid': 'unknown',
                    }
        elif command == '/versionz':
          self.control_with_version = True
          return {'status': True, 'message': 'dummy_version', 'url': 'fake'}
        elif command == '/flagz':
          return {'status': True, 'message': ''}
        else:
          raise Exception('Unknown command given.')
        return super(SpyGomaEnv, self).ControlCompilerProxy(command,
                                                            check_running,
                                                            need_pids)

      def KillStakeholders(self):
        self.kill_stakeholders = True
        self.status_compiler_proxy_running = False

      def ExecCompilerProxy(self):
        self.exec_compiler_proxy = True
        self.status_compiler_proxy_running = True
        self.status_healthy = True

    env = SpyGomaEnv()
    driver = self._module.GomaDriver(env, FakeGomaBackend())
    driver._EnsureStartCompilerProxy()
    self.assertTrue(env.compiler_proxy_running)
    self.assertTrue(env.get_version)
    self.assertTrue(env.control_with_health)
    self.assertTrue(env.control_with_version)
    self.assertTrue(env.control_with_quit)
    self.assertTrue(env.exec_compiler_proxy)
    self.assertTrue(env.kill_stakeholders)

  def testEnsureStartShouldRestartIfFlagsAreChanged(self):
    class SpyGomaEnv(FakeGomaEnv):
      def __init__(self):
        super(SpyGomaEnv, self).__init__()
        self.compiler_proxy_running = False
        self.control_with_quit = False
        self.control_with_health = False
        self.control_with_version = False
        self.control_with_flagz = False
        self.get_version = False
        self.kill_stakeholders = False

      def CompilerProxyRunning(self):
        self.compiler_proxy_running = True
        return True

      def GetCompilerProxyVersion(self):
        self.get_version = True
        return 'GOMA version dummy_version'

      def ControlCompilerProxy(self, command, check_running=False,
                               need_pids=False):
        if command == '/quitquitquit':
          self.control_with_quit = True
        elif command == '/healthz':
          self.control_with_health = True
          return {'status': True,
                  'message': 'ok',
                  'url': 'dummy',
                  'pid': 'unknown',
                  }
        elif command == '/versionz':
          self.control_with_version = True
          return {'status': True, 'message': 'dummy_version', 'url': 'fake',
                  'pid': 'unknown'}
        elif command == '/flagz':
          self.control_with_flagz = True
          return {'status': True, 'message': '', 'pid': 'unknown'}
        else:
          raise Exception('Unknown command given.')
        return super(SpyGomaEnv, self).ControlCompilerProxy(command,
                                                            check_running,
                                                            need_pids)

      def KillStakeholders(self):
        self.kill_stakeholders = True

    os.environ['GOMA_TEST'] = 'flag should be different'
    env = SpyGomaEnv()
    driver = self._module.GomaDriver(env, FakeGomaBackend())
    driver._EnsureStartCompilerProxy()
    self.assertTrue(env.compiler_proxy_running)
    self.assertTrue(env.get_version)
    self.assertTrue(env.control_with_health)
    self.assertTrue(env.control_with_version)
    self.assertTrue(env.control_with_quit)
    self.assertTrue(env.control_with_flagz)
    self.assertTrue(env.kill_stakeholders)

  def testEnsureStartShouldRestartIfFlagsAreRemoved(self):
    class SpyGomaEnv(FakeGomaEnv):
      def __init__(self):
        super(SpyGomaEnv, self).__init__()
        self.compiler_proxy_running = False
        self.control_with_quit = False
        self.control_with_health = False
        self.control_with_version = False
        self.control_with_flagz = False
        self.get_version = False
        self.kill_stakeholders = False

      def CompilerProxyRunning(self):
        self.compiler_proxy_running = True
        return True

      def GetCompilerProxyVersion(self):
        self.get_version = True
        return 'GOMA version dummy_version'

      def ControlCompilerProxy(self, command, check_running=False,
                               need_pids=False):
        if command == '/quitquitquit':
          self.control_with_quit = True
        elif command == '/healthz':
          self.control_with_health = True
          return {'status': True,
                  'message': 'ok',
                  'url': 'dummy',
                  'pid': 'unknown',
                  }
        elif command == '/versionz':
          self.control_with_version = True
          return {'status': True, 'message': 'dummy_version', 'url': 'fake',
                  'pid': 'unknown'}
        elif command == '/flagz':
          self.control_with_flagz = True
          return {'status': True, 'message': 'GOMA_TEST=test\n',
                  'pid': 'unknown'}
        else:
          raise Exception('Unknown command given.')
        return super(SpyGomaEnv, self).ControlCompilerProxy(command,
                                                            check_running,
                                                            need_pids)

      def KillStakeholders(self):
        self.kill_stakeholders = True

    env = SpyGomaEnv()
    driver = self._module.GomaDriver(env, FakeGomaBackend())
    driver._EnsureStartCompilerProxy()
    self.assertTrue(env.compiler_proxy_running)
    self.assertTrue(env.get_version)
    self.assertTrue(env.control_with_health)
    self.assertTrue(env.control_with_version)
    self.assertTrue(env.control_with_quit)
    self.assertTrue(env.control_with_flagz)
    self.assertTrue(env.kill_stakeholders)

  def testEnsureStartShouldNotRestartIfFlagsNotChanged(self):
    class SpyGomaEnv(FakeGomaEnv):
      def __init__(self):
        super(SpyGomaEnv, self).__init__()
        self.compiler_proxy_running = False
        self.control_with_quit = False
        self.control_with_health = False
        self.control_with_version = False
        self.control_with_flagz = False
        self.get_version = False
        self.kill_stakeholders = False

      def CompilerProxyRunning(self):
        self.compiler_proxy_running = True
        return True

      def GetCompilerProxyVersion(self):
        self.get_version = True
        return 'GOMA version dummy_version'

      def ControlCompilerProxy(self, command, check_running=False,
                               need_pids=False):
        if command == '/quitquitquit':
          self.control_with_quit = True
        elif command == '/healthz':
          self.control_with_health = True
          return {'status': True,
                  'message': 'ok',
                  'url': 'dummy',
                  'pid': 'unknown',
                  }
        elif command == '/versionz':
          self.control_with_version = True
          return {'status': True, 'message': 'dummy_version', 'url': 'fake',
                  'pid': 'unknown'}
        elif command == '/flagz':
          self.control_with_flagz = True
          return {'status': True, 'message': '', 'pid': 'unknown'}
        else:
          raise Exception('Unknown command given.')
        return super(SpyGomaEnv, self).ControlCompilerProxy(command,
                                                            check_running,
                                                            need_pids)

      def KillStakeholders(self):
        self.kill_stakeholders = True

    env = SpyGomaEnv()
    driver = self._module.GomaDriver(env, FakeGomaBackend())
    driver._EnsureStartCompilerProxy()
    self.assertTrue(env.compiler_proxy_running)
    self.assertTrue(env.get_version)
    self.assertTrue(env.control_with_health)
    self.assertTrue(env.control_with_version)
    self.assertTrue(env.control_with_flagz)
    self.assertFalse(env.control_with_quit)
    self.assertFalse(env.kill_stakeholders)

  def testEnsureStartShouldRestartCompilerProxyIfUnhealthy(self):
    class SpyGomaEnv(FakeGomaEnv):
      def __init__(self):
        super(SpyGomaEnv, self).__init__()
        self.compiler_proxy_running = False
        self.control_with_quit = False
        self.control_with_health = False
        self.control_with_version = False
        self.get_version = False
        self.kill_stakeholders = False

      def CompilerProxyRunning(self):
        self.compiler_proxy_running = True
        return True

      def GetCompilerProxyVersion(self):
        self.get_version = True
        return 'GOMA version dummy_version'

      def ControlCompilerProxy(self, command, check_running=False,
                               need_pids=False):
        if command == '/quitquitquit':
          self.control_with_quit = True
        elif command == '/healthz':
          self.control_with_health = True
          return {'status': True,
                  'message': 'running: failed to connect to backend servers',
                  'url': '',
                  'pid': 'unknown',
                  }
        elif command == '/versionz':
          self.control_with_version = True
          return {'status': True, 'message': 'dummy_version', 'url': 'fake',
                  'pid': 'unknown'}
        elif command == '/flagz':
          return {'status': True, 'message': '', 'pid': 'unknown'}
        else:
          raise Exception('Unknown command given.')
        return super(SpyGomaEnv, self).ControlCompilerProxy(command,
                                                            check_running,
                                                            need_pids)

      def KillStakeholders(self):
        self.kill_stakeholders = True

    env = SpyGomaEnv()
    driver = self._module.GomaDriver(env, FakeGomaBackend())
    driver._EnsureStartCompilerProxy()
    self.assertTrue(env.compiler_proxy_running)
    self.assertTrue(env.get_version)
    self.assertTrue(env.control_with_health)
    self.assertTrue(env.control_with_version)
    self.assertTrue(env.control_with_quit)
    self.assertTrue(env.kill_stakeholders)

  def testFetchShouldBeAbleToReturnMyPackage(self):
    self._module._GetPackageName(self._platform_specific.GetPlatform())

  def testFetchShouldRaiseIfPackageUnknown(self):
    self.assertRaises(self._module.ConfigError,
                      self._module._GetPackageName,
                      'unknown_package_name')

  def testFetchShouldRun(self):
    driver = self._module.GomaDriver(FakeGomaEnv(), FakeGomaBackend())
    driver._args = ['dummy', self._platform_specific.GetPlatform()]
    driver._Fetch()

  def testFetchShouldOutputToGivenOutputFile(self):
    class SpyGomaEnv(FakeGomaEnv):
      def __init__(self):
        super(SpyGomaEnv, self).__init__()
        self.http_download = False
        self.dest = None

      def HttpDownload(self, url,
                       rewrite_url=None, headers=None, destination_file=None):
        self.http_download = True
        self.dest = destination_file

    output_file = 'TEST'
    env = SpyGomaEnv()
    driver = self._module.GomaDriver(env, FakeGomaBackend())
    driver._args = ['dummy', self._platform_specific.GetPlatform(), output_file]
    driver._Fetch()
    self.assertTrue(env.http_download)
    self.assertTrue(output_file in env.dest,
                    msg='Seems not output to specified file.')

  def testFetchShouldRaiseIfPlatformNotGiven(self):
    driver = self._module.GomaDriver(FakeGomaEnv(), FakeGomaBackend())
    self.assertRaises(self._module.ConfigError, driver._Fetch)

  def testIsCompilerProxySilentlyUpdatedShouldReturnTrueIfVersionMismatch(self):
    class SpyGomaEnv(FakeGomaEnv):
      def __init__(self):
        super(SpyGomaEnv, self).__init__()
        self.get_version = False
        self.control_with_version = False

      def GetCompilerProxyVersion(self):
        self.get_version = True
        return 'GOMA version fake0'

      def ControlCompilerProxy(self, command, check_running=False,
                               need_pids=False):
        self.control_with_version = True
        if command == '/versionz':
          return {'status': True, 'message': 'fake1', 'url': 'fake',
                  'pid': 'unknown'}

    env = SpyGomaEnv()
    driver = self._module.GomaDriver(env, FakeGomaBackend())
    self.assertTrue(driver._IsCompilerProxySilentlyUpdated())
    self.assertTrue(env.get_version)
    self.assertTrue(env.control_with_version)

  def testIsCompilerProxySilentlyUpdatedShouldReturnFalseIfVersionMatch(self):
    class SpyGomaEnv(FakeGomaEnv):
      def __init__(self):
        super(SpyGomaEnv, self).__init__()
        self.get_version = False
        self.control_with_version = False

      def GetCompilerProxyVersion(self):
        self.get_version = True
        return 'GOMA version fake0'

      def ControlCompilerProxy(self, command, check_running=False,
                               need_pids=False):
        self.control_with_version = True
        if command == '/versionz':
          return {'status': True, 'message': 'fake0', 'url': 'fake',
                  'pid': 'unknown'}

    env = SpyGomaEnv()
    driver = self._module.GomaDriver(env, FakeGomaBackend())
    self.assertFalse(driver._IsCompilerProxySilentlyUpdated())
    self.assertTrue(env.get_version)
    self.assertTrue(env.control_with_version)

  def testGetJsonStatusShouldShowErrorStatusOnControlCompilerProxyError(self):
    class SpyGomaEnv(FakeGomaEnv):
      def __init__(self):
        super(SpyGomaEnv, self).__init__()
        self.control_compiler_proxy = False

      def ControlCompilerProxy(self, command, check_running=False,
                               need_pids=False):
        self.control_compiler_proxy = True
        if command == '/errorz':
          return {'status': False, 'pid': 'unknown'}

    env = SpyGomaEnv()
    driver = self._module.GomaDriver(env, FakeGomaBackend())
    printed_json = json.loads(driver._GetJsonStatus())
    self.assertTrue(env.control_compiler_proxy)
    self.assertEqual(printed_json['notice'][0]['compile_error'],
                     'COMPILER_PROXY_UNREACHABLE')

  def testGetJsonStatusShouldShowCompilerProxyReplyAsIsIfAvailable(self):
    compiler_proxy_output = '{"fake": 0}'
    class SpyGomaEnv(FakeGomaEnv):
      def __init__(self):
        super(SpyGomaEnv, self).__init__()
        self.control_compiler_proxy = False

      def ControlCompilerProxy(self, command, check_running=False,
                               need_pids=False):
        self.control_compiler_proxy = True
        if command == '/errorz':
          return {'status': True, 'message': compiler_proxy_output,
                  'pid': 'unknown'}

    env = SpyGomaEnv()
    driver = self._module.GomaDriver(env, FakeGomaBackend())
    returned = driver._GetJsonStatus()
    self.assertTrue(env.control_compiler_proxy)
    self.assertEqual(returned, compiler_proxy_output)

  def testCreateGomaTmpDirectoryNew(self):
    fake_tmpdir = '/tmp/gomatest_chrome-bot'
    class SpyGomaEnv(FakeGomaEnv):
      def __init__(self):
        super(SpyGomaEnv, self).__init__()
        self.is_directory_exist = None
        self.make_directory = None
        self.ensure_directory_owned_by_user = None

      def GetGomaTmpDir(self):
        return fake_tmpdir

      def IsDirectoryExist(self, dirname):
        self.is_directory_exist = dirname
        return False

      def MakeDirectory(self, dirname):
        self.make_directory = dirname

      def EnsureDirectoryOwnedByUser(self, dirname):
        self.ensure_directory_owned_by_user = dirname
        return True

    env = SpyGomaEnv()
    driver = self._module.GomaDriver(env, FakeGomaBackend())
    orig_goma_tmp_dir = os.environ.get('GOMA_TMP_DIR')
    self.assertNotEquals(orig_goma_tmp_dir, fake_tmpdir)
    driver._CreateGomaTmpDirectory()
    goma_tmp_dir = os.environ.get('GOMA_TMP_DIR')
    if orig_goma_tmp_dir:
      os.environ['GOMA_TMP_DIR'] = orig_goma_tmp_dir
    else:
      del os.environ['GOMA_TMP_DIR']
    self.assertEquals(env.is_directory_exist, fake_tmpdir)
    self.assertEquals(env.make_directory, fake_tmpdir)
    self.assertEquals(env.ensure_directory_owned_by_user, None)
    self.assertEquals(goma_tmp_dir, fake_tmpdir)

  def testCreateGomaTmpDirectoryExists(self):
    fake_tmpdir = '/tmp/gomatest_chrome-bot'
    class SpyGomaEnv(FakeGomaEnv):
      def __init__(self):
        super(SpyGomaEnv, self).__init__()
        self.is_directory_exist = None
        self.make_directory = None
        self.ensure_directory_owned_by_user = None

      def GetGomaTmpDir(self):
        return fake_tmpdir

      def IsDirectoryExist(self, dirname):
        self.is_directory_exist = dirname
        return True

      def MakeDirectory(self, dirname):
        self.make_directory = dirname

      def EnsureDirectoryOwnedByUser(self, dirname):
        self.ensure_directory_owned_by_user = dirname
        return True

    env = SpyGomaEnv()
    driver = self._module.GomaDriver(env, FakeGomaBackend())
    orig_goma_tmp_dir = os.environ.get('GOMA_TMP_DIR')
    self.assertNotEquals(orig_goma_tmp_dir, fake_tmpdir)
    driver._CreateGomaTmpDirectory()
    goma_tmp_dir = os.environ.get('GOMA_TMP_DIR')
    if orig_goma_tmp_dir:
      os.environ['GOMA_TMP_DIR'] = orig_goma_tmp_dir
    else:
      del os.environ['GOMA_TMP_DIR']
    self.assertEquals(env.is_directory_exist, fake_tmpdir)
    self.assertEquals(env.make_directory, None)
    self.assertEquals(env.ensure_directory_owned_by_user, fake_tmpdir)
    self.assertEquals(goma_tmp_dir, fake_tmpdir)


  def testCreateCrashDumpDirectoryShouldNotCreateDirectoryIfExist(self):
    fake_dump_dir = '/dump_dir'
    expected_dump_dir = fake_dump_dir

    class SpyGomaEnv(FakeGomaEnv):
      def __init__(self):
        super(SpyGomaEnv, self).__init__()
        self.is_directory_exist = False
        self.is_directory_exist_dir = ''
        self.ensure_directory_owned_by_user = False
        self.ensure_directory_owned_by_user_dir = ''
        self.make_directory = False
        self.get_crash_dump_directory = False

      def GetCrashDumpDirectory(self):
        self.get_crash_dump_directory = True
        return fake_dump_dir

      def IsDirectoryExist(self, dirname):
        self.is_directory_exist = True
        self.is_directory_exist_dir = dirname
        return True

      def EnsureDirectoryOwnedByUser(self, dirname):
        self.ensure_directory_owned_by_user = True
        self.ensure_directory_owned_by_user_dir = dirname
        return True

      def MakeDirectory(self, _):
        self.make_directory = True
        return True

    env = SpyGomaEnv()
    driver = self._module.GomaDriver(env, FakeGomaBackend())
    driver._CreateCrashDumpDirectory()
    self.assertTrue(env.get_crash_dump_directory)
    self.assertTrue(env.is_directory_exist)
    self.assertEqual(env.is_directory_exist_dir, expected_dump_dir)
    self.assertTrue(env.ensure_directory_owned_by_user)
    self.assertEqual(env.ensure_directory_owned_by_user_dir, expected_dump_dir)
    self.assertFalse(env.make_directory)

  def testCreateCrashDumpDirectoryShouldCreateDirectoryIfNotExist(self):
    fake_dump_dir = '/dump_dir'
    expected_dump_dir = fake_dump_dir

    class SpyGomaEnv(FakeGomaEnv):
      def __init__(self):
        super(SpyGomaEnv, self).__init__()
        self.is_directory_exist = False
        self.is_directory_exist_dir = ''
        self.ensure_directory_owned_by_user = False
        self.make_directory = False
        self.make_directory_dir = ''
        self.get_crash_dump_directory = False

      def GetCrashDumpDirectory(self):
        self.get_crash_dump_directory = True
        return fake_dump_dir

      def IsDirectoryExist(self, dirname):
        self.is_directory_exist = True
        self.is_directory_exist_dir = dirname
        return False

      def EnsureDirectoryOwnedByUser(self, _):
        self.ensure_directory_owned_by_user = True
        return True

      def MakeDirectory(self, dirname):
        self.make_directory = True
        self.make_directory_dir = dirname
        return True

    env = SpyGomaEnv()
    driver = self._module.GomaDriver(env, FakeGomaBackend())
    driver._CreateCrashDumpDirectory()
    self.assertTrue(env.get_crash_dump_directory)
    self.assertTrue(env.is_directory_exist)
    self.assertEqual(env.is_directory_exist_dir, expected_dump_dir)
    self.assertFalse(env.ensure_directory_owned_by_user)
    self.assertTrue(env.make_directory)
    self.assertEqual(env.make_directory_dir, expected_dump_dir)

  def testCreateCacheDirectoryShouldNotCreateDirectoryIfExist(self):
    fake_cache_dir = '/cache_dir'
    expected_cache_dir = fake_cache_dir

    class SpyGomaEnv(FakeGomaEnv):
      def __init__(self):
        super(SpyGomaEnv, self).__init__()
        self.is_directory_exist = False
        self.is_directory_exist_dir = ''
        self.ensure_directory_owned_by_user = False
        self.ensure_directory_owned_by_user_dir = ''
        self.make_directory = False
        self.get_cache_directory = False

      def GetCacheDirectory(self):
        self.get_cache_directory = True
        return fake_cache_dir

      def IsDirectoryExist(self, dirname):
        self.is_directory_exist = True
        self.is_directory_exist_dir = dirname
        return True

      def EnsureDirectoryOwnedByUser(self, dirname):
        self.ensure_directory_owned_by_user = True
        self.ensure_directory_owned_by_user_dir = dirname
        return True

      def MakeDirectory(self, _):
        self.make_directory = True
        return True

    env = SpyGomaEnv()
    driver = self._module.GomaDriver(env, FakeGomaBackend())
    driver._CreateCacheDirectory()
    self.assertTrue(env.get_cache_directory)
    self.assertTrue(env.is_directory_exist)
    self.assertEqual(env.is_directory_exist_dir, expected_cache_dir)
    self.assertTrue(env.ensure_directory_owned_by_user)
    self.assertEqual(env.ensure_directory_owned_by_user_dir,
                     expected_cache_dir)
    self.assertFalse(env.make_directory)

  def testCreateCacheDirectoryShouldCreateDirectoryIfNotExist(self):
    fake_cache_dir = 'cache_dir'
    expected_cache_dir = fake_cache_dir

    class SpyGomaEnv(FakeGomaEnv):
      def __init__(self):
        super(SpyGomaEnv, self).__init__()
        self.is_directory_exist = False
        self.is_directory_exist_dir = ''
        self.ensure_directory_owned_by_user = False
        self.make_directory = False
        self.make_directory_dir = ''
        self.get_cache_directory = False

      def GetCacheDirectory(self):
        self.get_cache_directory = True
        return fake_cache_dir

      def IsDirectoryExist(self, dirname):
        self.is_directory_exist = True
        self.is_directory_exist_dir = dirname
        return False

      def EnsureDirectoryOwnedByUser(self, _):
        self.ensure_directory_owned_by_user = True
        return True

      def MakeDirectory(self, dirname):
        self.make_directory = True
        self.make_directory_dir = dirname
        return True

    env = SpyGomaEnv()
    driver = self._module.GomaDriver(env, FakeGomaBackend())
    driver._CreateCacheDirectory()
    self.assertTrue(env.get_cache_directory)
    self.assertTrue(env.is_directory_exist)
    self.assertEqual(env.is_directory_exist_dir, expected_cache_dir)
    self.assertFalse(env.ensure_directory_owned_by_user)
    self.assertTrue(env.make_directory)

  def testValidFilesShouldReturnFalseIfOneFileMagicIsNotValid(self):
    class SpyGomaEnv(FakeGomaEnv):
      """Spy GomaEnv to provide IsValidMagic."""

      def __init__(self):
        super(SpyGomaEnv, self).__init__()
        self.written_manifest = {}
        self.is_valid_magic = False

      def IsValidMagic(self, filename):
        sys.stderr.write(filename)
        if filename.endswith('wrong_magic'):
          self.is_valid_magic = True
          return False
        return True

    env = SpyGomaEnv()
    driver = self._module.GomaDriver(env, FakeGomaBackend())
    self.assertFalse(driver._ValidFiles(['test', 'wrong_magic']))
    self.assertTrue(env.is_valid_magic)

  def testAuditShouldReturnTrueForEmptyJSON(self):
    class SpyGomaEnv(FakeGomaEnv):
      def __init__(self):
        super(SpyGomaEnv, self).__init__()
        self.load_checksum = False
        self.calculate_checksum = False

      def LoadChecksum(self, update_dir=''):
        self.load_checksum = True
        return {}

      def CalculateChecksum(self, _, update_dir=''):
        self.calculate_checksum = True

    env = SpyGomaEnv()
    driver = self._module.GomaDriver(env, FakeGomaBackend())
    self.assertTrue(driver._Audit())
    self.assertTrue(env.load_checksum)
    self.assertFalse(env.calculate_checksum)

  def testAuditShouldReturnTrueForValidChecksum(self):
    class SpyGomaEnv(FakeGomaEnv):
      def __init__(self):
        super(SpyGomaEnv, self).__init__()
        self.load_checksum = False
        self.calculate_checksum = False

      def LoadChecksum(self, update_dir=''):
        self.load_checksum = True
        return {'compiler_proxy': 'valid_checksum'}

      def CalculateChecksum(self, filename, update_dir=''):
        self.calculate_checksum = True
        assert filename == 'compiler_proxy'
        return 'valid_checksum'

    env = SpyGomaEnv()
    driver = self._module.GomaDriver(env, FakeGomaBackend())
    self.assertTrue(driver._Audit())
    self.assertTrue(env.load_checksum)
    self.assertTrue(env.calculate_checksum)

  def testAuditShouldReturnFalseForInvalidChecksum(self):
    class SpyGomaEnv(FakeGomaEnv):
      def __init__(self):
        super(SpyGomaEnv, self).__init__()
        self.load_checksum = False
        self.calculate_checksum = False

      def LoadChecksum(self, update_dir=''):
        self.load_checksum = True
        return {'compiler_proxy': 'valid_checksum'}

      def CalculateChecksum(self, filename, update_dir=''):
        self.calculate_checksum = True
        assert filename == 'compiler_proxy'
        return 'invalid_checksum'

    env = SpyGomaEnv()
    driver = self._module.GomaDriver(env, FakeGomaBackend())
    self.assertFalse(driver._Audit())
    self.assertTrue(env.load_checksum)
    self.assertTrue(env.calculate_checksum)


class GomaEnvTest(GomaCtlTestCommon):
  """Medium tests for GomaEnv in goma_ctl.py.

  Some tests in this class may affect external environment.
  """
  # test should be able to access protected members and variables.
  # pylint: disable=W0212

  def testSetupEnvShouldAutomaticallySetProxyParamIfGomaProxyEnvIsSet(self):
    os.environ['GOMA_PROXY_HOST'] = 'proxy.example.com'
    os.environ['GOMA_PROXY_PORT'] = '3128'
    env = self._module.GomaEnv()
    self.assertEqual(env._https_proxy, 'proxy.example.com:3128')

  def testSetupEnvShouldAutomaticallySetProxyParamIfHttpProxyEnvIsSet(self):
    os.environ['https_proxy'] = 'proxy.example.com:3128'
    env = self._module.GomaEnv()
    self.assertEqual(env._https_proxy, 'proxy.example.com:3128')

  def testSetupEnvShouldNotSetProxyParamIfGomaProxyEnvIsBroken(self):
    os.environ['GOMA_PROXY_HOST'] = 'proxy.example.com'
    os.environ['GOMA_PROXY_PORT'] = '3128'
    env = self._module.GomaEnv()
    self.assertEqual(env._https_proxy, 'proxy.example.com:3128')

  def testBackupCurrentPackageShouldCreateBackup(self):
    env = self._module.GomaEnv()
    env.BackupCurrentPackage()
    self.assertTrue(os.path.isfile(os.path.join(env._dir, 'backup',
                                                'goma_ctl.py')))

  def testBackupCurrentPackageShouldOverwriteBackupDirectory(self):
    dummy_data = 'Hello! This is dummy data.'
    env = self._module.GomaEnv()
    self.assertFalse(os.path.isfile(os.path.join(env._dir, 'backup')))
    env.MakeDirectory('backup')
    dummy_goma_ctl_path = os.path.join(env._dir, 'backup', 'goma_ctl.py')
    with open(dummy_goma_ctl_path, 'w') as f:
      f.write(dummy_data)
    self.assertEqual(open(dummy_goma_ctl_path).read(), dummy_data)
    env.BackupCurrentPackage()
    self.assertTrue(os.path.isfile(dummy_goma_ctl_path))
    self.assertNotEqual(open(dummy_goma_ctl_path).read(), dummy_data)

  def testRollbackShouldRollbackUpdate(self):
    env = self._module.GomaEnv()
    test_file_tuple = tempfile.mkstemp(dir=env._dir)
    os.close(test_file_tuple[0])
    test_file = test_file_tuple[1]
    with open(test_file, 'w') as f:
      f.write('before')
    self.assertTrue(os.path.isfile(test_file))
    env.BackupCurrentPackage()
    with open(os.path.join(env._dir, test_file), 'w') as f:
      f.write('after')
    self.assertEqual(open(test_file).read(), 'after')
    env.RollbackUpdate()
    self.assertEqual(open(test_file).read(), 'before')

  def testRollbackShouldRollbackVeryLongFileName(self):
    env = self._module.GomaEnv()
    long_suffix = '.' + 'Aa0-' * 16
    long_prefix = 'Aa0-' * 16
    # File name should be at least 128 charactors.
    test_file_tuple = tempfile.mkstemp(suffix=long_suffix, prefix=long_prefix,
                                       dir=env._dir)
    os.close(test_file_tuple[0])
    test_file = test_file_tuple[1]
    self.assertTrue(len(test_file) > 128,
                    msg='assuming at least 128 charactors filename.')
    with open(test_file, 'w') as f:
      f.write('before')
    self.assertTrue(os.path.isfile(test_file))
    env.BackupCurrentPackage()
    with open(os.path.join(env._dir, test_file), 'w') as f:
      f.write('after')
    self.assertEqual(open(test_file).read(), 'after')
    env.RollbackUpdate()
    self.assertEqual(open(test_file).read(), 'before')

  def testRollbackShouldNotDieEvenIfOriginalContainsDirectory(self):
    env = self._module.GomaEnv()
    tmp_dir = tempfile.mkdtemp(dir=env._dir)
    self.assertTrue(os.path.isdir(os.path.join(env._dir, tmp_dir)))
    env.BackupCurrentPackage()
    env.RollbackUpdate()

  def testRollbackShouldRecreateRemovedDirectory(self):
    env = self._module.GomaEnv()
    tmp_dir = tempfile.mkdtemp(dir=env._dir)
    self.assertTrue(os.path.isdir(os.path.join(env._dir, tmp_dir)))
    env.BackupCurrentPackage()
    env.RemoveDirectory(tmp_dir)
    self.assertFalse(os.path.isdir(os.path.join(env._dir, tmp_dir)))
    env.RollbackUpdate()
    self.assertTrue(os.path.isdir(os.path.join(env._dir, tmp_dir)))

  def testShouldSetPlatformEnvIfPlatformNotInManifest(self):
    os.environ['PLATFORM'] = 'goobuntu'
    self.assertTrue(os.environ.get('PLATFORM'))
    env = self._module.GomaEnv()
    self.assertFalse(os.path.exists(os.path.join(env._dir, 'MANIFEST')))
    self.assertEqual(env._platform, 'goobuntu')

  def testShouldPreferPlatformInManifestToEnv(self):
    os.environ['PLATFORM'] = 'goobuntu'
    self.assertTrue(os.environ.get('PLATFORM'))
    manifest_file = os.path.join(self._tmp_dir, self._TMP_SUBDIR_NAME,
                                 'MANIFEST')
    with open(manifest_file, 'w') as f:
      f.write('PLATFORM=chromeos')
    env = self._module.GomaEnv()
    self.assertTrue(os.path.exists(os.path.join(env._dir, 'MANIFEST')))
    self.assertEqual(env._platform, 'chromeos')

  def testIsValidMagicShouldBeTrueForValidManifest(self):
    filename = os.path.join(self._tmp_dir, self._TMP_SUBDIR_NAME, 'MANIFEST')
    with open(filename, 'w') as f:
      f.write('PLATFORM=goobuntu\nVERSION=1')
    env = self._module.GomaEnv()
    self.assertTrue(env.IsValidMagic(filename))

  def testIsValidMagicShouldBeFalseForValidManifest(self):
    filename = os.path.join(self._tmp_dir, self._TMP_SUBDIR_NAME, 'MANIFEST')
    with open(filename, 'w') as f:
      f.write('invalid magic')
    env = self._module.GomaEnv()
    self.assertFalse(env.IsValidMagic(filename))

  def testGeneratedChecksumShouldBeValid(self):
    env = self._module.GomaEnv()
    cksums = env.LoadChecksum()
    self.assertTrue(cksums)
    for filename, checksum in cksums.iteritems():
      self.assertEqual(env.CalculateChecksum(filename), checksum)

  def testIsOldFileShouldReturnTrueForOldFile(self):
    filename = os.path.join(self._tmp_dir, self._TMP_SUBDIR_NAME, 'test')
    with open(filename, 'w') as f:
      f.write('test')
    env = self._module.GomaEnv()
    env._time = time.time() + 120
    os.environ['GOMA_LOG_CLEAN_INTERVAL'] = '1'
    self.assertTrue(env.IsOldFile(filename))

  def testIsOldFileShouldReturnFalseIfAFileIsNew(self):
    filename = os.path.join(self._tmp_dir, self._TMP_SUBDIR_NAME, 'test')
    with open(filename, 'w') as f:
      f.write('test')
    env = self._module.GomaEnv()
    env._time = time.time()
    os.environ['GOMA_LOG_CLEAN_INTERVAL'] = '60'
    self.assertFalse(env.IsOldFile(filename))

  def testIsOldFileShouldReturnFalseIfLogCleanIntervalIsNegative(self):
    filename = os.path.join(self._tmp_dir, self._TMP_SUBDIR_NAME, 'test')
    with open(filename, 'w') as f:
      f.write('test')
    env = self._module.GomaEnv()
    env._time = time.time() + 120
    os.environ['GOMA_LOG_CLEAN_INTERVAL'] = '-1'
    self.assertFalse(env.IsOldFile(filename))

  def testMakeDirectory(self):
    env = self._module._GOMA_ENVS[os.name]()
    tmpdir = tempfile.mkdtemp()
    os.rmdir(tmpdir)
    self.assertFalse(os.path.exists(tmpdir))
    env.MakeDirectory(tmpdir)
    self.assertTrue(os.path.isdir(tmpdir))
    if os.name != 'nt':
      st = os.stat(tmpdir)
      self.assertEquals(st.st_uid, os.geteuid())
      self.assertEquals((st.st_mode & 077), 0)
    os.rmdir(tmpdir)

  def testEnsureDirectoryOwnedByUser(self):
    tmpdir = tempfile.mkdtemp()
    env = self._module._GOMA_ENVS[os.name]()
    if os.name == 'nt':
      self.assertTrue(env.EnsureDirectoryOwnedByUser(tmpdir))
      os.rmdir(tmpdir)
      return
    self._module._GetPlatformSpecificTempDirectory = lambda: None
    # test only permissions will not have readable/writable for group/other.
    os.chmod(tmpdir, 0755)
    st = os.stat(tmpdir)
    self.assertEquals(st.st_uid, os.geteuid())
    self.assertNotEquals((st.st_mode & 077), 0)
    self.assertTrue(env.EnsureDirectoryOwnedByUser(tmpdir))
    self.assertTrue(os.path.isdir(tmpdir))
    st = os.stat(tmpdir)
    self.assertEquals(st.st_uid, os.geteuid())
    self.assertEquals((st.st_mode & 077), 0)
    os.rmdir(tmpdir)

  def testCreateCacheDirectoryShouldUseDefaultIfNoEnv(self):
    fake_tmp_dir = '/fake_tmp'
    expected_cache_dir = os.path.join(
        fake_tmp_dir, self._module._CACHE_DIR)

    env = self._module._GOMA_ENVS[os.name]()
    env.GetGomaTmpDir = lambda: fake_tmp_dir
    self.assertEqual(env.GetCacheDirectory(), expected_cache_dir)

  def testCreateCacheDirectoryShouldRespectCacheDirEnv(self):
    fake_tmp_dir = '/fake_tmp'
    fake_cache_dir = '/fake_cache_dir'
    expected_cache_dir = fake_cache_dir

    env = self._module._GOMA_ENVS[os.name]()
    env.GetGomaTmpDir = lambda: fake_tmp_dir
    try:
      backup = os.environ.get('GOMA_CACHE_DIR')
      os.environ['GOMA_CACHE_DIR'] = fake_cache_dir
      self.assertEqual(env.GetCacheDirectory(), expected_cache_dir)
    finally:
      if backup:
        os.environ['GOMA_CACHE_DIR'] = backup
      else:
        del os.environ['GOMA_CACHE_DIR']

  def testShouldFallbackGomaUsernameNoEnvIfNoEnvSet(self):
    self._module._GetUsernameEnv = lambda: ''
    env = self._module._GOMA_ENVS[os.name]()
    self.assertNotEqual(env.GetUsername(), '')


class GomaCtlLargeTestCommon(GomaCtlTestCommon):
  """Large tests for goma_ctl.py.

  All tests in this class may affect external environment.  It may try to
  download packages from servers and I/O local files in test environment.
  """
  # test should be able to access protected members and variables.
  # pylint: disable=W0212

  def __init__(self, method_name, goma_ctl_path, platform_specific,
               oauth2_file, port):
    """Initialize GomaCtlTest.

    Args:
      method_name: a string of test method name to execute.
      goma_ctl_path: a string of goma directory name.
      platform_specific: a object for providing platform specific behavior.
      oauth2_file: a string of OAuth2 service account JSON filename.
      port: a string or an integer port number of compiler_proxy.
    """
    super(GomaCtlLargeTestCommon, self).__init__(method_name, goma_ctl_path,
                                                 platform_specific)
    self._oauth2_file = oauth2_file
    self._port = int(port)
    self._driver = None

  def setUp(self):
    super(GomaCtlLargeTestCommon, self).setUp()
    self._platform_specific.SetCompilerProxyEnv(self._tmp_dir, self._port)

  def tearDown(self):
    if self._driver:
      self._driver._EnsureStopCompilerProxy()
    super(GomaCtlLargeTestCommon, self).tearDown()

  def StartWithModifiedVersion(self, version=None):
    """Start compiler proxy with modified version.

    Since start-up method is overwritten with dummy method, we do not need
    to stop the compiler proxy.

    Args:
      version: current version to be written.
    """
    driver = self._module.GetGomaDriver()
    manifest = {}
    if version:
      manifest['VERSION'] = version
      # Not goma_ctl to ask the platform, let me put 'PLATFORM' param here.
      manifest['PLATFORM'] = self._platform_specific.GetPlatform()
      driver._env.WriteManifest(manifest)
    driver = self._module.GetGomaDriver()
    # Put fake methods instead of actual one to improve performance of tests.
    driver._env.GetCompilerProxyVersion = lambda dummy = None: 'dummy'
    driver._env.ExecCompilerProxy = lambda dummy = None: True
    def DummyControlCompilerProxy(dummy, **_):
      return {'status': True, 'message': 'msg', 'url': 'url', 'pid': '1'}
    driver._env.ControlCompilerProxy = DummyControlCompilerProxy
    driver._env.CompilerProxyRunning = lambda dummy = None: True
    driver._StartCompilerProxy()

  def testPullShouldDownloadAndUpdateManifest(self):
    driver = self._module.GetGomaDriver()
    driver._env._platform = self._platform_specific.GetPlatform()
    driver._Pull()
    manifest = driver._env.ReadManifest(driver._latest_package_dir)
    self.assertTrue(manifest)
    self.assertTrue('PLATFORM' in manifest)
    self.assertEqual(manifest['PLATFORM'],
                     self._platform_specific.GetPlatform())
    self.assertTrue('VERSION' in manifest)

  def testUpdateShouldUpdateManifestAndCompilerProxyButNotAutoRunIt(self):
    """We expect 'update' command updates compiler proxy and manifest.

    However, we do not expect it automatically run compiler proxy if it did not
    run before.
    """
    driver = self._module.GetGomaDriver()
    old_timestamp = os.stat(os.path.join(
        self._tmp_dir, self._TMP_SUBDIR_NAME,
        driver._env._COMPILER_PROXY)).st_mtime
    driver._env._platform = self._platform_specific.GetPlatform()
    self.assertFalse(driver._env.ReadManifest())
    self.assertFalse(driver._env.CompilerProxyRunning())
    driver._Update()
    manifest = driver._env.ReadManifest()
    self.assertTrue(manifest)
    self.assertTrue('PLATFORM' in manifest)
    self.assertTrue('VERSION' in manifest)
    new_timestamp = os.stat(os.path.join(
        self._tmp_dir, self._TMP_SUBDIR_NAME,
        driver._env._COMPILER_PROXY)).st_mtime
    self.assertNotEqual(old_timestamp, new_timestamp,
                        msg=('Update should update the compiler proxy.'
                             'old: %d, new: %d' % (old_timestamp,
                                                   new_timestamp)))
    self.assertFalse(driver._env.CompilerProxyRunning())

  def testUpdateShouldUpdateCompilerProxyAndRestartIfItIsRunning(self):
    self._driver = self._module.GetGomaDriver()
    old_timestamp = os.stat(os.path.join(
        self._tmp_dir, self._TMP_SUBDIR_NAME,
        self._driver._env._COMPILER_PROXY)).st_mtime
    self._driver._env._platform = self._platform_specific.GetPlatform()
    try:
      self._driver._StartCompilerProxy()
      self._driver._Update()
      # Check compiler proxy restarted.
      self.assertTrue(self._driver._env.CompilerProxyRunning())
    finally:
      self._driver._EnsureStopCompilerProxy()
    new_timestamp = os.stat(os.path.join(
        self._tmp_dir, self._TMP_SUBDIR_NAME,
        self._driver._env._COMPILER_PROXY)).st_mtime
    self.assertNotEqual(old_timestamp, new_timestamp,
                        msg=('Update should update the compiler proxy.'
                             'old: %d, new: %d' % (old_timestamp,
                                                   new_timestamp)))

  def testAutoUpdateShouldUpdateManifest(self):
    self.StartWithModifiedVersion(version=1)
    driver = self._module.GetGomaDriver()
    manifest = driver._env.ReadManifest()
    self.assertTrue(manifest)
    self.assertTrue('PLATFORM' in manifest)
    self.assertTrue('VERSION' in manifest)
    self.assertNotEqual(manifest['VERSION'], '1')

  def testShouldNotAutoUpdateNoAutoUpdate(self):
    # Put no_auto_update file.
    no_auto_update_path = os.path.join(self._tmp_dir, self._TMP_SUBDIR_NAME,
                                       'no_auto_update')
    with open(no_auto_update_path, 'w') as handler:
      handler.write('dummy')
    self.StartWithModifiedVersion(version=1)
    # Confirm manifest not changed.
    driver = self._module.GetGomaDriver()
    manifest = driver._env.ReadManifest()
    self.assertTrue(manifest)
    self.assertTrue('PLATFORM' in manifest)
    self.assertTrue('VERSION' in manifest)
    self.assertEqual(manifest['VERSION'], '1')

  def testShouldNotAutoUpdateNoVersionInManifest(self):
    # Manifest is empty by default.
    driver = self._module.GetGomaDriver()
    manifest = driver._env.ReadManifest()
    self.assertFalse(manifest)
    self.StartWithModifiedVersion()
    manifest = driver._env.ReadManifest()
    self.assertFalse(manifest)

  def testAutoUpdateShouldUpdateCompilerProxyEvenIfItIsRunning(self):
    # Start compiler proxy first.
    driver0 = self._module.GetGomaDriver()
    driver0._StartCompilerProxy()

    # Make version in manifest old.
    manifest = driver0._env.ReadManifest()
    manifest['VERSION'] = '1'
    manifest['PLATFORM'] = self._platform_specific.GetPlatform()
    driver0._env.WriteManifest(manifest)

    # Save the current compiler_proxy timestamp.
    old_timestamp = os.stat(os.path.join(
        self._tmp_dir, self._TMP_SUBDIR_NAME,
        driver0._env._COMPILER_PROXY)).st_mtime
    self._driver = self._module.GetGomaDriver()
    self._driver._env._platform = self._platform_specific.GetPlatform()
    try:
      self._driver._StartCompilerProxy()
    finally:
      self._driver._EnsureStopCompilerProxy()
      driver0._EnsureStopCompilerProxy()

    # Time stamp should be changed.
    new_timestamp = os.stat(os.path.join(
        self._tmp_dir, self._TMP_SUBDIR_NAME,
        self._driver._env._COMPILER_PROXY)).st_mtime
    self.assertNotEqual(old_timestamp, new_timestamp,
                        msg=('Update should update the compiler proxy.'
                             'old: %d, new: %d' % (old_timestamp,
                                                   new_timestamp)))

  def testFetchShouldDownloadPackage(self):
    # Get list of supported platforms.
    platforms = []
    for goma_env in self._module._GOMA_ENVS.values():
      platforms.extend([x[1] for x in goma_env.PLATFORM_CANDIDATES])

    # Check packages for them can be downloaded.
    driver = self._module.GetGomaDriver()
    for platform in platforms:
      filename = os.path.join(self._tmp_dir, platform)
      driver._args = ['dummy', platform, filename]
      driver._Fetch()
      self.assertTrue(os.path.isfile(filename))

  def testEnsureShouldRestartCompilerProxyIfBinarySilentlyChanged(self):
    if isinstance(self._platform_specific, WindowsSpecific):
      return  # Windows cannot proceed this test.

    # Start compiler proxy first.
    driver0 = self._module.GetGomaDriver()
    driver0._env._platform = self._platform_specific.GetPlatform()
    driver0._StartCompilerProxy()

    # binary update.
    driver0._env.IsGomaInstalledBefore = lambda dummy = None: False
    driver0._Pull()
    driver0._UpdatePackage()
    before_version = driver0._env.ControlCompilerProxy('/versionz')['message']
    after_version = None

    # start latter compiler_proxy.
    self._driver = self._module.GetGomaDriver()
    self._driver._env._platform = self._platform_specific.GetPlatform()
    try:
      self._driver._EnsureStartCompilerProxy()
      after_version = self._driver._env.ControlCompilerProxy(
          '/versionz')['message']
    finally:
      self._driver._EnsureStopCompilerProxy()
      driver0._EnsureStopCompilerProxy()

    self.assertTrue(after_version)
    self.assertNotEquals(before_version, after_version)
  # TODO: test not silently updated case.

  def testEnsureShouldWorkWithoutFuserCommand(self):
    if isinstance(self._platform_specific, WindowsSpecific):
      return  # Windows don't need this test.

    self._driver = self._module.GetGomaDriver()
    self._driver._env._platform = self._platform_specific.GetPlatform()
    if not self._driver._env._GetFuserPath():
      return  # No need to run this test.
    self._driver._env._GetFuserPath = lambda dummy = None: ''
    try:
      self.assertFalse(self._driver._env.CompilerProxyRunning())
      self._driver._EnsureStartCompilerProxy()
      self.assertTrue(self._driver._env.CompilerProxyRunning())
    finally:
      self._driver._EnsureStopCompilerProxy()

  def testMultipleCompilerProxyInstancesRuns(self):
    if isinstance(self._platform_specific, WindowsSpecific):
      return  # Windows don't support this feature.

    self._driver = self._module.GetGomaDriver()
    self._driver._env._platform = self._platform_specific.GetPlatform()
    try:
      self.assertFalse(self._driver._env.CompilerProxyRunning())
      self._driver._EnsureStartCompilerProxy()
      self.assertTrue(self._driver._env.CompilerProxyRunning())

      prev_envs = {}
      try:
        envs = [
            'GOMA_COMPILER_PROXY_PORT',
            'GOMA_COMPILER_PROXY_SOCKET_NAME',
            'GOMA_COMPILER_PROXY_LOCK_FILENAME']
        for env in envs:
          prev_envs[env] = os.environ.get(env)

        os.environ['GOMA_COMPILER_PROXY_PORT'] = str(int(self._port) + 1)
        os.environ['GOMA_COMPILER_PROXY_SOCKET_NAME'] = 'goma.ipc_test'
        os.environ['GOMA_COMPILER_PROXY_LOCK_FILENAME'] = (
            '/tmp/goma_compiler_proxy.lock.test')
        self.assertFalse(self._driver._env.CompilerProxyRunning())
        self._driver._EnsureStartCompilerProxy()
        self.assertTrue(self._driver._env.CompilerProxyRunning())
      finally:
        self._driver._EnsureStopCompilerProxy()
        for key, value in prev_envs.items():
          if value:
            os.environ[key] = value

    finally:
      self._driver._EnsureStopCompilerProxy()

  def testPullShouldNotUpdateInSecondTime(self):
    driver = self._module.GetGomaDriver()
    driver._env._platform = self._platform_specific.GetPlatform()
    driver._Pull()
    latest_dir = os.path.join(driver._env._dir, driver._latest_package_dir)

    mtime = time.time() - 5
    stat_dict = {}
    for f in os.listdir(latest_dir):
      # change modification time of files in |latest_dir| to check update
      f_path = os.path.join(latest_dir, f)
      os.utime(f_path, (mtime, mtime))
      stat_dict[f] = os.stat(f_path)

    driver._Pull()
    for f in os.listdir(latest_dir):
      if f == 'MANIFEST':
        # In goma_ctl.py pull, we update timestamp of MANIFEST in latest_dir
        # to skip frequent update check in goma_ctl.py ensure_start.
        self.assertNotEqual(stat_dict[f].st_mtime,
                            os.stat(os.path.join(latest_dir, f)).st_mtime)
      else:
        self.assertEqual(stat_dict[f].st_mtime,
                         os.stat(os.path.join(latest_dir, f)).st_mtime)

  def testPullShouldUpdateIfFilesAreNotExist(self):
    driver = self._module.GetGomaDriver()
    driver._env._platform = self._platform_specific.GetPlatform()
    driver._Pull()
    latest_dir = os.path.join(driver._env._dir, driver._latest_package_dir)
    files = os.listdir(latest_dir)
    for f in files:
      # If we make broken manifest, driver's _DownloadedVersion() returns 0.
      # In that case, _ShouldUpdate become true, and file check will not be
      # executed in _Update function.
      # Note that test for this case is testPullShouldUpdateIfManifestIsBroken.
      if f == 'MANIFEST':
        continue
      os.remove(os.path.join(latest_dir, f))

    driver._Pull()
    for f in files:
      self.assertTrue(os.path.exists(os.path.join(latest_dir, f)))

  def testPullShouldUpdateIfFilesAreBroken(self):
    driver = self._module.GetGomaDriver()
    driver._env._platform = self._platform_specific.GetPlatform()
    driver._Pull()
    latest_dir = os.path.join(driver._env._dir, driver._latest_package_dir)
    msg = 'broken'
    files = os.listdir(latest_dir)
    for f in files:
      # If we make broken manifest, driver's _DownloadedVersion() returns 0.
      # In that case, _ShouldUpdate become true, and file check will not be
      # executed in _Update function.
      # Note that test for this case is testPullShouldUpdateIfManifestIsBroken.
      if f == 'MANIFEST':
        continue
      with open(os.path.join(latest_dir, f), 'w') as f:
        f.write(msg)
    # Confirms the files are broken.
    for f in files:
      # ditto.
      if f == 'MANIFEST':
        continue
      with open(os.path.join(latest_dir, f)) as f:
        self.assertEqual(f.read(), msg)

    driver._Pull()
    for f in files:
      with open(os.path.join(latest_dir, f)) as f:
        self.assertNotEqual(f.read(), msg)


class GomaCtlLargeClients5Test(GomaCtlLargeTestCommon):
  """Large Clients5 tests for goma_ctl.py."""

  def setUp(self):
    super(GomaCtlLargeClients5Test, self).setUp()

    os.environ['GOMA_SERVICE_ACCOUNT_JSON_FILE'] = self._oauth2_file
    sys.stderr.write(
        'Using GOMA_SERVICE_ACCOUNT_JSON_FILE = %s\n' % self._oauth2_file)


def GetParameterizedTestSuite(klass, **kwargs):
  test_loader = unittest.TestLoader()
  test_names = test_loader.getTestCaseNames(klass)
  suite = unittest.TestSuite()
  for name in test_names:
    suite.addTest(klass(name, **kwargs))
  return suite


def main():
  test_dir = os.path.abspath(os.path.dirname(__file__))
  os.chdir(os.path.join(test_dir, '..'))

  option_parser = optparse.OptionParser()
  option_parser.add_option('--goma-dir', default=None,
                           help='absolute or relative to goma top dir')
  option_parser.add_option('--platform', help='goma platform type.',
                           default={'linux2': 'linux',
                                    'darwin': 'mac',
                                    'win32': 'win',
                                    'cygwin': 'win'}.get(sys.platform, None),
                           choices=('linux', 'mac', 'win',
                                    'goobuntu', 'chromeos', 'win64'))
  option_parser.add_option('--goma-service-account-json-file',
                           help='goma service account JSON file')
  option_parser.add_option('--small', action='store_true',
                           help='Check small tests only.')
  option_parser.add_option('--verbosity', default=1,
                           help='Verbosity of tests.')
  option_parser.add_option('--port', default='8200',
                           help='compiler_proxy port for large test')
  options, _ = option_parser.parse_args()

  platform_specific = GetPlatformSpecific(options.platform)

  print 'testdir:%s' % test_dir
  if options.goma_dir:
    goma_ctl_path = os.path.abspath(options.goma_dir)
  else:
    goma_ctl_path = os.path.abspath(
        platform_specific.GetDefaultGomaCtlPath(test_dir))
  del sys.argv[1:]

  # Execute test.
  suite = unittest.TestSuite()
  suite.addTest(
      GetParameterizedTestSuite(GomaCtlSmallTest,
                                goma_ctl_path=goma_ctl_path,
                                platform_specific=platform_specific))
  if not options.small:
    suite.addTest(
        GetParameterizedTestSuite(GomaEnvTest,
                                  goma_ctl_path=goma_ctl_path,
                                  platform_specific=platform_specific))
    clients5_key = options.goma_service_account_json_file
    if not clients5_key and platform_specific.GetCred():
      clients5_key = platform_specific.GetCred()
    assert clients5_key
    suite.addTest(
        GetParameterizedTestSuite(GomaCtlLargeClients5Test,
                                  goma_ctl_path=goma_ctl_path,
                                  platform_specific=platform_specific,
                                  oauth2_file=clients5_key,
                                  port=options.port))
  result = unittest.TextTestRunner(verbosity=options.verbosity).run(suite)

  # Return test status as exit status.
  exit_code = 0
  if result.errors:
    exit_code |= 0x01
  if result.failures:
    exit_code |= 0x02
  if exit_code:
    sys.exit(exit_code)


if __name__ == '__main__':
  main()

# TODO: write tests for GomaEnv and GomaBackend.
