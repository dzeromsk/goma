#!/usr/bin/env python

# Copyright 2012 The Goma Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# TODO: remove GOMA_COMPILER_PROXY_PORT from code.
#                    it could be 8089, 8090, ... actually.
"""A Script to manage compiler_proxy.

It starts/stops compiler_proxy.exe or compiler_proxy.
"""

from __future__ import print_function




import collections
import copy
import datetime
import glob
import gzip
import hashlib
import io
import json
import os
import random
import re
import shutil
import socket
import string
import subprocess
import sys
import tarfile
import tempfile
import time
try:
  import urllib.parse, urllib.request
  URLOPEN = urllib.request.urlopen
  URLOPEN2 = urllib.request.urlopen
  URLREQUEST = urllib.request.Request
except ImportError:
  import urllib
  import urllib2
  URLOPEN = urllib.urlopen
  URLOPEN2 = urllib2.urlopen
  URLREQUEST = urllib2.Request
import zipfile

SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))
_TRUE_PATTERN = re.compile(r'^([tTyY]|1)')
_DEFAULT_ENV = [
    ('USE_SSL', 'true'),
    ('PING_TIMEOUT_SEC', '60'),
    ('LOG_CLEAN_INTERVAL', str(24 * 60 * 60)),
    ]
_DEFAULT_NO_SSL_ENV = [
    ('STUBBY_PROXY_PORT', '80'),
    ]
_MAX_COOLDOWN_WAIT = 10  # seconds to wait for compiler_proxy to shutdown.
_COOLDOWN_SLEEP = 1  # seconds to each wait for compiler_proxy to shutdown.
_CRASH_DUMP_DIR = 'goma_crash'
_CACHE_DIR = 'goma_cache'
_PRODUCT_NAME = 'Goma'  # product name used for crash report.
_DUMP_FILE_SUFFIX = '.dmp'
_CHECKSUM_FILE = 'sha256.json'

_TIMESTAMP_PATTERN = re.compile('(\d{4}/\d{2}/\d{2} \d{2}:\d{2}:\d{2})')
_TIMESTAMP_FORMAT = '%Y/%m/%d %H:%M:%S'

_CRASH_SERVER = 'https://clients2.google.com/cr/report'
_STAGING_CRASH_SERVER = 'https://clients2.google.com/cr/staging_report'

def _IsGomaFlagTrue(flag_name, default=False):
  """Return true when the given flag is true.

  Note:
  Implementation is based on client/env_flags.h.
  Any values that do not match _TRUE_PATTERN are False.

  Args:
    flag_name: name of a GOMA flag without GOMA_ prefix.
    default: default return value if the flag is not set.

  Returns:
    True if the flag is true.  Otherwise False.
  """
  flag_value = os.environ.get('GOMA_%s' % flag_name, '')
  if not flag_value:
    return default
  return bool(_TRUE_PATTERN.search(flag_value))


def _SetGomaFlagDefaultValueIfEmpty(flag_name, default_value):
  """Set default value to the given flag if it is not set.

  Args:
    flag_name: name of a GOMA flag without GOMA_ prefix.
    default_value: default value to be set if the flag is not set.
  """
  full_flag_name = 'GOMA_%s' % flag_name
  if full_flag_name not in os.environ:
    os.environ[full_flag_name] = default_value


def _ParseManifestContents(manifest):
  """Parse contents of MANIFEST into a dictionary.

  Args:
    manifest: a string of manifest to be parsed.

  Returns:
    The dictionary of key and values in string.
  """
  output = {}
  for line in manifest.splitlines():
    pair = line.strip().split('=', 1)
    if len(pair) == 2:
      output[pair[0].strip()] = pair[1].strip()
  return output


def _IsBadVersion(cur_ver, bad_vers):
  """Check cur_ver is in bad_vers.

  Args:
    cur_ver: current version number.
    bad_vers: a string for bad version, '|' separated.
  Returns:
    True if cur_ver is in bad_vers.
  """
  for ver in bad_vers.split('|'):
    if str(cur_ver) == ver:
      return True
  return False


def _ShouldUpdate(cur_ver, next_ver, bad_vers):
  """Check to update from cur_ver to next_ver.

  Basically, update to newer version (i.e. cur_ver < next_ver).
  If cur_ver is the same as next_ver, then should not update (because
  it is already up-to-date).
  If cur_ver is listed in bad_vers, then
  should update to next_ver even if cur_ver > next_ver.

  Args:
    cur_ver: current version number
    next_ver: next version number
    bad_vers: a string for bad versions, '|' separated.
  Returns:
    True to update cur_ver to next_ver. False otherwise.
  """
  if cur_ver < next_ver:
    return True
  if cur_ver == next_ver:
    return False
  return _IsBadVersion(cur_ver, bad_vers)


def _ParseSpaceSeparatedValues(data):
  """Parses space separated values.

  This function assumes that 1st line is a list of labels.

  e.g. If data is like this:
  | COMMAND   PID
  | bash        1
  | tcsh        2
  This function returns:
  | [{'COMMAND': 'bash', 'PID': '1'}, {'COMMAND': 'tcsh', 'PID': '2'}]

  Args:
    data: space separated values to be parsed.

  Returns:
    a list of dictionaries parsed from data.
  """
  # TODO: remove this if I will not use this on Windows.
  label = None
  contents = []
  for line in data.splitlines():
    entries = line.split()
    if not entries:  # skip empty line.
      continue

    if not label:  # 1st line.
      label = entries
    else:
      contents.append(dict(list(zip(label, entries))))
  return contents


def _ParseLsof(data):
  """Parse lsof.

  Expected inputs are results of:
  - lsof -F pun <filename>
    e.g.
    p1234
    u5678
    f9
  - lsof -F pun -p <pid>
    e.g.
    p1234
    u5678
    fcwd
    n/
    fmem
    n/lib/ld.so
    (f and n repeats, which should be owned by pid:1234 & uid:5678)
  - lsof -F pun -P -i tcp:<port>
    e.g.
    p1234
    u5678
    f9
    nlocalhost:1112

  Although this function might only be used on Posix environment, I put this
  here for ease of testing.

  This function returns:
  | [{'uid': 1L, 'pid': 2L}]
  or
  | [{'uid': 1L, 'pid': 2L, 'name': '/tmp/goma.ipc'}]
  or
  | [{'uid': 1L, 'pid': 2L, 'name': 'localhost:8088'}]

  Args:
    data: result of lsof.

  Returns:
    a list of dictionaries parsed from data.
  """
  pid = None
  uid = None
  contents = []
  for line in data.splitlines():
    if not line:  # skip empty line.
      continue

    code = line[0]
    value = line[1:]

    if code == 'p':
      pid = int(value)
    elif code == 'u':
      uid = int(value)
    elif code == 'n':
      if uid is None or pid is None:
        raise Error('failed to parse lsof result: %s' % data)
      # Omit type=STREAM at the end.
      TYPE_STREAM = ' type=STREAM'
      if value.endswith(TYPE_STREAM):
        value = value[0:-len(TYPE_STREAM)]
      contents.append({'name': value,
                       'uid': uid,
                       'pid': pid})
  return contents


def _GetEnvMatchedCondition(candidates, condition, default_value):
  """Returns environmental variable that matched the condition.

  Args:
    candidates: a list of string to specify environmental variables.
    condition: a condition to decide which value to return.
    default_value: a string to be returned if no candidates matched.

  Returns:
    a string of enviromnental variable's value that matched the condition.
    Otherwise, default_value will be returned.
  """
  for candidate in candidates:
    value = os.environ.get(candidate, '')
    if value and condition(value):
      return value
  return default_value


def _GetTempDirectory():
  """Get temp directory.

  It should match the logic with GetTempDirectory in client/mypath.cc.

  Returns:
    a directory name.
  """
  candidates = ['TEST_TMPDIR', 'TMPDIR', 'TMP']
  return _GetEnvMatchedCondition(candidates, os.path.isdir, '/tmp')


def _GetLogDirectory():
  """Get directory where log exists.

  Returns:
    a directory name.
  """
  candidates = ['GLOG_log_dir', 'TEST_TMPDIR', 'TMPDIR', 'TMP']
  return _GetEnvMatchedCondition(candidates, os.path.isdir, '/tmp')


def _GetUsernameEnv():
  """Get user name.

  Returns:
    an user name that runs this script.
  """
  candidates = ['SUDO_USER', 'USERNAME', 'USER', 'LOGNAME']
  return _GetEnvMatchedCondition(candidates,
                                 lambda x: x != 'root',
                                 '')


def _GetHostname():
  """Gets hostname.

  Returns:
    a hostname of the machine running this script.
  """
  return socket.gethostname()


def _FindCommandInPath(command, find_subdir_rule=os.path.join):
  """Find command in the PATH.

  Args:
    command: a string of command name to find out.
    find_subdir_rule: a function to explore sub directory.

  Returns:
    a string of a full path name if the command is found. Otherwise, None.
  """
  for directory in os.environ['PATH'].split(os.path.pathsep):
    fullpath = find_subdir_rule(directory, command)
    if fullpath and os.path.isfile(fullpath) and os.access(fullpath, os.X_OK):
      return fullpath
  return None


def _ParseFlagz(flagz):
  """Returns a dictionary of user-configured flagz.

  Note that the dictionary will not contain auto configured flags.

  Args:
    flagz: a string returned by compiler_proxy's /flagz.

  Returns:
    a dictionary of user-configured flags.
  """
  envs = {}
  for line in flagz.splitlines():
    line = line.strip()
    if line.endswith('(auto configured)'):
      continue
    pair = line.split('=', 1)
    if len(pair) == 2:
      envs[pair[0].strip()] = pair[1].strip()
  return envs


def _IsGomaFlagUpdated(envs):
  """Returns true if environment is updated from the argument.

  Note: caller MUST NOT set environ after call of this method.
  Otherwise, this function may always return true.

  Args:
    a dictionary of environment to check. e.g. {
      'GOMA_USE_SSL': 'true',
    }

  Returns:
    True if one of values is different from given one.
  """
  for key, original in envs.items():
    new = os.environ.get(key)
    if new != original:
      return True
  for key, value in os.environ.items():
    if key.startswith('GOMA_'):
      if value != envs.get(key):
        return True
  return False


def _CalculateChecksum(filename):
  """Calculate SHA256 checksum of given file.

  Args:
    filename: a string filename to calculate checksum.

  Returns:
    hexdigest string of file contents.
  """
  with open(filename, 'rb') as f:
    return hashlib.sha256(f.read()).hexdigest()


def _GetLogFileTimestamp(glog_log):
  """Returns timestamp when the given glog log was created.

  Args:
    glog_log: a filename of a google-glog log.

  Returns:
    datetime instance when the logfile was created.
    Or, returns None if not a glog file.

  Raises:
    IOError if this function cannot open glog_log.
  """
  with open(glog_log) as f:
    matched = _TIMESTAMP_PATTERN.search(f.readline())
    if matched:
      return datetime.datetime.strptime(matched.group(1), _TIMESTAMP_FORMAT)
  return None


