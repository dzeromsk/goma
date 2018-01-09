#!/usr/bin/python
#
# Copyright 2017 The Goma Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.



import argparse
import subprocess
import sys

def main():
  parser = argparse.ArgumentParser()
  parser.add_argument('--generator', help='generator')
  parser.add_argument('--output-path', help='output file path')
  parser.add_argument('inputs', metavar='INPUT', type=str, nargs='+',
                      help='input files')
  args = parser.parse_args()

  # cmd = "$(location :js_embed) $(SRCS) > $@",
  cmd = [ args.generator ] + args.inputs

  p = subprocess.Popen(cmd, stdout=subprocess.PIPE)
  stdout_data, _ = p.communicate()

  if p.returncode != 0:
    print >>sys.stderr, 'failed to run js_embed: exit_status=', p.returncode
    sys.exit(1)

  with open(args.output_path, 'wb') as f:
    f.write(stdout_data)


if __name__ == '__main__':
  main()
