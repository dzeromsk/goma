#!/usr/bin/env python
# Copied from Chromium src/third_party/boringssl, and modified for Goma.
# - We do not build/run tests.
# - We do not need some architectures, which is listed in .gitignore.
# - We should not contaminate Chromium issue.
# - It follows existing boringssl update commit description flavor.
# - It automatically removes removed asm files.
#
# Copyright 2015 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Rolls third_party/boringssl/src in DEPS and updates generated build files."""

import os
import os.path
import shutil
import subprocess
import sys


SCRIPT_PATH = os.path.abspath(__file__)
SRC_PATH = os.path.dirname(os.path.dirname(os.path.dirname(SCRIPT_PATH)))
DEPS_PATH = os.path.join(SRC_PATH, 'DEPS')
BORINGSSL_INSIDE_REPO_PATH = os.path.join('third_party', 'boringssl')
BORINGSSL_PATH = os.path.join(SRC_PATH, BORINGSSL_INSIDE_REPO_PATH)
BORINGSSL_SRC_PATH = os.path.join(BORINGSSL_PATH, 'src')

if not os.path.isfile(DEPS_PATH) or not os.path.isdir(BORINGSSL_SRC_PATH):
  raise Exception('Could not find Chromium checkout')

# Pull OS_ARCH_COMBOS out of the BoringSSL script.
sys.path.append(os.path.join(BORINGSSL_SRC_PATH, 'util'))
import generate_build_files

GENERATED_FILES = [
    'BUILD.generated.gni',
    'err_data.c',
]


def IsPristine(repo):
  """Returns True if a git checkout is pristine."""
  cmd = ['git', 'diff', '--ignore-submodules']
  return not (subprocess.check_output(cmd, cwd=repo).strip() or
              subprocess.check_output(cmd + ['--cached'], cwd=repo).strip())


def RevParse(repo, rev):
  """Resolves a string to a git commit."""
  return subprocess.check_output(['git', 'rev-parse', rev], cwd=repo).strip()


def UpdateDEPS(deps, from_hash, to_hash):
  """Updates all references of |from_hash| to |to_hash| in |deps|."""
  with open(deps, 'rb') as f:
    contents = f.read()
    if from_hash not in contents:
      raise Exception('%s not in DEPS' % from_hash)
  contents = contents.replace(from_hash, to_hash)
  with open(deps, 'wb') as f:
    f.write(contents)


def main():
  if len(sys.argv) > 2:
    sys.stderr.write('Usage: %s [COMMIT]' % sys.argv[0])
    return 1

  if not IsPristine(SRC_PATH):
    print >>sys.stderr, 'Goma checkout not pristine.'
    return 0
  if not IsPristine(BORINGSSL_SRC_PATH):
    print >>sys.stderr, 'BoringSSL checkout not pristine.'
    return 0

  if len(sys.argv) > 1:
    new_head = RevParse(BORINGSSL_SRC_PATH, sys.argv[1])
  else:
    subprocess.check_call(['git', 'fetch', 'origin'], cwd=BORINGSSL_SRC_PATH)
    new_head = RevParse(BORINGSSL_SRC_PATH, 'origin/master')

  old_head = RevParse(BORINGSSL_SRC_PATH, 'HEAD')
  if old_head == new_head:
    print 'BoringSSL already up to date.'
    return 0

  print 'Rolling BoringSSL from %s to %s...' % (old_head, new_head)

  UpdateDEPS(DEPS_PATH, old_head, new_head)

  # Checkout third_party/boringssl/src to generate new files.
  subprocess.check_call(['git', 'checkout', new_head], cwd=BORINGSSL_SRC_PATH)

  # Clear the old generated files.
  for (osname, arch, _, _, _) in generate_build_files.OS_ARCH_COMBOS:
    path = os.path.join(BORINGSSL_PATH, osname + '-' + arch)
    try:
      shutil.rmtree(path)
    except OSError as e:
      print 'failed to remove but continue %s: %s' % (path, e)
  for f in GENERATED_FILES:
    path = os.path.join(BORINGSSL_PATH, f)
    os.unlink(path)

  # Generate new ones.
  subprocess.check_call(['python',
                         os.path.join(BORINGSSL_SRC_PATH, 'util',
                                      'generate_build_files.py'),
                         'gn'],
                        cwd=BORINGSSL_PATH)

  # Commit everything except dirs in .gitignore.
  gitignore = []
  try:
    with open(os.path.join(BORINGSSL_PATH, '.gitignore')) as f:
      gitignore = f.read().splitlines()
  except OSError as e:
    print 'cannot access .gitignore file exist: %s' % e
  for entry in gitignore:
    path = os.path.join(BORINGSSL_PATH, entry)
    try:
      shutil.rmtree(path)
    except OSError as e:
      print 'failed to remove but continue %s: %s' % (path, e)
  subprocess.check_call(['git', 'add', DEPS_PATH], cwd=SRC_PATH)
  for (osname, arch, _, _, _) in generate_build_files.OS_ARCH_COMBOS:
    dirname = osname + '-' + arch
    if dirname in gitignore:
      continue
    path = os.path.join(BORINGSSL_PATH, dirname)
    subprocess.check_call(['git', 'add', path], cwd=SRC_PATH)
  for f in GENERATED_FILES:
    path = os.path.join(BORINGSSL_PATH, f)
    subprocess.check_call(['git', 'add', path], cwd=SRC_PATH)

  # Remove removed files from the repository.
  changed_files = subprocess.check_output(
      ['git', 'diff', '--name-only']).splitlines()
  for fname in changed_files:
    if fname.startswith(BORINGSSL_INSIDE_REPO_PATH):
      fname = fname.replace(BORINGSSL_INSIDE_REPO_PATH, BORINGSSL_PATH)
    if not os.path.exists(fname):
      subprocess.check_call(['git', 'rm', fname], cwd=SRC_PATH)

  commits = subprocess.check_output(
      ['git', 'log', '--oneline', '%s..%s' % (old_head, new_head)],
      cwd=BORINGSSL_SRC_PATH)
  message = """Roll client/third_party/boringssl/src %s..%s

This CL is generated from third_party/boringssl/roll_boringssl.py

Changes:
%s
Details: https://boringssl.googlesource.com/boringssl/+log/%s..%s

""" % (old_head[:9], new_head[:9], commits, old_head, new_head)

  subprocess.check_call(['git', 'commit', '-m', message], cwd=SRC_PATH)

  # Print update notes.
  notes = subprocess.check_output(
      ['git', 'log', '--grep', '^Update-Note:', '-i',
       '%s..%s' % (old_head, new_head)], cwd=BORINGSSL_SRC_PATH).strip()
  if len(notes) > 0:
    print "\x1b[1mThe following changes contain updating notes\x1b[0m:\n\n"
    print notes

  return 0


if __name__ == '__main__':
  sys.exit(main())
