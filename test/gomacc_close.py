#!/usr/bin/env python

# Copyright 2018 The Goma Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""This script is for emulating gomacc closed.

The script open the socket and send request to compiler_proxy, and close
the connection without waiting for the response.
This is the regression test for crbug.com/904532.
"""

import os
import re
import select
import shutil
import socket
import subprocess
import sys
import tempfile

# TODO: remove this when we deprecate python2.
if sys.version_info >= (3, 0):
  import io
  STRINGIO = io.StringIO
else:
  import cStringIO
  STRINGIO = cStringIO.StringIO


SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))
CONTENT_LENGTH_PATTERN = re.compile('\r\nContent-Length:\s*(\d+)\r\n')
READ_TIMEOUT_IN_SEC = 5.0


def GomaDir():
  """Returns goma's directory."""
  return os.environ.get('GOMA_DIR',
                        os.path.join(SCRIPT_DIR, '..', 'out', 'Release'))


def GetGomaccPath():
  """Returns gomacc's path."""
  return os.path.join(GomaDir(), 'gomacc')


def GetGomaCtlPath():
  """Returns compiler_proxy's path."""
  return os.path.join(GomaDir(), 'goma_ctl.py')


def FindAvailablePort():
  """Returns available port number."""
  for p in range(9000, 9100):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    res = s.connect_ex(('localhost', p))
    if res != 0:
      return p
  raise Execption('Cannot find available port')


class CompilerProxyManager(object):
  """compiler_proxy management class."""

  def __init__(self):
    self.goma_ctl = GetGomaCtlPath()
    # Temporary directory used by compiler_proxy.
    self.tmpdir = None
    # TCP port used by compiler_proxy.
    self.port = None
    # IPC socket name used by compiler_proxy.
    self.ipc_socket = None

  def __enter__(self):
    self.tmpdir = tempfile.mkdtemp()
    self.port = FindAvailablePort()
    self.ipc_socket = os.path.join(self.tmpdir, 'goma.ipc')
    os.environ['GOMA_TMP_DIR'] = self.tmpdir
    os.environ['GOMA_COMPILER_PROXY_PORT'] = str(self.port)
    os.environ['GOMA_COMPILER_PROXY_SOCKET_NAME'] = self.ipc_socket
    subprocess.check_call([self.goma_ctl, 'ensure_start'])
    return self

  def __exit__(self, unused_exc_type, unused_exc_value, unused_traceback):
    subprocess.check_call([self.goma_ctl, 'ensure_stop'])
    if self.tmpdir:
      shutil.rmtree(self.tmpdir)

  def IsRunning(self):
    """Returns True if compiler_proxy is running."""
    return subprocess.call([self.goma_ctl, 'status']) == 0

  def Stat(self):
    """Returns goma_ctl.py stat."""
    return subprocess.check_output([self.goma_ctl, 'stat'])



def GetContentLength(header):
  """Returns content-length in HTTP header, or None.

  Args:
    header: HTTP header.

  Returns:
    long content-length or None.

  >>> GetContentLength('POST /e HTTP/1.1\\r\\nContent-Length: 140\\r\\n\\r\\n')
  140L
  >>> GetContentLength('POST /e HTTP/1.1\\r\\nHost: 0.0.0.0\\r\\n\\r\\n')
  """
  matched = CONTENT_LENGTH_PATTERN.search(header)
  if matched:
    return int(matched.group(1))
  return None


def ReadAll(conn):
  """Read all data in connection assuming it is http request.

  Args:
    conn: socket instance to read.

  Returns:
    data read from conn.
  """
  data = STRINGIO()
  while True:
    ready, _, _ = select.select([conn], [], [], READ_TIMEOUT_IN_SEC)
    if not ready:
      raise Exception('read time out')
    snippet = conn.recv(16)
    if not snippet:
      return
    data.write(snippet)
    CRLFCRLF = '\r\n\r\n'
    pos = data.getvalue().find(CRLFCRLF)
    if pos != -1:
      pos += len(CRLFCRLF)
      content_length = GetContentLength(data.getvalue()[:pos])
      if content_length and (len(data.getvalue()) - pos) == content_length:
        return data.getvalue()


def GetGomaccData():
  """Returns data send by gomacc to compiler_proxy."""
  data = None
  socket_dir = tempfile.mkdtemp()
  try:
    socket_name = os.path.join(socket_dir, 'goma.ipc')
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.bind(socket_name)
    sock.listen(1)
    proc = subprocess.Popen(
        [GetGomaccPath(), 'gcc', '-c', 'empty.cc'],
        env={'GOMA_COMPILER_PROXY_SOCKET_NAME': socket_name,
             'PATH': os.environ.get('PATH', '')},
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE)
    conn, _ = sock.accept()
    conn.setblocking(0)
    data = ReadAll(conn)
    proc.kill()
  finally:
    if socket_dir:
      shutil.rmtree(socket_dir)
  return data


def main():
  data = GetGomaccData()
  s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
  with CompilerProxyManager() as cpm:
    before = cpm.Stat()
    s.connect((cpm.ipc_socket))
    s.send(data)
    s.close()
    if not cpm.IsRunning():
      raise Exception('compiler_proxy is not running')
    after = cpm.Stat()
    if before == after:
      raise Exception('compiler_proxy seems not get any request.'
                      ' before=%s\nafter=%s\n' % (before, after))


if __name__ == '__main__':
  main()
