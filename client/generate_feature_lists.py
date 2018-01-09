#!/usr/bin/python
#
# Copyright 2012 The Goma Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Generates the lists of clangs features.

See: http://clang.llvm.org/docs/LanguageExtensions.html#feature-checking-macros
"""



import re
import urllib2

BASE_URL = 'http://llvm.org/svn/llvm-project/cfe/trunk'
ATTR_URL = BASE_URL + '/include/clang/Basic/Attr.td'
PPMACRO_EXPANSION_URL = BASE_URL + '/lib/Lex/PPMacroExpansion.cpp'
BUILTINS_URL = BASE_URL + '/include/clang/Basic/Builtins.def'
CASE_NAME_PATTERN = re.compile(r'Case\("(.*?)"')
ATTR_NAME_IN_BRACKETS_PATTERN = re.compile(r'(.*?)<"(.*?)">')
CPP_ATTR_NAME_IN_BRACKETS_PATTERN = re.compile(
    r'CXX11<"(.*?)",\s*"(.*?)"(.*?)>')
DECLSPEC_NAME_IN_BRACKETS_PATTERN = re.compile(r'Declspec<"(.*?)">')
SPELLINGS_PATTERN = re.compile(r'let Spellings = \[(.*?)\];',
                               re.MULTILINE | re.DOTALL)
BUILTINS_PATTERN = re.compile(r'BUILTIN\((\w+),')

class Error(Exception):
  pass


def GetRevision():
  trunk = urllib2.urlopen(BASE_URL).read()
  matched = re.search('Revision (\d+):', trunk)
  if matched:
    return matched.group(1)
  raise Error('Failed to parse revision.')


def ScrapeFunction(source, function_name):
  m = re.search(function_name + r'.*{', source)
  if not m:
    raise Error(function_name + ' not found')

  s = source[m.end():]
  m = re.search('\n}', s)
  if not m:
    raise Error(function_name + " doesn't end")

  return s[:m.start()]


# Fetch all required data.
revision = GetRevision()
ppmacro_expansion = urllib2.urlopen(PPMACRO_EXPANSION_URL).read()
attr_td = urllib2.urlopen(ATTR_URL).read()
builtins_def = urllib2.urlopen(BUILTINS_URL).read()

print '// This is auto-generated file from generate_feature_list.py.'
print '// Clang revision: %s.' % revision
print '// *** DO NOT EDIT ***'

# __has_feature
featureFunc = ScrapeFunction(ppmacro_expansion, 'HasFeature')
features = CASE_NAME_PATTERN.findall(featureFunc)
features.sort()
print
print 'static const char* KNOWN_FEATURES[] = {'
for feature in features:
  print '  "%s",' % feature
print '};'
print 'static const unsigned long NUM_KNOWN_FEATURES ='
print '    sizeof(KNOWN_FEATURES) / sizeof(KNOWN_FEATURES[0]);'

# __has_extension
extensionFunc = ScrapeFunction(ppmacro_expansion, 'HasExtension')
extensions = CASE_NAME_PATTERN.findall(extensionFunc)
extensions.sort()
print
print 'static const char* KNOWN_EXTENSIONS[] = {'
for extension in extensions:
  print '  "%s",' % extension
print '};'
print 'static const unsigned long NUM_KNOWN_EXTENSIONS ='
print '    sizeof(KNOWN_EXTENSIONS) / sizeof(KNOWN_EXTENSIONS[0]);'

# __has_attribute
attributes = set()
for spellings in re.findall(SPELLINGS_PATTERN, attr_td):
  for entry in ATTR_NAME_IN_BRACKETS_PATTERN.findall(spellings):
    if entry[0] == "Pragma":
      # Ignore attribute used with pragma.
      # Pragma seems to be only used for #pragma.
      # It also caused the issue. (b/63365915)
      continue
    attr = entry[1]
    if '"' in attr:
      l = attr.split('"')
      attr = l[len(l) - 1]
    if attr and not attr in attributes:
      attributes.add(attr)
attributes = list(attributes)
attributes.sort()
print
print 'static const char* KNOWN_ATTRIBUTES[] = {'
print '\n'.join(['  "%s",' % attr for attr in attributes])
print '};'
print 'static const unsigned long NUM_KNOWN_ATTRIBUTES ='
print '    sizeof(KNOWN_ATTRIBUTES) / sizeof(KNOWN_ATTRIBUTES[0]);'

# __has_cpp_attribute
# CXX11<"clang", "fallthrough", 1>  --> clang::fallthrough
# CXX11<"", "noreturn">             --> noreturn
cpp_attributes = set()
for spellings in re.findall(SPELLINGS_PATTERN, attr_td):
  for attr in CPP_ATTR_NAME_IN_BRACKETS_PATTERN.findall(spellings):
    namespace = attr[0]
    name = attr[1]
    if namespace:
      cpp_attributes.add(namespace + '::' + name)
    else:
      cpp_attributes.add(name)
cpp_attributes = list(cpp_attributes)
cpp_attributes.sort()
print
print 'static const char* KNOWN_CPP_ATTRIBUTES[] = {'
print '\n'.join(['  "%s",' % attr for attr in cpp_attributes])
print '};'
print 'static const unsigned long NUM_KNOWN_CPP_ATTRIBUTES ='
print '    sizeof(KNOWN_CPP_ATTRIBUTES) / sizeof(KNOWN_CPP_ATTRIBUTES[0]);'

# __has_declspec_attribute
declspec_attributes = set()
for spellings in re.findall(SPELLINGS_PATTERN, attr_td):
  for attr in DECLSPEC_NAME_IN_BRACKETS_PATTERN.findall(spellings):
    declspec_attributes.add(attr)
declspec_attributes = list(declspec_attributes)
declspec_attributes.sort()
print
print 'static const char* KNOWN_DECLSPEC_ATTRIBUTES[] = {'
print '\n'.join(['  "%s",' % attr for attr in attributes])
print '};'
print 'static const unsigned long NUM_KNOWN_DECLSPEC_ATTRIBUTES ='
print '    sizeof(KNOWN_DECLSPEC_ATTRIBUTES) /'
print '    sizeof(KNOWN_DECLSPEC_ATTRIBUTES[0]);'

# __has_builtin
builtins = list(set(re.findall(BUILTINS_PATTERN, builtins_def)))
builtins.sort()
print
print 'static const char* KNOWN_BUILTINS[] = {'
print '\n'.join(['  "%s",' % builtin for builtin in builtins])
print '};'
print 'static const unsigned long NUM_KNOWN_BUILTINS ='
print '    sizeof(KNOWN_BUILTINS) /'
print '    sizeof(KNOWN_BUILTINS[0]);'