class ConfigError(Exception):
  """Raises when an error found in configurations."""


class Error(Exception):
  """Raises when an error found in the system."""


class PopenWithCheck(subprocess.Popen):
  """subprocess.Popen with automatic exit status check on communicate."""

  def communicate(self, input=None):
    # I do not think argument name |input| is good but this is from the original
    # communicate method.
    # pylint: disable=W0622
    (stdout, stderr) = super(PopenWithCheck, self).communicate(input)
    if self.returncode is None or self.returncode != 0:
      if stdout or stderr:
        raise Error('Error(%s): %s%s' % (self.returncode, stdout, stderr))
      else:
        raise Error('failed to execute subprocess return=%s' % self.returncode)
    return (stdout, stderr)


class GomaDriver(object):
  """Driver of Goma control."""

  def __init__(self, env, backend):
    """Initialize GomaDriver.

    Args:
      env: an instance of GomaEnv subclass.
      backend: an instance of GomaBackend subclass.
    """
    self._env = env
    self._backend = backend
    self._latest_package_dir = 'latest'
    self._action_mappings = {
        'audit': self._CheckAudit,
        'ensure_start': self._EnsureStartCompilerProxy,
        'ensure_stop': self._EnsureStopCompilerProxy,
        'histogram': self._PrintHistogram,
        'jsonstatus': self._PrintJsonStatus,
        'report': self._Report,
        'restart': self._RestartCompilerProxy,
        'showflags': self._PrintFlags,
        'start': self._StartCompilerProxy,
        'stat': self._PrintStatistics,
        'status': self._CheckStatus,
        'stop': self._ShutdownCompilerProxy,
    }
    self._version = 0
    self._manifest = {}
    self._args = []
    self._ReadManifest()
    self._compiler_proxy_running = None

  def _ReadManifest(self):
    """Reads MANIFEST file.
    """
    self._manifest = self._env.ReadManifest()
    if 'VERSION' in self._manifest:
      self._version = int(self._manifest['VERSION'])
    if 'GOMA_API_KEY_FILE' in self._manifest:
      sys.stderr.write('WARNING: GOMA_API_KEY_FILE is deprecated\n')

  def _UpdateManifest(self):
    """Write self._manifest to in MANIFEST."""
    self._env.WriteManifest(self._manifest)

  def _ValidFiles(self, files):
    """Validate files."""
    for f in files:
      filename = os.path.join(self._latest_package_dir, f)
      if not self._env.IsValidMagic(filename):
        print('%s is broken.' % filename)
        return False
    return True

  def _Pull(self):
    """Download the latest package to goma_dir/latest."""
    latest_version, bad_version = self._GetLatestVersion()
    files_to_download = ['MANIFEST', self._env.GetPackageName()]
    if ((_ShouldUpdate(self._DownloadedVersion(), latest_version,
                       bad_version) or
         not self._ValidFiles(files_to_download)) and
        _ShouldUpdate(self._version, latest_version, bad_version)):
      self._env.RemoveDirectory(self._latest_package_dir)
      self._env.MakeDirectory(self._latest_package_dir)
      base_url = self._backend.GetDownloadBaseUrl()
      for f in files_to_download:
        url = '%s/%s' % (base_url, f)
        destination = os.path.join(self._latest_package_dir, f)
        print('Downloading %s' % url)
        self._env.HttpDownload(url,
                               rewrite_url=self._backend.RewriteRequest,
                               headers=self._backend.GetHeaders(),
                               destination_file=destination)
      manifest = self._env.ReadManifest(self._latest_package_dir)
      manifest['PLATFORM'] = self._env.GetPlatform()
      self._env.WriteManifest(manifest, self._latest_package_dir)
    else:
      print('Downloaded package is already the latest version.')

      # update the timestamp of MANIFEST in self._latest_package_dir
      # to skip unnecessary download in ensure_start if the file is valid.
      if self._env.IsValidManifest(self._latest_package_dir):
        manifest = self._env.ReadManifest(directory=self._latest_package_dir)
        self._env.WriteManifest(manifest, directory=self._latest_package_dir)

  def _GetRunningCompilerProxyVersion(self):
    versionz = self._env.ControlCompilerProxy('/versionz', check_running=False)
    if versionz['status']:
      return versionz['message'].strip()
    return None

  def _GetDiskCompilerProxyVersion(self):
    return self._env.GetCompilerProxyVersion().replace('GOMA version',
                                                       '').strip()

  def _GetCompilerProxyHealthz(self):
    """Returns compiler proxy healthz message."""
    healthz = self._env.ControlCompilerProxy('/healthz', check_running=False)
    if healthz['status']:
      return healthz['message'].split()[0].strip()
    return 'unavailable /healthz'

  def _IsCompilerProxySilentlyUpdated(self):
    """Returns True if compiler_proxy is different from running version."""
    disk_version = self._GetDiskCompilerProxyVersion()
    running_version = self._GetRunningCompilerProxyVersion()
    if running_version:
      return running_version != disk_version
    return False

  def _IsGomaFlagUpdated(self):
    flagz = self._env.ControlCompilerProxy('/flagz', check_running=False)
    if flagz['status']:
      return _IsGomaFlagUpdated(_ParseFlagz(flagz['message'].strip()))
    return False

  def _GenericStartCompilerProxy(self, ensure=False):
    self._env.CheckConfig()
    if self._compiler_proxy_running is None:
      self._compiler_proxy_running = self._env.CompilerProxyRunning()
    if (not ensure and self._env.MayUsingDefaultIPCPort() and
        self._compiler_proxy_running):
      self._KillStakeholders()
      self._compiler_proxy_running = False

    can_auto_update = self._env.CanAutoUpdate()
    if can_auto_update:
      bad_version = ''

      if (self._env.ReadManifest(self._latest_package_dir) and
          self._env.IsManifestModifiedRecently(self._latest_package_dir)):
        print('Auto update is skipped'
              ' because %s/MANIFEST was updated recently.' %
              self._latest_package_dir)
        latest_version = self._version
      else:
        latest_version, bad_version = self._GetLatestVersion()
      do_update = False
      if self._version < latest_version:
        print('new goma client found (VERSION=%d).' % latest_version)
        do_update = True
      if _IsBadVersion(self._version, bad_version):
        print('your version (VERSION=%d) is marked as bad version (%s)' % (
            self._version, bad_version))
        do_update = True
      if do_update:
        print('Updating...')
        self._env.AutoUpdate()
        # AutoUpdate may change running status.
        self._compiler_proxy_running = self._env.CompilerProxyRunning()
        self._ReadManifest()

    if 'VERSION' in self._manifest:
      print('Using goma VERSION=%s (%s)' % (
          self._manifest['VERSION'],
          'latest' if can_auto_update else 'no_auto_update'))
    disk_version = self._GetDiskCompilerProxyVersion()
    print('GOMA version %s' % disk_version)
    if ensure and self._compiler_proxy_running:
      healthz = self._GetCompilerProxyHealthz()
      if healthz != 'ok':
        print('goma is not in healthy state: %s' % healthz)
      updated = self._IsCompilerProxySilentlyUpdated()
      flag_updated = self._IsGomaFlagUpdated()
      if flag_updated:
        print('flagz is updated from the previous time.')
      if healthz != 'ok' or updated or flag_updated:
        self._ShutdownCompilerProxy()
        if not self._WaitCooldown():
          self._KillStakeholders()
        self._compiler_proxy_running = False

    if ensure and self._compiler_proxy_running:
      print()
      print('goma is already running.')
      print()
      return

    # AutoUpdate may restart compiler proxy.
    if not self._compiler_proxy_running:
      self._env.ExecCompilerProxy()
      self._compiler_proxy_running = True

    if self._GetStatus():
      running_version = self._GetRunningCompilerProxyVersion()
      if running_version != disk_version:
        print('Updated GOMA version %s' % running_version)
      print()
      print('Now goma is ready!')
      print()
      return
    else:
      sys.stderr.write('Failed to start compiler_proxy.\n')
      try:
        subprocess.check_call(
            [sys.executable,
             os.path.join(self._env.GetScriptDir(), 'goma_auth.py'),
             'info'])
        sys.stderr.write('Temporary error?  try again\n')
      except subprocess.CalledProcessError:
        pass
      sys.exit(1)

  def _StartCompilerProxy(self):
    self._GenericStartCompilerProxy(ensure=False)

  def _EnsureStartCompilerProxy(self):
    self._GenericStartCompilerProxy(ensure=True)

  def _EnsureStopCompilerProxy(self):
    self._ShutdownCompilerProxy()
    if not self._WaitCooldown():
      self._KillStakeholders()

  def _CheckStatus(self):
    status = self._GetStatus()
    if not status:
      sys.exit(1)
    return

  def _GetStatus(self):
    reply = self._env.ControlCompilerProxy('/healthz', need_pids=True)
    print('compiler proxy (pid=%(pid)s) status: %(url)s %(message)s' % reply)
    if reply['message'].startswith('error:'):
        reply['status'] = False
    return reply['status']

  def _ShutdownCompilerProxy(self):
    print('Killing compiler proxy.')
    reply = self._env.ControlCompilerProxy('/quitquitquit')
    print('compiler proxy status: %(url)s %(message)s' % reply)

  def _GetLatestVersion(self):
    """Get latest version of goma.

    Returns:
      A tuple of the version number and bad_version string from MANIFEST

    Raises:
      Error: if failed to determine the latest version.
    """
    try:
      url = self._backend.GetDownloadBaseUrl() + '/MANIFEST'
    except Error as ex:
      oauth2_config_file = os.environ.get('GOMA_OAUTH2_CONFIG_FILE')
      if (oauth2_config_file and
          "not_initialized" in open(oauth2_config_file).read()):
        return 0, ""
      raise ex
    contents = self._env.HttpDownload(
        url,
        rewrite_url=self._backend.RewriteRequest,
        headers=self._backend.GetHeaders())
    manifest = _ParseManifestContents(contents)
    if 'VERSION' in manifest:
      return (int(manifest['VERSION']), manifest.get('bad_version', ''))
    raise Error('Unable to determine the latest version. '
                'Failed to download the latest valid MANIFEST '
                'from the server.\n'
                'Response from server: %s' % contents)

  def _DownloadedVersion(self):
    """Check version of already downloaded goma package.

    Returns:
      The version as integer.
    """
    version = 0
    try:
      version = int(self._env.ReadManifest(self._latest_package_dir)['VERSION'])
    except (KeyError, ValueError):
      pass
    return version

  def _WaitCooldown(self):
    """Wait until compiler_proxy process have finished.

    This will give up after waiting _MAX_COOLDOWN_WAIT seconds.
    It would return False, if other compiler_proxy is running on other IPC port.

    Returns:
      True if compiler_proxy successfully cool down.  Otherwise, False.
    """
    if not self._env.CompilerProxyRunning():
      return True
    print('Waiting for cool down...')
    for cnt in range(_MAX_COOLDOWN_WAIT):
      if not self._env.CompilerProxyRunning():
        break
      print(_MAX_COOLDOWN_WAIT - cnt)
      sys.stdout.flush()
      time.sleep(_COOLDOWN_SLEEP)
    else:
      print('give up')
      return False
    print()
    return True

  def _KillStakeholders(self):
    """Kill and wait until its shutdown."""
    self._env.KillStakeholders()
    if not self._WaitCooldown():
      print('Could not kill compiler_proxy.')
      print('trying to kill it forcefully.')
      self._env.KillStakeholders(force=True)
    if not self._WaitCooldown():
      print('Could not kill compiler_proxy.')
      print('Probably, somebody else also runs compiler_proxy.')

  def _UpdatePackage(self):
    """Update or install latest package.

    We raise error immediately when there is anything wrong instead of
    trying to do something smart. When things go wrong it can be disk
    issues and it's better to have human intervention instead.

    Raises:
      Error: if failed to install the package.
    """
    update_dir = 'update'
    self._env.RemoveDirectory(update_dir)
    self._env.MakeDirectory(update_dir)
    manifest = self._env.ReadManifest(self._latest_package_dir)
    if not manifest or 'VERSION' not in manifest:
      manifest_file = os.path.join(self._latest_package_dir, 'MANIFEST')
      print('MANIFEST (%s) seems to be broken.' % manifest_file)
      print('Going to remove MANIFEST.')
      self._env.RemoveFile(manifest_file)
      print('Please execute update again.')
      raise Error('MANIFEST in downloaded version is broken.')
    latest_version = int(manifest['VERSION'])
    package_file = os.path.join(self._latest_package_dir,
                                self._env.GetPackageName())
    if not self._env.ExtractPackage(package_file, update_dir):
      print('Package file (%s) seems to be broken.' % package_file)
      print('Going to remove package_file.')
      self._env.RemoveFile(package_file)
      print('Please execute update again.')
      raise Error('Failed to extract downloaded package')
    if not self._Audit(update_dir=update_dir):
      print('Failed to verify a file in package.')
      print('Going to remove package_file and update_dir')
      self._env.RemoveFile(package_file)
      self._env.RemoveDirectory(update_dir)
      raise Error('downloaded package is broken')
    if self._env.IsGomaInstalledBefore():
      # This is an update rather than fresh install.
      print('Stopping compiler_proxy ...')
      self._ShutdownCompilerProxy()
      if not self._WaitCooldown():
        self._KillStakeholders()
      self._compiler_proxy_running = False
    print('Updating package to %s ...' % self._env.GetScriptDir())
    if not self._env.InstallPackage(update_dir):
      raise Error('Failed to install package')
    self._version = latest_version
    self._manifest.update(manifest)
    self._UpdateManifest()
    self._env.RemoveDirectory(update_dir)

  def _Update(self):
    """Update goma binary to latest version."""
    latest_version, bad_version = self._GetLatestVersion()
    if _ShouldUpdate(self._version, latest_version, bad_version):
      self._Pull()
      self._env.BackupCurrentPackage()
      rollback = True
      if self._compiler_proxy_running is None:
        self._compiler_proxy_running = self._env.CompilerProxyRunning()
      is_goma_running = self._compiler_proxy_running
      try:
        self._UpdatePackage()
        rollback = False
      finally:
        if rollback:
          print('Failed to update. Rollback...')
          try:
            self._env.RollbackUpdate()
          except Error as inst:
            print(inst)
        if is_goma_running and not self._env.CompilerProxyRunning():
          print(self._env.GetCompilerProxyVersion())
          self._env.ExecCompilerProxy()
          self._compiler_proxy_running = True
    else:
      print('Goma is already up-to-date.')

  def _RestartCompilerProxy(self):
    if self._compiler_proxy_running is None:
      self._compiler_proxy_running = self._env.CompilerProxyRunning()
    if self._compiler_proxy_running:
      self._ShutdownCompilerProxy()
      if not self._WaitCooldown():
        self._KillStakeholders()
      self._compiler_proxy_running = False
    self._StartCompilerProxy()

  def _Fetch(self):
    """Fetch requested goma package."""
    if len(self._args) < 2:
      raise ConfigError('At least platform should be specified to fetch.')
    platform = self._args[1]
    pkg_name = _GetPackageName(platform)
    if len(self._args) > 2:
      outfile = os.path.join(os.getcwd(), self._args[2])
    else:
      outfile = os.path.join(os.getcwd(), pkg_name)
    url = '%s/%s' % (self._backend.GetDownloadBaseUrl(), pkg_name)
    print('Downloading %s' % url)
    self._env.HttpDownload(url,
                           rewrite_url=self._backend.RewriteRequest,
                           headers=self._backend.GetHeaders(),
                           destination_file=outfile)

  def _PrintLatestVersion(self):
    """Print latest version on stdout."""
    latest_version, _ = self._GetLatestVersion()
    print('VERSION=%d' % latest_version)

  def _PrintStatistics(self):
    print(self._env.ControlCompilerProxy('/statz')['message'])

  def _PrintHistogram(self):
    print(self._env.ControlCompilerProxy('/histogramz')['message'])

  def _PrintFlags(self):
    flagz = self._env.ControlCompilerProxy('/flagz', check_running=False)
    print(json.dumps(_ParseFlagz(flagz['message'].strip())))

  def _PrintJsonStatus(self):
    status = self._GetJsonStatus()
    if len(self._args) > 1:
      with open(self._args[1], 'w') as f:
        f.write(status)
    else:
      print(status)

  def _CopyLatestInfoFile(self, command_name, dst):
    """Copies latest *.INFO.* file to destination.

    Args:
      command_name: command_name of *.INFO.* file to copy.
                    e.g. compiler_proxy.
      dst: destination directory name.
    """

    infolog_path = self._env.FindLatestLogFile(command_name, 'INFO')
    if infolog_path:
      self._env.CopyFile(infolog_path,
                         os.path.join(dst, os.path.basename(infolog_path)))
    else:
      print('%s log was not found' % command_name)

  def _CopyGomaccInfoFiles(self, dst):
    """Copies gomacc *.INFO.* file to destination. all gomacc logs after
    latest compiler_proxy started.

    Args:
      dst: destination directory name.
    """

    infolog_path = self._env.FindLatestLogFile('compiler_proxy', 'INFO')
    if not infolog_path:
      return
    compiler_proxy_start_time = _GetLogFileTimestamp(infolog_path)
    if not compiler_proxy_start_time:
      print('compiler_proxy start time could not be inferred. ' +
            'gomacc logs won\'t be included.')
      return
    logs = glob.glob(os.path.join(_GetLogDirectory(), 'gomacc.*.INFO.*'))
    for log in logs:
      timestamp = _GetLogFileTimestamp(log)
      if timestamp and timestamp > compiler_proxy_start_time:
        self._env.CopyFile(log, os.path.join(dst, os.path.basename(log)))

  def _InferBuildDirectory(self):
    """Infer latest build directory from compiler_proxy.INFO.

    This would work for chromium build. Not sure for android etc.

    Returns:
      build directory if inferred. None otherwise.
    """

    infolog_path = self._env.FindLatestLogFile('compiler_proxy', 'INFO')
    if not infolog_path:
      print('compiler_proxy log was not found')
      return None

    build_re = re.compile('.*Task:.*Start.* build_dir=(.*)')

    # List build_dir from compiler_proxy, and take only last 10 build dirs.
    # TODO: move this code to GomaEnv to write tests.
    build_dirs = collections.deque()
    with open(infolog_path) as f:
      for line in f.readlines():
        m = build_re.match(line)
        if m:
          build_dirs.append(m.group(1))
          if len(build_dirs) > 10:
            build_dirs.popleft()

    if not build_dirs:
      return None

    counter = collections.Counter(build_dirs)
    for candidate, _ in counter.most_common():
      if os.path.exists(os.path.join(candidate, '.ninja_log')):
        return candidate
    return None

  def _Report(self):
    tempdir = None
    try:
      tempdir = tempfile.mkdtemp()

      compiler_proxy_is_working = True
      # Check compiler_proxy is working.
      ret = self._env.ControlCompilerProxy('/healthz')
      if ret.get('status', False):
        print('compiler_proxy is working:')
      else:
        compiler_proxy_is_working = False
        print('compiler_proxy is not working:')
        print('  omit compiler_proxy stats')

      if compiler_proxy_is_working:
        keys = ['compilerinfoz', 'histogramz', 'serverz', 'statz']
        for key in keys:
          ret = self._env.ControlCompilerProxy('/' + key)
          if not ret.get('status', False):
            # Failed to get the message. compiler_proxy has died?
            print('  failed to get %s: %s' % (key, ret['message']))
            continue
          print('  include /%s' % key)
          self._env.WriteFile(os.path.join(tempdir, key + '-output'),
                              ret['message'])

      self._CopyLatestInfoFile('compiler_proxy', tempdir)
      self._CopyLatestInfoFile('compiler_proxy-subproc', tempdir)
      self._CopyLatestInfoFile('goma_fetch', tempdir)
      self._CopyGomaccInfoFiles(tempdir)

      build_dir = self._InferBuildDirectory()
      if build_dir:
        print('build directory is inferred as', build_dir)
        src_ninja_log = os.path.join(build_dir, '.ninja_log')
        if os.path.exists(src_ninja_log):
          dst_ninja_log = os.path.join(tempdir, 'ninja_log')
          self._env.CopyFile(src_ninja_log, dst_ninja_log)
        print('  include ninja_log')
      else:
        print('build directory cannot be inferred:')
        print('  omit ninja_log')

      output_filename = os.path.join(_GetTempDirectory(), 'goma-report.tgz')
      self._env.MakeTgzFromDirectory(tempdir, output_filename)

      print()
      print('A report file is successfully created:')
      print(' ', output_filename)
    finally:
      if tempdir:
        shutil.rmtree(tempdir, ignore_errors=True)

  def _GetJsonStatus(self):
    reply = self._env.ControlCompilerProxy('/errorz')
    if not reply['status']:
      return json.dumps({
          'notice': [
              {
                  'version': 1,
                  'compile_error': 'COMPILER_PROXY_UNREACHABLE',
              },
          ]})
    return reply['message']

  def _CheckAudit(self):
    """Audit files in the goma client package.  Exit failure on error."""
    if not self._Audit():
      sys.exit(1)
    return

  def _Audit(self, update_dir=''):
    """Audit files in the goma client package.

    If update_dir is an empty string, it audit current goma files.

    Args:
      update_dir: directory containing goma files to verify.

    Returns:
      False if failed to verify.
    """
    cksums = self._env.LoadChecksum(update_dir=update_dir)
    if not cksums:
      print('No checksum could be loaded.')
      return True
    for filename, checksum in cksums.items():
      # TODO: remove following two lines after the next release.
      # Windows checksum file has non-existing .pdb files.
      if os.path.splitext(filename)[1] == '.pdb':
        continue
      digest = self._env.CalculateChecksum(filename, update_dir=update_dir)
      if checksum != digest:
        print('%s differs: %s != %s' % (filename, checksum, digest))
        return False
    print('All files verified.')
    return True

  def _UploadCrashDump(self):
    """Upload crash dump if exists.

    Important Notice:
    You should not too much trust version number shown in the crash report.
    A version number shown in the crash report may different from what actually
    created a crash dump. Since the version to be sent is collected when
    goma_ctl.py starts up, it may pick the wrong version number if
    compiler_proxy was silently changed without using goma_ctl.py.
    """
    if self._env.IsProductionBinary():
      server_url = _CRASH_SERVER
    else:
      # Set this for testing crash dump upload feature.
      server_url = os.environ.get('GOMACTL_CRASH_SERVER')
      if server_url == 'staging':
        server_url = _STAGING_CRASH_SERVER

    if not server_url:
      # We do not upload crash dump made by the developer's binary.
      return

    try:
      (hash_value, timestamp) = self._GetDiskCompilerProxyVersion().split('@')
      version = '%s.%s' % (hash_value[:8], timestamp)
    except OSError:  # means file not exist and we can ignore it.
      version = ''

    if self._version and version:
      version = 'ver %d %s' % (self._version, version)

    if _IsGomaFlagTrue('SEND_USER_INFO', default=True):
      guid = '%s@%s' % (self._env.GetUsername(), _GetHostname())
    else:
      guid = None

    for dump_file in self._env.GetCrashDumps():
      uploaded = False
      # Upload crash dumps only when we know its version number.
      if version:
        sys.stderr.write(
            'Uploading crash dump: %s to %s\n' % (dump_file, server_url))
        try:
          report_id = self._env.UploadCrashDump(server_url, _PRODUCT_NAME,
                                                version, dump_file, guid=guid)
          report_id_file = os.environ.get('GOMACTL_CRASH_REPORT_ID_FILE')
          if report_id_file:
            with open(report_id_file, 'w') as f:
              f.write(report_id)
          sys.stderr.write('Report Id: %s\n' % report_id)
          uploaded = True
        except Error as inst:
          sys.stderr.write('Failed to upload crash dump: %s\n' % inst)
      if uploaded or self._env.IsOldFile(dump_file):
        try:
          self._env.RemoveFile(dump_file)
        except OSError as e:
          print('failed to remove %s: %s.' % (dump_file, e))

  def _CreateDirectory(self, dir_name, purpose, suppress_message=False):
    info = {
        'purpose': purpose,
        'dir': dir_name,
        }
    if not self._env.IsDirectoryExist(info['dir']):
      if not suppress_message:
        sys.stderr.write('INFO: creating %(purpose)s dir (%(dir)s).\n' % info)
      self._env.MakeDirectory(info['dir'])
    else:
      if not self._env.EnsureDirectoryOwnedByUser(info['dir']):
        sys.stderr.write(
            'Error: %(purpose)s dir (%(dir)s) is not owned by you.\n' % info)
        raise Error('%(purpose)s dir (%(dir)s) is not owned by you.' % info)

  def _CreateGomaTmpDirectory(self):
    tmp_dir = self._env.GetGomaTmpDir()
    self._CreateDirectory(tmp_dir, 'temp')
    sys.stderr.write('using %s as tmpdir\n' % tmp_dir)
    os.environ['GOMA_TMP_DIR'] = tmp_dir

  def _CreateCrashDumpDirectory(self):
    self._CreateDirectory(self._env.GetCrashDumpDirectory(), 'crash dump',
                          suppress_message=True)

  def _CreateCacheDirectory(self):
    self._CreateDirectory(self._env.GetCacheDirectory(), 'cache')

  def _Usage(self):
    """Print usage."""
    program_name = self._env.GetGomaCtlScriptName()
    print('Usage: %s <subcommand>, available subcommands are:' % program_name)
    print('  audit                 audit goma client.')
    print('  ensure_start          start compiler proxy if it is not running')
    print('  ensure_stop           synchronous stop of compiler proxy')
    print('  histogram             show histogram')
    print('  jsonstatus [outfile]  show status report in JSON')
    print('  report                create a report file.')
    print('  restart               restart compiler proxy')
    print('  start                 start compiler proxy')
    print('  stat                  show statistics')
    print('  status                get compiler proxy status')
    print('  stop                  asynchronous stop of compiler proxy')


  def _DefaultAction(self):
    if self._args and not self._args[0] in ('-h', '--help', 'help'):
      print('unknown command: %s' % ' '.join(self._args))
      print()
    self._Usage()

  def Dispatch(self, args):
    """Parse and dispatch commands."""
    is_audit = args and (args[0] == 'audit')
    if not is_audit:
      # when audit, we don't want to run gomacc to detect temp directory,
      # since gomacc might be binary for different platform.
      self._CreateGomaTmpDirectory()
      upload_crash_dump = False
      if upload_crash_dump:
        self._UploadCrashDump()
        self._CreateCrashDumpDirectory()
      self._CreateCacheDirectory()
    self._args = args
    if not args:
      self._GetStatus()
    else:
      self._action_mappings.get(args[0], self._DefaultAction)()


