#!/usr/bin/python

# Copyright 2015 The Goma Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""
Compiles Javascript using Closure Compiler API.

Usage:
% closure_compiler.py <source.js> -o <output.js>

See Also:
https://developers.google.com/closure/compiler/docs/api-tutorial1
"""

import argparse
import copy
import sys
import StringIO
import urllib
import urllib2
import threading


def Communicate(params):
  """Communicate with Closure Compiler API."""
  encoded = urllib.urlencode(params)
  conn = urllib2.urlopen('http://closure-compiler.appspot.com/compile', encoded)
  return conn.read()


class CompileThread(threading.Thread):
  """Thread for compiling."""

  def __init__(self, params):
    threading.Thread.__init__(self)
    self.params = params
    self._out = StringIO.StringIO()

  def run(self):
    data = Communicate(self.params)
    if data:
      self._out.write(data)

  @property
  def output(self):
    return self._out.getvalue()

  @property
  def output_info(self):
    return self.params['output_info']


PARAMS = {
    'js_code': open(sys.argv[1]).read(),
    'compilation_level': 'SIMPLE_OPTIMIZATIONS',
    'output_format': 'text',
    'language': 'ECMASCRIPT5_STRICT',
}


def main():
  parser = argparse.ArgumentParser(description='Closure Compiler')
  parser.add_argument('file', help='a file to compile')
  parser.add_argument('-o', '--output', help='output filename')
  opts = parser.parse_args()

  infos = ['errors', 'warnings', 'compiled_code']
  threads = []
  for output_info in infos:
    params = copy.copy(PARAMS)
    params['output_info'] = output_info
    t = CompileThread(params)
    t.start()
    threads.append(t)

  is_error = False
  compiled_code = None
  for t in threads:
    t.join()
    if t.output:
      if t.output_info == 'compiled_code' and opts.output:
        compiled_code = t.output
        continue
      if t.output_info == 'errors':
        is_error = True
      sys.stderr.write(t.output)
  if compiled_code and not is_error:
    with open(opts.output, 'w') as f:
      f.write(t.output)


if __name__ == '__main__':
  main()
