#!/usr/bin/python
#
# Copyright 2010 The Goma Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Generate C source file to embed given file.

Usage:
 % genc.py file.txt # generates file.c and file.h
"""

from __future__ import print_function



import optparse
import os
import os.path
import re
import sys

def writeToEscaping(dst, src):
  count = 0
  for c in src.read():
    v = ord(c)
    if v > 127:
      v = -(256 - v)
    dst.write('%3d, ' % v)
    count += 1
    if count >= 16:
      dst.write('\n')
      count = 0

def main():
  parser = optparse.OptionParser()
  parser.add_option('-o', '--out-dir', default='.',
                    help='Output directory')
  parser.add_option('-p', '--prefix', default='',
                    help=('A structure name given by prefix + basename.'
                          'Without this flag, given pathame becomes structure '
                          'name.'))
  options, args = parser.parse_args()
  filename = args[0]
  size = os.stat(filename).st_size
  symbol = re.sub('[^0-9a-zA-Z]', '_', os.path.basename(filename))
  symbol = '%s%s' % (options.prefix, symbol)
  name = os.path.splitext(filename)[0]
  if options.out_dir:
    name = os.path.join(options.out_dir, os.path.basename(name))
  header_file = name + '.h'
  try:
    out = open(header_file, 'w')
    out.write("""
// This is auto-generated file from %(filename)s. DO NOT EDIT.
//
extern "C" {
const int %(symbol)s_size = %(size)d;
extern const char %(symbol)s_start[];
};
""" % {'filename': filename,
       'size': size,
       'symbol': symbol})
    out.close()
  except Exception as ex:
    os.remove(header_file)
    print('Failed to generate %s: %s' % (header_file, ex))
    sys.exit(1)

  c_file = name + '.c'
  try:
    dst = open(c_file, 'wb')
    dst.write("""
// This is auto-generated file from %(filename)s. DO NOT EDIT.
//
const char %(symbol)s_start[] = {
""" % {'filename': filename,
       'symbol': symbol})
    src = open(filename, 'rb')
    writeToEscaping(dst, src)
    dst.write('};\n')
    src.close()
    dst.close()
  except Exception as ex:
    os.remove(c_file)
    print('Failed to generate %s: %s' % (c_file, ex))
    sys.exit(1)


if __name__ == '__main__':
  main()
