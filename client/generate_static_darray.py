#!/usr/bin/python
#
# Copyright 2011 The Goma Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Generates c++ static double array data for given symbols.

This only works well for small number (~dozens) of keywords set.

Usage:
  To generate enum and static double-array data:
  % generate_static_darray.py \
      --keywords=comma,separated,keys \
      --outfile=<outfile>

"""



from collections import defaultdict
import optparse
import os
import re
import random
import sys


# Constants for goma cpp.
CPP_OUTFILE = 'cpp_parser_darray.h'
CPP_DIRECTIVES = [
  'include', 'import', 'include_next', 'define', 'undef',
  'ifdef', 'ifndef', 'if', 'else', 'endif', 'elif', 'pragma' ]
CPP_COND_DIRECTIVES = [
  'ifdef', 'ifndef', 'if', 'else', 'endif', 'elif' ]

# Constants for testing data.
TEST_OUTFILE = 'static_darray_test_array.h'
TEST_FALLBACK_DEFAULT_KEYWORDS = ['bird', 'bison', 'cat', 'category']


class Trie(object):
  """Representing a simple trie which only has Add operation."""
  class Node:
    def __init__(self):
      self.value = 0
      self.children = defaultdict(Trie.Node)

  def __init__(self):
    self.root = Trie.Node()

  def Add(self, word, value):
    node = self.root
    for c in word:
      node = node.children[c]
    node.value = value


class DoubleArray(object):
  """Representing a simple double-array."""
  class Node(object):
    def __init__(self):
      self.base = 1
      self.check = -1

  class List(list):
    def __getitem__(self, index):
      if index >= len(self):
        self.extend([DoubleArray.Node() for _ in xrange(index - len(self) + 1)])
      return list.__getitem__(self, index)

  def __init__(self, encode=None, end_char=None):
    self.nodes = DoubleArray.List()
    self.encode = encode
    self.base_char = None
    self.end_char = end_char

  def FindBase(self, base, children):
    while True:
      if all((self.nodes[base + self.encode(c)].check < 0 for c in children)):
        return base
      base += 1

  # x -> (c) -> y
  # nodes[x].base + encode(c) = y
  # nodes[y].check = x
  def AddTrieNode(self, trienode, from_base):
    base_start = self.FindBase(0, trienode.children)
    self.nodes[from_base].base = base_start
    for (c, n) in sorted(trienode.children.iteritems()):
      base = base_start + self.encode(c)
      self.nodes[base].check = from_base
    for (c, n) in sorted(trienode.children.iteritems()):
      base = base_start + self.encode(c)
      if n.value:
        self.nodes[base].base = -n.value
      else:
        self.AddTrieNode(n, base)

  def Build(self, dictionary):
    if not self.encode or not self.end_char:
      min_char = min(map(min, dictionary.iterkeys()))
      max_char = max(map(max, dictionary.iterkeys()))
      self.base_char = min_char
      self.end_char = chr(ord(max_char) + 1)
      self.encode = lambda c: ord(c) - ord(min_char) + 1

    trie = Trie()
    for (word, value) in dictionary.iteritems():
      trie.Add(word + self.end_char, value)
    self.AddTrieNode(trie.root, 0)

  def Lookup(self, word):
    index = 0
    for c in word + self.end_char:
      next_index = self.nodes[index].base + self.encode(c)
      if next_index >= len(self.nodes) or index != self.nodes[next_index].check:
        return -1
      index = next_index
    return -self.nodes[index].base

  def DumpAsCArray(self, name, c_type, out):
    out.write('const %s %s[] = {\n' % (c_type, name))
    for i in xrange(0, len(self.nodes), 5):
      out.write('  ')
      out.write(', '.join(['{ %d, %d }' % (n.base, n.check)
                           for n in self.nodes[i:i+5]]))
      out.write(',\n')
    out.write('};\n')


def PrintKeywordEnumAndArray(keywords, prefix, out, values=None,
    print_enum=True, print_keywords=True, perform_check=True):
  """Print c++ enum and double-array list from the given keywords."""

  if print_enum:
    out.write('enum %sValue {\n  ' % prefix)
    camel = lambda word: ''.join([w.title() for w in word.split('_')])
    out.write(',\n  '.join(('k%s%s' % (prefix, camel(k)) for k in keywords)))
    out.write('\n};\n')

  if print_keywords:
    out.write('const char* const k%sKeywords[] = {\n  ' % prefix)
    out.write(',\n  '.join(('"%s"' % k for k in keywords)))
    out.write('\n};\n')

  nodes_name = 'k%sNodes' % prefix
  if not values:
    values = xrange(len(keywords))
  da = DoubleArray()
  da.Build(dict(zip(keywords, values)))

  if perform_check:
    for (word, value) in zip(keywords, values):
      assert value == da.Lookup(word)
      assert -1 == da.Lookup(word[:-1])

  da.DumpAsCArray(nodes_name, 'StaticDoubleArray::Node', out)
  out.write('const StaticDoubleArray k%sArray(' % prefix)
  out.write('%s, %d, \'%c\', %d);\n' %
            (nodes_name,               # nodes
             len(da.nodes),            # nodes_len
             da.base_char,             # encode_base
             da.encode(da.end_char)))  # terminate_code


def GetRandomKeywords(dictfile, maxwords):
  keywords = set()
  if not os.path.exists(dictfile):
    return TEST_FALLBACK_DEFAULT_KEYWORDS
  file_size = os.stat(dictfile)[6]
  with open(dictfile) as d:
    # Try at most 2 * maxwords to prevent eternal loop.
    for _ in xrange(2 * maxwords):
      offset = random.randint(0, file_size - 1)
      d.seek(offset)
      d.readline()
      word = d.readline().strip()
      word = re.sub('\W', '', word)
      if not word:
        continue
      keywords.add(word)
      if len(keywords) >= maxwords:
        break
  return keywords


def main():
  option_parser = optparse.OptionParser()
  option_parser.add_option('', '--keywords', default=None,
                           help='Comma-separated keywords to encode. '
                                'If none is given pre-defined keywords for '
                                'goma will be used.')
  option_parser.add_option('', '--outfile', default=None,
                           help='Output file name.  Will output to stdout '
                                'if none is given.')
  option_parser.add_option('', '--prefix', default='DArray',
                           help='Prefix string used to make up an enum '
                                'and array name.  Used only if --keywords '
                                'is given.')
  option_parser.add_option('-o', '--out-dir', default='.',
                           help='Output directory')
  option_parser.add_option('', '--test', action='store_true', default=False,
                           help='Generate test array data.  Other parameter '
                                'is ignored if this is given.')
  option_parser.add_option('', '--verbose', action='store_true', default=False,
                           help='Be verbose.')
  options, _ = option_parser.parse_args()
  random.seed()

  keywords = []
  if options.test:
    options.outfile = TEST_OUTFILE
    keywords = GetRandomKeywords('/usr/share/dict/words', 30)
  elif options.keywords:
    keywords = options.keywords.split(',')
  elif not options.outfile:
    options.outfile = CPP_OUTFILE

  out = sys.stdout
  outfile = None
  try:
    if options.outfile:
      outfile = os.path.join(options.out_dir, options.outfile)
      out = open(outfile, 'w')

    if keywords:
      PrintKeywordEnumAndArray(keywords, options.prefix, out)
    else:
      PrintKeywordEnumAndArray(CPP_DIRECTIVES, 'Directive', out)
      PrintKeywordEnumAndArray(
          CPP_COND_DIRECTIVES, 'ConditionalDirective', out,
          [CPP_DIRECTIVES.index(v) for v in CPP_COND_DIRECTIVES],
          print_enum=False, print_keywords=False)

    if outfile:
      if options.verbose:
        print 'Generated enum and double-array data into "%s".' % outfile
      out.close()
  except Exception, ex:
    if outfile:
      os.remove(outfile)
      print 'Failed to generate %s: %s' % (outfile, ex)
    sys.exit(1)


if __name__ == "__main__":
  sys.exit(main())