class GomaEnv(object):
  """Goma running environment."""
  # You must implement following protected variables in subclass.
  _GOMACC = ''
  _COMPILER_PROXY = ''
  _GOMA_FETCH = ''
  _COMPILER_PROXY_IDENTIFIER_ENV_NAME = ''
  PLATFORM_CANDIDATES = []
  _DEFAULT_ENV = []
  _DEFAULT_SSL_ENV = []

  def __init__(self, script_dir=SCRIPT_DIR):
    self._dir = os.path.abspath(script_dir)
    self._compiler_proxy_binary = os.environ.get(
        'GOMA_COMPILER_PROXY_BINARY',
        os.path.join(self._dir, self._COMPILER_PROXY))
    self._goma_fetch = None
    if os.path.exists(os.path.join(self._dir, self._GOMA_FETCH)):
      self._goma_fetch = os.path.join(self._dir, self._GOMA_FETCH)
    self._is_daemon_mode = False
    self._gomacc_binary = os.path.join(self._dir, self._GOMACC)
    self._manifest = self.ReadManifest(self._dir)
    self._platform = self._manifest.get('PLATFORM', '')
    # If manifest does not have PLATFORM, goma_ctl.py tries to get it from env.
    # See: b/16274764
    if not self._platform:
      self._platform = os.environ.get('PLATFORM', '')
    self._version = self._manifest.get('VERSION', '')
    self._time = time.time()
    self._goma_params = None
    self._gomacc_socket = None
    self._gomacc_port = None
    self._backup = None
    self._goma_tmp_dir = None
    # TODO: remove this in python3
    self._delete_tmp_dir = False
    self._SetupEnviron()

  def __del__(self):
    if self._delete_tmp_dir:
      shutil.rmtree(self._goma_tmp_dir)

  # methods that may interfere with external environment.
  def MayUsingDefaultIPCPort(self):
    """Returns True if IPC port is not configured in environmental variables.

    If os.environ has self._COMPILER_PORT_IDENTIFIER_ENV_NAME, it may use
    non-default IPC port.  Otherwise, it would use default port.

    Returns:
      True if os.environ does not have self._COMPILER_IDENTIFIER_ENV_NAME.
    """
    return self._COMPILER_PROXY_IDENTIFIER_ENV_NAME not in os.environ

  def GetCompilerProxyVersion(self):
    """Returns compiler proxy version."""
    return PopenWithCheck([self._compiler_proxy_binary, '--version'],
                          stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT).communicate()[0].rstrip()

  def GetScriptDir(self):
    return self._dir

  def IsManifestModifiedRecently(self, directory='', threshold=4*60*60):
    manifest_path = os.path.join(self._dir, directory, 'MANIFEST')
    return time.time() - os.stat(manifest_path).st_mtime < threshold

  def ReadManifest(self, directory=''):
    """Read manifest from MANIFEST file in the directory.

    Args:
      directory: a string of directory name to read the manifest file.

    Returns:
      A dictionary of manifest if the manifest file exist.
      Otherwise, an empty dictionary.
    """
    manifest_path = os.path.join(self._dir, directory, 'MANIFEST')
    if not os.path.isfile(manifest_path):
      return {}
    return _ParseManifestContents(open(manifest_path, 'r').read())

  def WriteManifest(self, manifest, directory=''):
    """Write manifest dictionary to MANIFEST file in the directory.

    Args:
      manifest: a dictionary of the manifest.
      directory: a string of directory name to write the manifest file.
    """
    manifest_path = os.path.join(self._dir, directory, 'MANIFEST')
    if os.path.exists(manifest_path):
      os.chmod(manifest_path, 0o644)
    with open(manifest_path, 'w') as manifest_file:
      for key, value in manifest.items():
        manifest_file.write('%s=%s\n' % (key, value))

  def CheckConfig(self):
    """Checks GomaEnv configurations."""
    socket_name = os.environ.get(self._COMPILER_PROXY_IDENTIFIER_ENV_NAME, '')
    if self._gomacc_socket != socket_name:
      self._gomacc_socket = socket_name
      self._gomacc_port = None # invalidate
    if not os.path.isdir(self._dir):
      raise ConfigError('%s is not directory' % (self._dir))
    if not os.path.isfile(self._compiler_proxy_binary):
      raise ConfigError('compiler_proxy(%s) not exist' % (
          self._compiler_proxy_binary))
    if not os.path.isfile(self._gomacc_binary):
      raise ConfigError('gomacc(%s) not exist' % self._gomacc_binary)
    self._CheckPlatformConfig()

  def _GetCompilerProxyPort(self, proc=None):
    """Gets compiler_proxy's port by "gomacc port".

    Args:
      proc: an instance of subprocess.Popen to poll.

    Returns:
      a string of compiler proxy port number.

    Raises:
      Error: if it cannot get compiler proxy port.
    """
    if self._gomacc_port:
      return self._gomacc_port

    port_error = ''
    stderr = ''

    ping_start_time = time.time()
    ping_timeout_sec = int(os.environ.get('GOMA_PING_TIMEOUT_SEC', '0')) + 20
    ping_deadline = ping_start_time + ping_timeout_sec
    ping_print_time = ping_start_time
    while True:
      current_time = time.time()
      if current_time > ping_deadline:
        break

      if current_time - ping_print_time > 1:
        print('waiting for compiler_proxy...')
        ping_print_time = current_time

      # output glog output to stderr but ignore it because it is usually about
      # failure of connecting IPC port.
      env = os.environ.copy()
      env['GLOG_logtostderr'] = 'true'
      with tempfile.TemporaryFile() as tf:
        # "gomacc port" command may fail until compiler_proxy gets ready.
        # We know gomacc port only output port number to stdout, whose size
        # should be within pipe buffer.
        portcmd = subprocess.Popen([self._gomacc_binary, 'port'],
                                   stdout=subprocess.PIPE,
                                   stderr=tf,
                                   env=env)
        self._WaitWithTimeout(portcmd, 1)
        if portcmd.poll() is None:
          # port takes long time
          portcmd.kill()
          port_error = 'port timedout'
          tf.seek(0)
          stderr = tf.read()
          continue
        portcmd.wait()
        port = portcmd.stdout.read()
        tf.seek(0)
        stderr = tf.read()
      if port and int(port) != 0:
        self._gomacc_port = str(int(port))
        return self._gomacc_port
      if proc and not self._is_daemon_mode:
        proc.poll()
        if proc.returncode is not None:
          raise Error('compiler_proxy is not running %d' % proc.returncode)
    if port_error:
      print(port_error)
    if stderr:
      sys.stderr.write(stderr)
    e = Error('compiler_proxy is not ready?')
    self._GetDetailedFailureReason()
    if proc:
      e = Error('compiler_proxy is not ready? pid=%d' % proc.pid)
      if proc.returncode is not None:
        e = Error('compiler_proxy is not running %d' % proc.returncode)
      proc.kill()
    raise e

  def ControlCompilerProxy(self, command, check_running=True, need_pids=False):
    """Send comamnd to compiler proxy.

    Args:
      command: a string of command to send to the compiler proxy.
      check_running: True if it needs to check compiler_proxy is running
      need_pids: True if it needs stakeholder pids.

    Returns:
      Dict of boolean status, message string, url prefix, and pids.
      if need_pids is True, pid field will be stakeholder's pids if
      compiler_proxy is available.  Otherwise, pid='unknown'.
    """
    self.CheckConfig()
    pids = 'unknown'
    if check_running and not self.CompilerProxyRunning():
      return {'status': False, 'message': 'goma is not running.', 'url': '',
              'pid': pids}
    url_prefix = 'http://127.0.0.1:0'
    try:
      url_prefix = 'http://127.0.0.1:%s' % self._GetCompilerProxyPort()
      url = '%s%s' % (url_prefix, command)
      # When a user set HTTP proxy environment variables (e.g. http_proxy),
      # urllib.urlopen uses them even for communicating with compiler_proxy
      # running in 127.0.0.1, and trying to make the proxy connect to
      # 127.0.0.1 (i.e. the proxy itself), which should not work.
      # We should make urllib.urlopen ignore the proxy environment variables.
      resp = URLOPEN(url, proxies={})
      reply = resp.read()
      if need_pids:
        pids = ','.join(self._GetStakeholderPids())
      return {'status': True, 'message': reply, 'url': url_prefix, 'pid': pids}
    except (Error, socket.error) as ex:
      # urllib.urlopen(url) may raise socket.error, such as [Errno 10054]
      # An existing connection was forcibly closed by the remote host.
      # socket.error uses IOError as base class in python 2.6.
      # note: socket.error changed to an alias of OSError in python 3.3.
      msg = repr(ex)
    return {'status': False, 'message': msg, 'url': url_prefix, 'pid': pids}

  def HttpDownload(self, source_url,
                   rewrite_url=None, headers=None, destination_file=None):
    """Download data from the given URL to the file.

    If self._goma_fetch defined, prefer goma_fetch to urllib2.

    Args:
      source_url: URL to retrieve data.
      rewrite_url: rewrite source_url for urllib2.
      headers: a dictionary to be used in the HTTP header.
      destination_file: file name to store data, if specified None, return
                        contents as string.

    Returns:
      None if provided destination_file, downloaded contents otherwise.

    Raises:
      Error if fetch failed.
    """
    if self._goma_fetch:
      # for proxy, goma_fetch uses $GOMA_PROXY_HOST, $GOMA_PROXY_PORT.
      # headers is used to set Authorization header, but goma_fetch will
      # set appropriate authorization headers from goma env flags.
      # increate timeout.
      env = os.environ
      env['GOMA_HTTP_SOCKET_READ_TIMEOUT_SECS'] = '300.0'
      if destination_file:
        destination_file = os.path.join(self._dir, destination_file)
        with open(destination_file, 'wb') as f:
          retcode = subprocess.call([self._goma_fetch, '--auth', source_url],
                                    env=env,
                                    stdout=f)
          if retcode:
            raise Error('failed to fetch %s: %d' % (source_url, retcode))
        return
      return PopenWithCheck([self._goma_fetch, '--auth', source_url],
                            stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE,
                            env=env).communicate()[0]

    if rewrite_url:
      source_url = rewrite_url(source_url)

    if sys.hexversion < 0x2070900:
      raise Error('Please use python version >= 2.7.9')

    http_req = URLREQUEST(source_url)
    if headers:
      for name, value in headers.items():
        http_req.add_header(name, value)

    r = URLOPEN2(http_req)
    if destination_file:
      with open(os.path.join(self._dir, destination_file), 'wb') as f:
        shutil.copyfileobj(r, f)
      return
    else:
      return r.read()

  def GetGomaTmpDir(self):
    """Get a directory path for goma.

    Returns:
      a directory name.
    """
    if self._goma_tmp_dir:
      return self._goma_tmp_dir

    if not os.path.exists(self._gomacc_binary):
      # When installing goma from `goma_ctl.py update`, gomacc does not exist.
      # In such case, we need to get tempdir from other than gomacc.

      # TODO: Use tmpfile.TemporaryDirectory in python3
      self._goma_tmp_dir = tempfile.mkdtemp()
      self._delete_tmp_dir = True
      return self._goma_tmp_dir

    env = os.environ.copy()
    env['GLOG_logtostderr'] = 'true'
    self._goma_tmp_dir = subprocess.check_output(
        [self._gomacc_binary, 'tmp_dir'],
        env=env).strip()
    return self._goma_tmp_dir

  def GetCrashDumpDirectory(self):
    """Get a directory path that may contain crash dump.

    Returns:
      a directory name.
    """
    return os.path.join(self.GetGomaTmpDir(), _CRASH_DUMP_DIR)

  def GetCacheDirectory(self):
    """Get a directory path that may contain cache.

    Returns:
      a directory name.
    """
    cache_dir = os.environ.get('GOMA_CACHE_DIR')
    if cache_dir:
      return cache_dir

    return os.path.join(self.GetGomaTmpDir(), _CACHE_DIR)

  def GetCrashDumps(self):
    """Get file names of crash dumps.

    Returns:
      a list of full qualified crash dump file names.
      If no crash dump, empty list is returned.
    """
    crash_dir = self.GetCrashDumpDirectory()
    return glob.glob(os.path.join(crash_dir, '*' + _DUMP_FILE_SUFFIX))

  @staticmethod
  def FindLatestLogFile(command_name, log_type):
    """Finds latest *.|log_type|.* file.

    Args:
      command_name: command name of *.|log_type|.* file. e.g. compiler_proxy.

    Returns:
      The latest *.|log_type|.* file path. None if not found.
    """

    info_pattern = os.path.join(_GetLogDirectory(),
                                '%s.*.%s.*' % (command_name, log_type))
    candidates = glob.glob(info_pattern)
    if candidates:
      return sorted(candidates, reverse=True)[0]
    return None

  @classmethod
  def _GetCompressedLatestLog(cls, command_name, log_type):
    """Returns compressed log file if exist.

    Args:
      command_name: command name of *.|log_type|.* file. e.g. compiler_proxy.

    Returns:
      compressed *.|log_type|.* file name.
      Note: caller should remove the file by themselves.
    """
    logfile = cls.FindLatestLogFile(command_name, log_type)
    if not logfile:
      return None

    tf = tempfile.NamedTemporaryFile(delete=False)
    with tf as f_out:
      with gzip.GzipFile(fileobj=f_out) as gzipf_out:
        with open(logfile) as f_in:
          shutil.copyfileobj(f_in, gzipf_out)
    return tf.name

  @staticmethod
  def _BuildFormData(form, boundary, out_fh):
    """Build multipart/form-data from given input.

    Since we cannot always expect existent of requests module,
    we need to have the way to build form data by ourselves.
    Please see also:
    https://tools.ietf.org/html/rfc2046#section-5.1.1

    Args:
      form: a list of list or tuple that represents form input.
            first element of each tuple or list would be treated as a name
            of a form data.
            If a value starts from '@', it is treated as a file like curl.
            e.g. [['prod', 'goma'], ['ver', '160']]
      bondary: a string to represent what value should be used as a boundary.
      out_fh: file handle to output.  Note that you can use StringIO.
    """
    out_fh.write('--%s\r\n' % boundary)
    if isinstance(form, dict):
      form = form.items()
    for i in range(len(form)):
      name, value = form[i]
      filename = None
      content_type = None
      content = value
      if isinstance(value, str) and value.startswith('@'):  # means file.
        filename = value[1:]
        content_type = 'application/octet-stream'
        with open(filename, 'rb') as f:
          content = f.read()
      if filename:
        out_fh.write('content-disposition: form-data; '
                     'name="%s"; '
                     'filename="%s"\r\n' % (name, filename))
      else:
        out_fh.write('content-disposition: form-data; name="%s"\r\n' % name)
      if content_type:
        out_fh.write('content-type: %s\r\n' % content_type)
      out_fh.write('\r\n')
      out_fh.write(content)
      if i == len(form) - 1:
        out_fh.write('\r\n--%s--\r\n' % boundary)
      else:
        out_fh.write('\r\n--%s\r\n' % boundary)

  def UploadCrashDump(self, destination_url, product, version, dump_file,
                      guid=None):
    """Upload crash dump.

    Args:
      destination_url: URL to post data.
      product: a product name string.
      version: a version number string.
      dump_file: a dump file name.
      guid: a unique identifier for this client.

    Returns:
      any messages returned by a server.
    """
    form = {
        'prod': product,
        'ver': version,
        'upload_file_minidump': '@%s' % dump_file,
    }

    dump_size = -1
    try:
      dump_size = os.path.getsize(dump_file)
      sys.stderr.write('Crash dump size: %d\n' % dump_size)
      form['comments'] = 'dump_size:%x' % dump_size
    except OSError:
      pass

    if guid:
      form['guid'] = guid

    cp_logfile = None
    subproc_logfile = None
    form_body_file = None
    try:
      # Since we test goma client with ASan, if we see a goma client crash,
      # it is usually a crash caused by abort.
      # In such a case, ERROR logs should be left.
      # Also, INFO logs are huge, let us avoid to upload them.
      cp_logfile = self._GetCompressedLatestLog(
          'compiler_proxy', 'ERROR')
      subproc_logfile = self._GetCompressedLatestLog(
          'compiler_proxy-subproc', 'ERROR')
      if cp_logfile:
        form['compiler_proxy_logfile'] = '@%s' % cp_logfile
      if subproc_logfile:
        form['subproc_logfile'] = '@%s' % subproc_logfile

      boundary = ''.join(
          random.choice(string.ascii_letters + string.digits)
          for _ in range(32))
      if self._goma_fetch:
        tf = None
        try:
          tf = tempfile.NamedTemporaryFile(delete=False)
          with tf as f:
            self._BuildFormData(form, boundary, f)
          return subprocess.check_output(
              [self._goma_fetch,
               '--noauth',
               '--content_type',
               'multipart/form-data; boundary=%s' % boundary,
               '--data_file', tf.name,
               '--post', destination_url])
        finally:
          if tf:
            os.remove(tf.name)

      if sys.hexversion < 0x2070900:
        raise Error('Please use python version >= 2.7.9')

      body = io.StringIO()
      self._BuildFormData(form, boundary, body)

      headers = {
          'content-type': 'multipart/form-data; boundary=%s' % boundary,
      }
      http_req = URLREQUEST(destination_url, data=body.getvalue(),
                                 headers=headers)
      r = URLOPEN2(http_req)
      out = r.read()
    finally:
      if cp_logfile:
        os.remove(cp_logfile)
      if subproc_logfile:
        os.remove(subproc_logfile)
      if form_body_file:
        os.remove(form_body_file)
    return out

  def WriteFile(self, filename, content):
    with open(filename, 'wb') as f:
      f.write(content)

  def CopyFile(self, from_file, to_file):
    shutil.copy(from_file, to_file)

  def MakeTgzFromDirectory(self, dir_name, output_filename):
    with tarfile.open(output_filename, 'w:gz') as tf:
      tf.add(dir_name)

  def RemoveFile(self, filename):
    filename = os.path.join(self._dir, filename)
    os.remove(filename)

  def _ReadBytesFromFile(self, filename, length):
    filename = os.path.join(self._dir, filename)
    with open(filename, 'rb') as f:
      return f.read(length)

  def IsValidManifest(self, directory=''):
    contents = self.ReadManifest(directory=directory)

    if 'PLATFORM' in contents and 'VERSION' in contents:
      return True
    return False

  def IsValidMagic(self, filename):
    # MANIFEST is special case.
    if os.path.basename(filename) == 'MANIFEST':
      return self.IsValidManifest(os.path.dirname(filename))

    filename = os.path.join(self._dir, filename)

    if not os.path.exists(filename):
      return False

    magics = {
        '.tgz': '\x1F\x8B',
        '.txz': '\xFD7zXZ\x00',
        '.zip': 'PK',
    }
    magic = magics.get(os.path.splitext(filename)[1])
    if not magic:
      return True
    return self._ReadBytesFromFile(filename, len(magic)) == magic

  def RemoveDirectory(self, directory):
    directory = os.path.join(self._dir, directory)
    shutil.rmtree(directory, ignore_errors=True)

  def MakeDirectory(self, directory):
    directory = os.path.join(self._dir, directory)
    os.mkdir(directory, 0o700)
    if not os.path.exists(directory):
      raise Error('Unable to create directory: %s.' % directory)

  def IsDirectoryExist(self, directory):
    directory = os.path.join(self._dir, directory)
    # To avoid symlink attack, the directory should not be symlink.
    return os.path.isdir(directory) and not os.path.islink(directory)

  def IsGomaInstalledBefore(self):
    return os.path.exists(self._compiler_proxy_binary)

  def IsOldFile(self, filename):
    log_clean_interval = int(os.environ.get('GOMA_LOG_CLEAN_INTERVAL', '-1'))
    if log_clean_interval < 0:
      return False
    return os.path.getmtime(filename) < self._time - log_clean_interval

  def _SetupEnviron(self):
    """Sets default environment variables if they are not configured."""
    os.environ['GLOG_logfile_mode'] = str(0o600)
    for flag_name, default_value in _DEFAULT_ENV:
      _SetGomaFlagDefaultValueIfEmpty(flag_name, default_value)
    for flag_name, default_value in self._DEFAULT_ENV:
      _SetGomaFlagDefaultValueIfEmpty(flag_name, default_value)

    if not _IsGomaFlagTrue('USE_SSL'):
      for flag_name, default_value in _DEFAULT_NO_SSL_ENV:
        _SetGomaFlagDefaultValueIfEmpty(flag_name, default_value)

    if _IsGomaFlagTrue('USE_SSL'):
      for flag_name, default_value in self._DEFAULT_SSL_ENV:
        _SetGomaFlagDefaultValueIfEmpty(flag_name, default_value)

  def ExecCompilerProxy(self):
    """Execute compiler proxy in platform dependent way."""
    self._gomacc_port = None  # invalidate gomacc_port cache.
    proc = self._ExecCompilerProxy()
    return self._GetCompilerProxyPort(proc=proc)  # set the new gomacc_port.

  def GetPlatform(self):
    """Get platform.

    If the script do not know the platform, it will ask and set platform member
    varible automatically.

    Returns:
      a string of platform.
    """
    if self._platform:
      return self._platform

    idx = 1
    to_show = []
    for candidate in self.PLATFORM_CANDIDATES:
      to_show.append('%d. %s' % (idx, candidate[0]))
      idx += 1
    print('What is your platform?')
    print('%s ? --> ' % '  '.join(to_show), end="")
    selected = sys.stdin.readline()
    selected = selected.strip()
    try:
      self._platform = self.PLATFORM_CANDIDATES[int(selected) - 1][1]
    except (ValueError, IndexError):
      raise Error('Invalid selection')
    return self._platform

  def CanAutoUpdate(self):
    """Checks auto update is allowed or not.

    Returns:
      True if auto-update is allowed.  Otherwise, False.
    """
    if self._version:
      if not os.path.isfile(os.path.join(self._dir, 'no_auto_update')):
        return True
    return False

  def AutoUpdate(self):
    """Automatically update the client."""
    # Just call myself with update option.
    script = os.path.join(self._dir,
                          os.path.basename(os.path.realpath(__file__)))
    subprocess.check_call(['python', script, 'update'])

  def BackupCurrentPackage(self, backup_dir='backup'):
    """Back up current pacakge.

    Args:
      backup_dir: a string of back up directory name.
    """
    self._backup = []
    # ignore parameter in shutil.copytree can be used to remember the copied
    # files.
    # See Also: http://docs.python.org/2/library/shutil.html

    def RememberCopiedFiles(path, names):
      self._backup.append((path, names))
      return []

    self.RemoveDirectory(backup_dir)
    shutil.copytree(self._dir, os.path.join(self._dir, backup_dir),
                    symlinks=True, ignore=RememberCopiedFiles)

  def RollbackUpdate(self, backup_dir='backup'):
    """Best-effort-rollback from the backup.

    Args:
      backup_dir: a string of back up directory name.

    Raises:
      Error: if the caller did not executed BackupCurrentPackage before.  Or,
             rollback failed.
    """
    if not self._backup:
      raise Error('You should backup files before calling rollback.')
    for entry in self._backup:
      backup_dir_path = entry[0].replace(self._dir,
                                         os.path.join(self._dir, backup_dir))
      # Note:
      # Somebody may ask "Why not shutil.copytree?"
      # It is good for back up but not good for rollback.
      # Since shutil.copytree try to create directories even if it exist,
      # it will try to make existing directory and cause OSError if we use it
      # in rollback process.
      for filename in entry[1]:
        from_name = os.path.join(backup_dir_path, filename)
        to_name = os.path.join(entry[0], filename)
        # After we started to use gomacc to get GomaTmpDir, a file named
        # UNKNOWN.INFO started to be detected on ChromeOS, which does not exist
        # on rollback.
        # Following code is a workaround to avoid failure due to missing
        # UNKNOWN.INFO.
        try:
          from_stat = os.stat(from_name)
        except OSError as e:
          sys.stderr.write('cannot access backuped file: %s' % e)
          continue
        to_stat = os.stat(to_name) if os.path.exists(to_name) else None
        # Skip unchanged file / dir.
        # I expect this also skips running processes since we cannot update it
        # on Windows.
        if (to_stat and
            from_stat.st_size == to_stat.st_size and
            from_stat.st_mode == to_stat.st_mode and
            from_stat.st_mtime == to_stat.st_mtime):
          continue

        if os.path.isfile(from_name) and os.path.isfile(to_name):
          shutil.copy2(from_name, to_name)
        elif os.path.isfile(from_name) and not os.path.exists(to_name):
          shutil.copy2(from_name, to_name)
        elif os.path.isdir(from_name) and os.path.isdir(to_name):
          continue  # do nothing if directory exist.
        elif os.path.isdir(from_name) and not os.path.exists(to_name):
          self.MakeDirectory(to_name)
        else:
          raise Error('Rollback failed.  We cannot rollback %s to %s' %
                      (from_name, to_name))

  def GetPackageName(self):
    """Returns package name based on platform."""
    return _GetPackageName(self.GetPlatform())

  def IsProductionBinary(self):
    """Returns True if compiler_proxy is release version.

    Since all of our release binaries are compiled by chrome-bot,
    we can assume that any binaries compiled by chrome bot would be
    release or release candidate.

    Returns:
      True if compiler_proxy is built by chrome-bot.
      Otherwise, False.
    """
    info = PopenWithCheck([self._compiler_proxy_binary, '--build-info'],
                          stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT).communicate()[0].rstrip()
    return 'built by chrome-bot' in info

  def _GetExtractedDir(self, update_dir):
    """Returns a full path directory name where a package is extracted.

    Args:
      update_dir: a name of update_dir.  This option should be specified when
                  this method is used in update process.

    Returns:
      a directory name goma client files are extracted.
    """
    if not update_dir:
      return self._dir
    return os.path.join(self._dir, update_dir,
                        'goma-%s' % self.GetPlatform())

  def LoadChecksum(self, update_dir=''):
    """Returns a dictionary of checksum.

    For backward compatibility, it returns an empty dictionary if a JSON
    file does not exist.

    Args:
      update_dir: directory containing latest goma files.
                  if empty, load checksum from current goma client directory.

    Returns:
      a dictionary of filename and checksum.
      e.g. {'compiler_proxy': 'abcdef...', ...}
    """
    json_file = os.path.join(self._GetExtractedDir(update_dir), _CHECKSUM_FILE)
    if not os.path.exists(json_file):
      print('%s not exist' % json_file)
      return {}

    with open(json_file) as f:
      return json.load(f)

  def CalculateChecksum(self, filename, update_dir=''):
    """Returns checksum of a file.

    Args:
      filename: a string filename under script dir.
      update_dir: directory containing latest goma files
                  if empty, calculate checksum of files in current goma client
                  directory.

    Returns:
      a checksum of a file.
    """
    return _CalculateChecksum(os.path.join(self._GetExtractedDir(update_dir),
                                           filename))

  # methods need to be implemented in subclasses.
  def _ProcessRunning(self, image_name):
    """Test if any process with image_name is running.

    Args:
      image_name: executable image file name

    Returns:
      boolean value indicating the result.
    """
    raise NotImplementedError('_ProcessRunning should be implemented.')

  def _CheckPlatformConfig(self):
    """Checks platform dependent GomaEnv configurations."""
    pass

  def _ExecCompilerProxy(self):
    """Execute compiler proxy in platform dependent way."""
    raise NotImplementedError('_ExecCompilerProxy should be implemented.')

  def _GetDetailedFailureReason(self, proc=None):
    """Gets detailed failure reason if possible."""
    pass

  def ExtractPackage(self, package_file, update_dir):
    """Extract a platform dependent package.

    Args:
      package_file: a filename of package to extract.
      update_dir: where to extract

    Returns:
      boolean indicating success or failure.
    """
    raise NotImplementedError('ExtractPackage should be implemented.')

  def InstallPackage(self, update_dir):
    """Overwrite self._dir with files in update_dir.

    Args:
      update_dir: directory containing latest goma files

    Returns:
      boolean indicating success or failure.
    """
    raise NotImplementedError('InstallPackage should be implemented.')

  def GetGomaCtlScriptName(self):
    """Get the name of goma_ctl script to be executed by command line."""
    # Subclass may uses its specific variable.
    # pylint: disable=R0201
    return os.environ.get('GOMA_CTL_SCRIPT_NAME',
                          os.path.basename(os.path.realpath(__file__)))

  @staticmethod
  def GetPackageExtension(platform):
    raise NotImplementedError('GetPackageExtension should be implemented.')

  def CompilerProxyRunning(self):
    """Returns True if compiler proxy running.

    Returns:
      True if compiler_proxy is running.  Otherwise, False.
    """
    raise NotImplementedError('CompilerProxyRunning should be implemented.')

  def KillStakeholders(self, force=False):
    """Kills stake holder processes.

    Args:
      force: kills forcefully.

    This will kill all processes having locks compiler_proxy needs.
    """
    raise NotImplementedError('KillStakeholders should be implemented.')

  def WarnNonProtectedFile(self, filename):
    """Warn if access to the file is not limited.

    Args:
      filename: filename to check.
    """
    raise NotImplementedError('WarnNonProtectedFile should be implemented.')

  def EnsureDirectoryOwnedByUser(self, directory):
    """Ensure the directory is owned by the user.

    Args:
      directory: a name of a directory to be checked.

    Returns:
      True if the directory is owned by the user.
    """
    raise NotImplementedError(
        'EnsureDirectoryOwnedByUser should be implemented.')

  def _WaitWithTimeout(self, proc, timeout_sec):
    """Wait proc finish until timeout_sec.

    Args:
      proc: an instance of subprocess.Popen
      timeout_sec: an integer number to represent timeout in sec.
    """
    raise NotImplementedError

  def GetUsername(self):
    user = _GetUsernameEnv()
    if user:
      return user
    user = self._GetUsernameNoEnv()
    if user:
      os.environ['USER'] = user
      return user
    return 'unknown'

  def _GetUsernameNoEnv(self):
    """OS specific way of getting username without environment variables.

    Returns:
      a string of an user name if available.  If not, empty string.
    """
    raise NotImplementedError


