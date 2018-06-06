# Copyright 2012 The Goma Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Top-level presubmit script for goma/client.

To run presubmit,
  $ git cl presubmit --upload

See http://dev.chromium.org/developers/how-tos/depottools/presubmit-scripts
for more details about the presubmit API built into git-cl.
"""

def CheckChangeLintsClean(input_api, output_api, source_file_filter=None):
  """Checks that all '.cc' and '.h' files pass cpplint.py.

  It is clone of depot_tools/presubmit_canned_checks.py, but hacks on
  cpplint.GetHeaderGuardCPPVariable, because our code uses 'DEVTOOLS_GOMA_'
  prefix for header guard.
  """
  _RE_IS_TEST = input_api.re.compile(r'.*tests?.(cc|h)$')
  result = []
  import cpplint
  # pylint: disable=W0212
  cpplint._cpplint_state.ResetErrorCounts()

  getHeaderGuardCPPVariable = cpplint.GetHeaderGuardCPPVariable
  def gomaGetHeaderGuardCPPVariable(filename):
    return 'DEVTOOLS_GOMA_' + getHeaderGuardCPPVariable(filename)
  cpplint.GetHeaderGuardCPPVariable = gomaGetHeaderGuardCPPVariable

  cpplint._SetFilters('-build/include,-build/include_order,'
                      '-readability/casting,-runtime/int')
  files = [f.AbsoluteLocalPath() for f in
           input_api.AffectedSourceFiles(source_file_filter)]
  for file_name in files:
    if _RE_IS_TEST.match(file_name):
      level = 5
    else:
      level = 4
    cpplint.ProcessFile(file_name, level)

  if cpplint._cpplint_state.error_count > 0:
    if input_api.is_committing:
      res_type = output_api.PresubmitError
    else:
      res_type = output_api.PresubmitPromptWarning
    result = [res_type('Changelist failed cpplint.py check.')]

  return result


def CheckChangeOnUpload(input_api, output_api):
  results = []
  results += input_api.canned_checks.CheckChangeHasDescription(
      input_api, output_api)
  results += CheckChangeLintsClean(input_api, output_api)
  results += input_api.canned_checks.CheckChangeHasNoCrAndHasOnlyOneEol(
      input_api, output_api)
  results += input_api.canned_checks.CheckChangeHasNoTabs(
      input_api, output_api)
  results += input_api.canned_checks.CheckChangeTodoHasOwner(
      input_api, output_api)
  results += input_api.canned_checks.CheckChangeHasNoStrayWhitespace(
      input_api, output_api)
  results += input_api.canned_checks.CheckLongLines(input_api, output_api, 80)
  results += input_api.canned_checks.CheckLicense(
      input_api, output_api,
      r'(Copyright 201\d Google Inc. All Rights Reserved.|' +
       'Copyright.*The Chromium Authors. All rights reserved.)')
  results += input_api.canned_checks.CheckDoNotSubmit(
      input_api, output_api)
  results += input_api.canned_checks.RunPylint(
      input_api, output_api,
      black_list=(r'third_party[\\/].*',
                  r'build[\\/]tools[\\/].*',
                  r'build[\\/]vs_toolchain.py',
                  r'buildtools[\\/]clang_format[\\/]script[\\/].*',
                  r'tools[\\/].*',
                  r'out[\\/].*',
                  r'build[\\/](Debug|Release).*'))
  results += input_api.canned_checks.CheckGNFormatted(input_api, output_api)
  return results


