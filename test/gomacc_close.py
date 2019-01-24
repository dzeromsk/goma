#!/usr/bin/env python

# Copyright 2018 The Goma Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""This script is for emulating gomacc closed.

The script open the socket and send request to compiler_proxy, and close
the connection without waiting for the response.
This is the regression test for crbug.com/904532.
"""

# TODO: compatible with python3.
import cStringIO as StringIO
import os
import re
import select
import shutil
import socket
import subprocess
import sys
import tempfile

SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))
CONTENT_LENGTH_PATTERN = re.compile('\r\nContent-Length:\s*(\d+)\r\n')
READ_TIMEOUT_IN_SEC = 5.0


def GetGomaccPath():
  """Returns gomacc's path."""
  return os.environ.get(
      'GOMACC_PATH',
      os.path.join(SCRIPT_DIR, '..', 'out', 'Release', 'gomacc'))


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
    return long(matched.group(1))
  return None


def ReadAll(conn):
  """Read all data in connection assuming it is http request.

  Args:
    conn: socket instance to read.

  Returns:
    data read from conn.
  """
  data = StringIO.StringIO()
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


def GetIPCPath():
  """Returns Unix domain socket for IPC."""
  tmp_dir = subprocess.check_output([GetGomaccPath(), 'tmp_dir']).strip()
  return os.path.join(tmp_dir, 'goma.ipc')


def main():
  data = GetGomaccData()
  s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
  s.connect((GetIPCPath()))
  s.send(data)
  s.close()


if __name__ == '__main__':
  main()