class GomaEnvWin(GomaEnv):
  """Goma running environment for Windows."""

  _GOMACC = 'gomacc.exe'
  _COMPILER_PROXY = 'compiler_proxy.exe'
  _GOMA_FETCH = 'goma_fetch.exe'
  # TODO: could be in GomaEnv if env name is the same between
  # posix and win.
  _COMPILER_PROXY_IDENTIFIER_ENV_NAME = 'GOMA_COMPILER_PROXY_SOCKET_NAME'
  _DEFAULT_ENV = [
      ('RPC_EXTRA_PARAMS', '?win'),
      ('COMPILER_PROXY_SOCKET_NAME', 'goma.ipc'),
      ]
  _DEFAULT_SSL_ENV = [
      # Longer read timeout seems to be required on Windows.
      ('HTTP_SOCKET_READ_TIMEOUT_SECS', '90.0'),
      ]
  PLATFORM_CANDIDATES = [
      ('Win64', 'win64'),
      ]
  _GOMA_CTL_SCRIPT_NAME = 'goma_ctl.bat'
  _DEPOT_TOOLS_DIR_PATTERN = re.compile(r'.*[/\\]depot_tools[/\\]?$')

  def __init__(self):
    try:
      self._win32process = __import__('win32process')
      self._win32api = __import__('win32api')
    except ImportError as e:
      print('You don\'t seem to have installed pywin32 module, '
            '`pip install pywin32` may fix.')
      raise e

    GomaEnv.__init__(self)
    self._platform = 'win64'

  @staticmethod
  def GetPackageExtension(platform):
    return 'zip'

  def _ProcessRunning(self, image_name):
    process = PopenWithCheck(['tasklist', '/FI',
                              'IMAGENAME eq %s' % image_name],
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    output = process.communicate()[0]
    return image_name in output

  def _CheckPlatformConfig(self):
    """Checks platform dependent GomaEnv configurations."""
    if not os.path.isfile(os.path.join(self._dir, 'vcflags.exe')):
      raise ConfigError('vcflags.exe not found')

  def _ExecCompilerProxy(self):
    return PopenWithCheck([self._compiler_proxy_binary],
                          creationflags=self._win32process.DETACHED_PROCESS)

  def _GetDetailedFailureReason(self, proc=None):
    pids = self._GetStakeholderPids()
    print('ports are owned by following processes:')
    for pid in pids:
      print(PopenWithCheck(['tasklist', '/FI', 'PID eq %s' % pid],
                           stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT).communicate()[0])

  def ExtractPackage(self, package_file, update_dir):
    """Extract a platform dependent package.

    Args:
      package_file: a filename of package to extract.
      update_dir: where to extract

    Returns:
      boolean indicating success or failure.

    Raises:
      Error: if package does not exist.
    """
    package_file = os.path.join(self._dir, package_file)
    if not os.path.exists(package_file):
      raise Error('Expected package file %s does not exist' % package_file)
    update_dir = os.path.join(self._dir, update_dir)
    print('Extracting package %s to %s ...' % (package_file, update_dir))
    archive = zipfile.ZipFile(package_file)
    archive.extractall(update_dir)
    return True

  def InstallPackage(self, update_dir):
    """Overwrite self._dir with files in update_dir.

    Args:
      update_dir: directory containing latest goma files

    Returns:
      boolean indicating success or failure.
    """
    assert update_dir != ''
    source_dir = self._GetExtractedDir(update_dir)
    # return code may return non zero even if success.
    return_code = subprocess.call(['robocopy', source_dir, self._dir,
                                   '/ns', '/nc', '/nfl', '/ndl', '/np',
                                   '/njh', '/njs'])
    # ROBOCOPY has very, very interesting error codes.
    # see http://ss64.com/nt/robocopy-exit.html
    return return_code < 8

  def GetGomaCtlScriptName(self):
    return os.environ.get('GOMA_CTL_SCRIPT_NAME', self._GOMA_CTL_SCRIPT_NAME)

  def CompilerProxyRunning(self):
    return self._ProcessRunning(self._COMPILER_PROXY)

  def _GetStakeholderPids(self):
    ports = []
    ports.append(os.environ.get('GOMA_COMPILER_PROXY_PORT', '8088'))
    ns = PopenWithCheck(['netstat', '-a', '-n', '-o'],
                        stdout=subprocess.PIPE,
                        stderr=subprocess.STDOUT).communicate()[0]
    listenline = re.compile('.*TCP.*(?:%s).*LISTENING *([0-9]*).*' %
                            '|'.join(ports))
    pids = set()
    for line in ns.splitlines():
      m = listenline.match(line)
      if m:
        pids.add(m.group(1))
    return pids

  def KillStakeholders(self, force=False):
    pids = self._GetStakeholderPids()
    if pids:
      args = []
      if force:
        args.append('/F')
      for pid in pids:
        args.extend(['/PID', pid])
      subprocess.check_call(['taskkill'] + args)

  def WarnNonProtectedFile(self, protocol):
    # TODO: warn for Win.
    pass

  def EnsureDirectoryOwnedByUser(self, directory):
    # TODO: implement for Win.
    return True

  def _WaitWithTimeout(self, proc, timeout_sec):
    import win32api
    import win32con
    import win32event
    try:
      handle = win32api.OpenProcess(
          win32con.PROCESS_QUERY_INFORMATION | win32con.SYNCHRONIZE,
          False, proc.pid)
      ret = win32event.WaitForSingleObject(handle, timeout_sec * 10**3)
      if ret in (win32event.WAIT_TIMEOUT, win32event.WAIT_OBJECT_0):
        return
      raise Error('WaitForSingleObject returned expected value %s' % ret)
    finally:
      if handle:
        win32api.CloseHandle(handle)

  def _GetUsernameNoEnv(self):
    return self._win32api.GetUserName()


class GomaEnvPosix(GomaEnv):
  """Goma running environment for POSIX."""

  _GOMACC = 'gomacc'
  _COMPILER_PROXY = 'compiler_proxy'
  _GOMA_FETCH = 'goma_fetch'
  _COMPILER_PROXY_IDENTIFIER_ENV_NAME = 'GOMA_COMPILER_PROXY_SOCKET_NAME'
  _DEFAULT_ENV = [
      # goma_ctl.py runs compiler_proxy in daemon mode by default.
      ('COMPILER_PROXY_DAEMON_MODE', 'true'),
      ('COMPILER_PROXY_SOCKET_NAME', 'goma.ipc'),
      ('COMPILER_PROXY_LOCK_FILENAME', 'goma_compiler_proxy.lock'),
      ('COMPILER_PROXY_PORT', '8088'),
      ]
  PLATFORM_CANDIDATES = [
      # (Shown name, platform)
      ('Goobuntu', 'goobuntu'),
      ('Chrome OS', 'chromeos'),
      ('MacOS', 'mac'),
      ]
  _LSOF = 'lsof'
  _FUSER = 'fuser'
  _FUSER_PID_PATTERN = re.compile(r'(\d+)')
  _FUSER_USERNAME_PATTERN = re.compile(r'\((\w+)\)')

  def __init__(self):
    GomaEnv.__init__(self)
    # pylint: disable=E1101
    # Configure from sysname in uname.
    if os.uname()[0] == 'Darwin':
      self._platform = 'mac'
    self._fuser_path = None
    self._lsof_path = None
    self._pwd = __import__('pwd')

  @staticmethod
  def GetPackageExtension(platform):
    return 'tgz' if platform == 'mac' else 'txz'

  def _ProcessRunning(self, image_name):
    process = PopenWithCheck(['ps', '-Af'], stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE)
    output = process.communicate()[0]
    return image_name in output

  def _ExecCompilerProxy(self):
    if _IsGomaFlagTrue('COMPILER_PROXY_DAEMON_MODE'):
      self._is_daemon_mode = True
    return PopenWithCheck([self._compiler_proxy_binary],
                          stdout=open(os.devnull, "w"),
                          stderr=subprocess.STDOUT)

  def ExtractPackage(self, package_file, update_dir):
    """Extract a platform dependent package.

    Args:
      package_file: a filename of package to extract.
      update_dir: where to extract

    Returns:
      boolean indicating success or failure.

    Raises:
      Error: if package does not exist.
    """
    package_file = os.path.join(self._dir, package_file)
    if not os.path.exists(package_file):
      raise Error('Expected package file %s does not exist' % package_file)
    update_dir = os.path.join(self._dir, update_dir)
    print('Extracting package to %s ...' % update_dir)
    if os.path.splitext(package_file)[1] == '.tgz':
      tar_options = '-zxf'
    else:
      tar_options = '-Jxf'
    return subprocess.call(['tar', tar_options, package_file, '-C',
                            update_dir]) == 0

  def InstallPackage(self, update_dir):
    """Overwrite self._dir with files in update_dir.

    Args:
      update_dir: directory containing latest goma files

    Returns:
      boolean indicating success or failure.
    """
    assert update_dir != ''
    # TODO: implement a better version for POSIX
    source_files = os.path.join(self._GetExtractedDir(update_dir), '*')
    return subprocess.call(['cp -aRf %s %s' % (source_files, self._dir)],
                           shell=True) == 0

  def _ExecLsof(self, cmd):
    if self._lsof_path is None:
      self._lsof_path = _FindCommandInPath(self._LSOF)
      if not self._lsof_path:
        raise Error('lsof command not found. '
                    'Please install lsof command, or put it in PATH.')
    lsof_command = [self._lsof_path, '-F', 'pun', '-P']
    if cmd['type'] == 'process':
      lsof_command.extend(['-p', cmd['name']])
    elif cmd['type'] == 'file':
      lsof_command.extend([cmd['name']])
    elif cmd['type'] == 'network':
      lsof_command.extend(['-i', cmd['name']])
    else:
      raise Error('unknown cmd type: %s' % cmd)

    # Lsof returns 1 for WARNING even if the result is good enough.
    # It also returns 1 if an owner process is not found.
    ret = subprocess.Popen(lsof_command,
                           stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT).communicate()[0]
    if ret:
      return _ParseLsof(ret)
    return []

  def _GetOwners(self, name, network=False):
    """Get owner pid/uid of file or listen port.

    Args:
      name: name to check owner. e.g. <tmpdir>/goma.ipc or TCP:8088
      network: True if the request is for network socket.

    Returns:
      a list of dictionaries containing owner info.
    """
    # os.path.isfile is not feasible to check an unix domain socket.
    if not network and not os.path.exists(name):
      return []

    if not network and self._GetFuserPath():
      (out, err) = subprocess.Popen([self._GetFuserPath(), '-u', name],
                                    stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE).communicate()
      if out:  # Found at least one owner.
        pids = self._FUSER_PID_PATTERN.findall(out)
        usernames = self._FUSER_USERNAME_PATTERN.findall(err)
        if pids and usernames:
          uids = [int(self._pwd.getpwnam(x).pw_uid) for x in usernames]
          return [{'pid': x[0], 'uid': x[1], 'resource': name}
                  for x in zip(pids, uids)]

    if network:
      return self._ExecLsof({'type': 'network', 'name': name})
    else:
      return self._ExecLsof({'type': 'file', 'name': name})

  def _GetStakeholderPids(self):
    """Get PID of stake holders.

    Args:
      quick: if True, quickly returns result if one of pids is found.

    Returns:
      a list of pids holding compiler_proxy locks and a port.

    Raises:
      Error: if compiler_proxy's lock is onwed by others.
    """
    # os.getuid does not exist in Windows.
    # pylint: disable=E1101
    tmpdir = self.GetGomaTmpDir()
    socket_file = os.path.join(
        tmpdir, os.environ['GOMA_COMPILER_PROXY_SOCKET_NAME'])
    lock_prefix = os.path.join(
        tmpdir, os.environ['GOMA_COMPILER_PROXY_LOCK_FILENAME'])
    port = os.environ['GOMA_COMPILER_PROXY_PORT']
    lock_filename = '%s.%s' % (lock_prefix, port)

    results = []
    results.extend(self._GetOwners(socket_file))
    results.extend(self._GetOwners(lock_filename))
    results.extend(self._GetOwners('TCP:%s' % port, network=True))

    uid = os.getuid()
    if uid != 0:  # root can handle any processes.
      owned_by_others = [x for x in results if x['uid'] != uid]
      if owned_by_others:
        raise Error('compiler_proxy lock and/or socket is owned by others.'
                    ' details=%s' % owned_by_others)

    return set([str(x['pid']) for x in results])

  def KillStakeholders(self, force=False):
    pids = self._GetStakeholderPids()
    if pids:
      args = []
      if force:
        args.append('-9')
      args.extend(list(pids))
      subprocess.check_call(['kill'] + args)

  def _GetFuserPath(self):
    if self._fuser_path is None:
      self._fuser_path = _FindCommandInPath(self._FUSER)
      if not self._fuser_path:
        self._fuser_path = ''
    return self._fuser_path

  def CompilerProxyRunning(self):
    """Returns True if compiler proxy is running."""
    pids = ''
    try:
      # note: mac pgrep does not know --delimitor.
      pids = subprocess.check_output(['pgrep', '-d', ',',
                                      self._COMPILER_PROXY]).strip()
    except subprocess.CalledProcessError as e:
      if e.returncode == 1:
        # compiler_proxy is not running.
        return False
      else:
        # should be fatal error.
        raise e
    if not pids:
      raise Error('executed pgrep but result is not given')

    tmpdir = self.GetGomaTmpDir()
    socket_file = os.path.join(
        tmpdir, os.environ['GOMA_COMPILER_PROXY_SOCKET_NAME'])
    entries = self._ExecLsof({'type': 'process', 'name': pids})
    for entry in entries:
      if socket_file == entry['name']:
        return True
    return False

  def WarnNonProtectedFile(self, filename):
    # This is platform dependent part.
    # pylint: disable=R0201
    if os.path.exists(filename) and os.stat(filename).st_mode & 0o77:
      sys.stderr.write(
          'We recommend to limit access to the file: %(path)s\n'
          'e.g. chmod go-rwx %(path)s\n' % {'path': filename})

  def EnsureDirectoryOwnedByUser(self, directory):
    # This is platform dependent part.
    # pylint: disable=R0201
    # We must use lstat instead of stat to avoid symlink attack (b/69717657).
    st = os.lstat(directory)
    if st.st_uid != os.geteuid():
      return False
    try:
      os.chmod(directory, 0o700)
    except OSError as err:
      sys.stderr.write('chmod failure: %s\n' % err)
      return False
    return True

  def _WaitWithTimeout(self, proc, timeout_sec):
    import signal
    class TimeoutError(Exception):
      """Raised on timeout."""

    def handle_timeout(_signum, _frame):
      raise TimeoutError('timed out')

    signal.signal(signal.SIGALRM, handle_timeout)
    try:
      signal.alarm(timeout_sec)
      proc.wait()
    except TimeoutError:
      pass
    finally:
      signal.alarm(0)
      signal.signal(signal.SIGALRM, signal.SIG_DFL)

  def _GetUsernameNoEnv(self):
    return self._pwd.getpwuid(os.getuid()).pw_name


_GOMA_ENVS = {
    # os.name, GomaEnv subclass name
    'nt': GomaEnvWin,
    'posix': GomaEnvPosix,
    }


def _GetPackageName(platform):
  """Get name of package.

  Args:
    platform: a string of platform name.

  Returns:
    a string of package name of the given platform.

  Raises:
    ConfigError: when given platform is invalid.
  """
  for goma_env in _GOMA_ENVS.values():
    supported = [x[1] for x in goma_env.PLATFORM_CANDIDATES]
    if platform in supported:
      return 'goma-%s.%s' % (platform, goma_env.GetPackageExtension(platform))
  raise ConfigError('Unknown platform %s specified to get package name.' %
                    platform)


class GomaBackend(object):
  """Backend specific configs."""

  def __init__(self, env):
    self._env = env
    self._download_base_url = None
    self._path_prefix = None
    self._stubby_host = None
    self._SetupEnviron()

  def _SetupEnviron(self):
    """Set up backend specific environmental variables."""
    pass

  def _NormalizeBaseUrl(self, resp):
    """Check the URL is valid, and normalize it if needed.

    Args:
      resp: response to the download URL request.

    Returns:
      a string of the download base URL.

    Raises:
      Error: if the given resp is invalid.
    """
    raise NotImplementedError('Please implement _NormalizeBaseUrl')

  def GetDownloadBaseUrl(self):
    """Orchestrate download base url for retrieving manifest file.

    Returns:
      The URL string.

    Raises:
      Error: if failed to obtain download base URL.
    """
    if self._download_base_url:
      return self._download_base_url

    downloadurl_path = '%s/downloadurl' % self._path_prefix
    downloadurl = 'https://%s%s' % (self._stubby_host, downloadurl_path)
    url = self._NormalizeBaseUrl(
        self._env.HttpDownload(downloadurl,
                               rewrite_url=self.RewriteRequest,
                               headers=self.GetHeaders()))
    if 'GOMACHANNEL' in os.environ:
      url += '/%s' % os.environ.get('GOMACHANNEL')
    if url.startswith('http:'):
      url = 'https:' + url[5:]
    self._download_base_url = url
    return url

  def GetPingUrl(self):
    """Get ping url.

    Returns:
       URL for ping
    """
    return 'https://%s/%s/ping' % (self._stubby_host, self._path_prefix)

  def RewriteRequest(self, request):
    """Rewrite request based on backend needs."""
    # This usually do not rewrite but subclass may rewrite.
    # pylint: disable=R0201
    return request

  def GetHeaders(self):
    """Return headers if there are backend specific headers."""
    # This usually returns nothing but subclass may return.
    # pylint: disable=R0201
    return {}


class Clients5Backend(GomaBackend):
  """Backend specific config for Clients5."""

  def _SetupEnviron(self):
    """Set up clients5 backend specific environmental variables."""
    # Set member variables for _GetDownloadBaseUrl.
    self._path_prefix = '/cxx-compiler-service'
    self._stubby_host = 'clients5.google.com'

    # TODO: provide better way to make server know Windows client.
    # Fool proof until we provide the way.
    if (isinstance(self._env, GomaEnvWin) and
        not os.environ.get('GOMA_RPC_EXTRA_PARAMS', '')):
      sys.stderr.write('Please set GOMA_RPC_EXTRA_PARAMS=?win\n')

  def _NormalizeBaseUrl(self, resp):
    """Check the URL is valid, and normalize it if needed.

    Args:
      resp: response to the download URL request.

    Returns:
      a string of the download base URL.

    Raises:
      Error: if the given resp is invalid.
    """
    if resp.startswith('http'):
      return resp
    msg = 'Could not obtain the download base URL.\n'
    msg += ('Server response: %s' % resp)
    raise Error(msg)

  def GetHeaders(self):
    """Return headers if there are backend specific headers."""
    return {}


def GetGomaDriver():
  """Returns a proper instance of GomaEnv subclass based on os.name."""
  if os.name not in _GOMA_ENVS:
    raise Error('Could not find proper GomaEnv for "%s"' % os.name)
  env = _GOMA_ENVS[os.name]()
  backend = Clients5Backend(env)
  return GomaDriver(env, backend)


def main():
  goma = GetGomaDriver()
  goma.Dispatch(sys.argv[1:])
  return 0


if __name__ == '__main__':
  sys.exit(main())
