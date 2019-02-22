#!/usr/bin/env python
# Copyright 2012 The Goma Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Creates goma client release archives."""

from __future__ import print_function



import hashlib
import optparse
import os
import re
import shutil
import subprocess
import sys
import tarfile
import zipfile

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
GOMACC_CMDS = ('g++', 'gcc', 'javac', 'cc', 'c++', 'clang', 'clang++')
CHROMEOS_GOMACC_CMDS = (
    'i686-pc-linux-gnu-gcc',
    'i686-pc-linux-gnu-g++',
    'armv7a-cros-linux-gnueabi-gcc',
    'armv7a-cros-linux-gnueabi-g++',
    'x86_64-pc-linux-gnu-gcc',
    'x86_64-pc-linux-gnu-g++',
    'arm-none-eabi-gcc',
    'arm-none-eabi-g++',
    'x86_64-cros-linux-gnu-gcc',
    'x86_64-cros-linux-gnu-g++')


try:
  os.symlink
except AttributeError:
  # no os.symlink on Windows.
  def __fake_symlink(src, dst):
    raise NotImplementedError('symlink %s %s' % (src, dst))
  os.symlink = __fake_symlink


def CreatePlatformGomacc(distname, platform):
  """Creates gomacc symlinks in distname.

  Args:
    distname: distribution directory
    platform: platform name
  """
  if platform in ('linux', 'mac', 'goobuntu', 'chromeos'):
    gomacc = list(GOMACC_CMDS)
  else:
    raise NotImplementedError(platform)
  if platform == 'chromeos':
    gomacc.extend(CHROMEOS_GOMACC_CMDS)
  for cmd in gomacc:
    os.symlink('gomacc', os.path.join(distname, cmd))


def DeleteSymlinksToGomacc(distname):
  """Deletes symlinks to gomacc in distname.

  Args:
    distname: distribution directory
  """
  for name in os.listdir(distname):
    abs_name = os.path.join(distname, name)
    # since symlink only works on posix, we do not need to check gomacc.exe.
    if os.path.islink(abs_name) and os.readlink(abs_name) == 'gomacc':
      os.remove(abs_name)


def InstallPlatformFiles(distname, platform):
  """Install platform specific files in distname.

  Args:
    distname: distribution directory
    platform: platname name.
  Returns:
    a list of files.
  """
  if platform in ('linux', 'mac', 'goobuntu'):
    return
  if platform != 'chromeos':
    raise NotImplementedError(platform)
  files = ['goma-wrapper', 'goma-make']
  for f in files:
    shutil.copy(f, distname)


def CreateAndroidDir(distname, platform):
  """Creates android support directory if necessary.

  Args:
    distname: distribution directory.
    platform: platform name.
  Returns:
    a list of files to be released.
  """
  if platform in ('linux', 'mac', 'goobuntu'):
    distname = os.path.join(distname, 'android')
    shutil.rmtree(distname, ignore_errors=True)
    os.mkdir(distname)
    for cmd in ('gomacc', 'compiler_proxy', 'goma_fetch',
                'goma_auth.py', 'goma_ctl.py'):
      os.symlink(os.path.join('..', cmd), os.path.join(distname, cmd))
    for cmd in GOMACC_CMDS:
      os.symlink('gomacc', os.path.join(distname, cmd))
    for prefix in ('arm-eabi', 'arm-linux-androideabi',
                   'i686-android-linux', 'i686-linux',
                   'i686-unknown-linux-gnu',
                   'i686-unknown-linux-gnu-i686-unknown-linux-gnu',
                   'sh-linux-gnu'):
      os.symlink('gomacc', os.path.join(distname, '%s-gcc' % prefix))
      os.symlink('gomacc', os.path.join(distname, '%s-g++' % prefix))


def MkTarball(src, dst_tar_file):
  """Make tarball.

  Note: basename of |src| would show up as a file's directory name in
  a tar file.
  e.g.
  If you give "/tmp/foo/bar" that has followings inside as |src|:
    /tmp/foo/bar/gomacc
    /tmp/foo/bar/compiler_proxy
  then, the generated archive would have files with following path names:
    bar/gomacc
    bar/compiler_proxy

  Args:
    src: an absolute path name of the directory to archive.
    dst_tar_file: a filename (with extension) to output tarball.
  """
  dirname = os.path.dirname(src)
  assert os.path.abspath(dirname)
  def Filter(info):
    assert info.name.startswith(dirname[1:])
    info.name = info.name[len(dirname):]
    if info.name:
      print('Adding: %s' % info.name)
      return info

  mode = 'w:gz'
  if os.path.splitext(dst_tar_file)[1] == '.tbz':
    mode = 'w:bz2'

  with tarfile.open(dst_tar_file, mode) as tf:
    for path in os.listdir(src):
      tf.add(os.path.join(src, path), filter=Filter)


def MkZip(src, dst_zip_file):
  """Make zip file.

  Note: basename of |src| would show up as a file's directory name in
  a zip file.
  e.g.
  If you give "c:\\Users\\foo\\bar" that has followings inside as |src|:
    c:\\Users\\foo\\bar\\gomacc
    c:\\Users\\foo\\bar\\compiler_proxy
  then, the generated archive would have files with following path names:
    bar\\gomacc
    bar\\compiler_proxy

  Args:
    src: a full path name of the directory to archive.
    dst_tar_file: an output zip filename.
  """
  dirname = os.path.dirname(src)
  with zipfile.ZipFile(dst_zip_file, 'w',
                       compression=zipfile.ZIP_DEFLATED) as zf:
    for dirpath, _, filenames in os.walk(src):
      for f in filenames:
        orig_path = os.path.join(dirpath, f)
        path = orig_path[len(dirname) + 1:]
        print('Adding: %s' % path)
        zf.write(orig_path, arcname=path)


def main():
  option_parser = optparse.OptionParser()
  option_parser.add_option('--platform',
                           default={'linux2': 'linux',
                                    'darwin': 'mac',
                                    'win32': 'win',
                                    'cygwin': 'win'}.get(sys.platform, None),
                           choices=('linux', 'mac', 'win',
                                    'goobuntu', 'chromeos', 'win64'),
                           help='platform name')
  option_parser.add_option('--build_dir', default='out',
                           help='directory of build output')
  option_parser.add_option('--target_dir', default='Release',
                           help='subdirectory in build_dir to archive')
  option_parser.add_option('--dist_dir', default='..',
                           help='directory to put tgz')
  option_parser.add_option('--store_in_commit_dir', action='store_true',
                           help='store tgz in commit dir under dist_dir')

  options, args = option_parser.parse_args()
  if args:
    option_parser.error('Unsupported args: %s' % ' '.join(args))
  dist_top_absdir = os.path.abspath(options.dist_dir)
  dist_absdir = dist_top_absdir
  src_dir = os.getcwd()

  if not os.path.isdir(dist_absdir):
    os.makedirs(dist_absdir, 0o755)
  if options.store_in_commit_dir:
    gitproc = subprocess.Popen(['git', 'log', '-1', '--pretty=%H'],
                               shell=(sys.platform == 'win32'),
                               stdout=subprocess.PIPE,
                               cwd=src_dir)
    commit = gitproc.communicate()[0].strip()
    if gitproc.returncode:
      print('ERROR: git failed to get commit. exit=%d' % gitproc.returncode)
      return gitproc.returncode
    if not commit:
      print('ERROR: empty commit hash?')
      return 1
    print('Commit: %s' % commit)
    dist_absdir = os.path.join(dist_absdir, commit)
    shutil.rmtree(dist_absdir, ignore_errors=True)
    os.mkdir(dist_absdir, 0o755)

  os.chdir(os.path.join(src_dir, options.build_dir, options.target_dir))

  distname = 'goma-%s' % options.platform
  shutil.rmtree(distname, ignore_errors=True)

  print('Preparing files in %s in %s...' % (distname, os.getcwd()))
  print('mkdir %s' % distname)
  os.mkdir(distname, 0o755)
  if options.platform in ('win', 'win64'):
    for cmd in ('gomacc.exe', 'compiler_proxy.exe', 'vcflags.exe',
                'goma_fetch.exe'):
      shutil.copy(cmd, distname)
      pdb = os.path.splitext(cmd)[0] + '.pdb'
      if not os.path.exists(pdb):
        pdb = cmd + '.pdb'
      shutil.copy(pdb, distname)
    for f in ('.vpython', 'goma_auth.py', 'goma_ctl.py', 'goma_ctl.bat',
              'diagnose_goma_log.py', 'compiler_proxy.sym', 'sha256.json',
              'gomacc.sym', 'LICENSE'):
      shutil.copy(f, distname)
  else:
    for f in ('.vpython', 'gomacc', 'compiler_proxy', 'goma_fetch',
              'report_env.sh', 'diagnose_goma_log.py', 'compiler_proxy.sym',
              'goma_auth.py', 'goma_ctl.py', 'sha256.json', 'gomacc.sym',
              'LICENSE'):
      shutil.copy(f, distname)
    CreatePlatformGomacc(distname, options.platform)
    InstallPlatformFiles(distname, options.platform)
    CreateAndroidDir(distname, options.platform)

  # Create an archive file.
  if options.platform in ('win', 'win64'):
    target_file = os.path.join(dist_absdir, '%s.zip' % distname)
    print('Archiving in %s.zip' % distname)
    MkZip(os.path.realpath(distname), target_file)
    compiler_proxy_path = 'compiler_proxy.exe'
  else:
    target_file = os.path.join(dist_absdir, '%s.tgz' % distname)
    print('Archiving in %s.tgz' % distname)
    MkTarball(os.path.realpath(distname), target_file)
    compiler_proxy_path = os.path.join(distname, 'compiler_proxy')
    # Since CIPD uses this directory for creating CIPD package,
    # we need to remove gomacc symlinks.
    DeleteSymlinksToGomacc(distname)

  print()
  print('%s created.' % target_file)

  cp = open(compiler_proxy_path, 'rb')
  # Finds user-agent string (starts with 'compiler-proxy' and ends with 'Z',
  # which is the last letter of timestamp) for compiler_proxy_user_agent.csv
  # e.g. "compiler-proxy built by goma at " +
  # "9d6775c48911ad1b80624720121a5e0d0c320adf@1330938783 " +
  # "on 2012-03-05T09:20:30.931701Z"
  m = re.search(r'(compiler-proxy[- a-zA-Z0-9:.@]*Z)', cp.read())
  if m:
    print('"%s",,%s' % (m.group(1), options.platform))
  else:
    print('ERROR: user-agent string not found in %s' % compiler_proxy_path)
    return 1
  cp.close()
  return 0


if __name__ == '__main__':
  sys.exit(main())
