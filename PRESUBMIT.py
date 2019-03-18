# Copyright 2012 The Goma Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Top-level presubmit script for goma/client.

To run presubmit,
  $ git cl presubmit --upload

See http://dev.chromium.org/developers/how-tos/depottools/presubmit-scripts
for more details about the presubmit API built into git-cl.
"""

def CheckChangeLintsClean(input_api, output_api):
  """Checks that all '.cc' and '.h' files pass cpplint.py.

  It is clone of depot_tools/presubmit_canned_checks.py, but hacks on
  cpplint.GetHeaderGuardCPPVariable, because our code uses 'DEVTOOLS_GOMA_'
  prefix for header guard.
  """
  _RE_IS_TEST = input_api.re.compile(r'.*tests?.(cc|h)$')
  result = []
  input_api.cpplint._cpplint_state.ResetErrorCounts()
  getHeaderGuardCPPVariable = input_api.cpplint.GetHeaderGuardCPPVariable
  def gomaGetHeaderGuardCPPVariable(filename):
    return 'DEVTOOLS_GOMA_' + getHeaderGuardCPPVariable(filename)
  input_api.cpplint.GetHeaderGuardCPPVariable = gomaGetHeaderGuardCPPVariable

  input_api.cpplint._SetFilters('-build/include,-build/include_order,'
                                '-readability/casting,-runtime/int')

  def Filter(affected_file):
    return input_api.FilterSourceFile(
        affected_file,
        black_list=input_api.DEFAULT_BLACK_LIST+(r".+\.pb\.(h|cc)$",))

  files = [f.AbsoluteLocalPath() for f in
           input_api.AffectedSourceFiles(Filter)]
  for file_name in files:
    if _RE_IS_TEST.match(file_name):
      level = 5
    else:
      level = 4
    input_api.cpplint.ProcessFile(file_name, level)

  if input_api.cpplint._cpplint_state.error_count > 0:
    if input_api.is_committing:
      res_type = output_api.PresubmitError
    else:
      res_type = output_api.PresubmitPromptWarning
    result = [res_type('Changelist failed cpplint.py check.')]

  return result


# TODO: make this work after the fix of depot_tools.
# broken by https://chromium-review.googlesource.com/c/chromium/tools/depot_tools/+/1512058
# Please see also: crbug.com/939959
#def CheckGNGenChecked(input_api, output_api):
#  if not input_api.AffectedFiles(
#      file_filter=lambda x: x.LocalPath().endswith('.c') or
#                            x.LocalPath().endswith('.cc') or
#                            x.LocalPath().endswith('.gn') or
#                            x.LocalPath().endswith('.gni') or
#                            x.LocalPath().endswith('.h') or
#                            x.LocalPath().endswith('.typemap')):
#    return []
#
#  warnings = []
#  with input_api.gclient_utils.temporary_directory() as tmpdir:
#    gn_path = input_api.os_path.join(
#        input_api.gclient_utils.GetBuildtoolsPlatformBinaryPath(),
#        'gn' + input_api.gclient_utils.GetExeSuffix())
#    cmd = [gn_path, 'gen', '--root=%s' % input_api.change.RepositoryRoot(),
#           '--check', tmpdir]
#    proc = input_api.subprocess.Popen(
#        cmd, stdout=input_api.subprocess.PIPE, stderr=input_api.subprocess.PIPE)
#    proc.wait()
#    if proc.returncode != 0:
#      warnings.append(output_api.PresubmitPromptWarning(
#          'Failed to run "gn gen --check".'))
#  return warnings


def CheckChangeOnUpload(input_api, output_api):
  def source_file_filter(x):
    third_party_files = (
        # todo missed owner
        'build/config/mac/sdk_info.py',
        # longer line
        'build/config/mac/mac_sdk.gni',
        # longer line from https://pki.goog/roots.pem
        'client/certs/roots.pem',
    )
    return x.LocalPath() not in third_party_files
  results = []
  results += input_api.canned_checks.CheckChangeHasDescription(
      input_api, output_api)
  results += CheckChangeLintsClean(input_api, output_api)
  results += input_api.canned_checks.CheckChangeHasNoCrAndHasOnlyOneEol(
      input_api, output_api)
  results += input_api.canned_checks.CheckPatchFormatted(input_api, output_api)
  results += input_api.canned_checks.CheckChangeHasNoTabs(
      input_api, output_api)
  results += input_api.canned_checks.CheckChangeTodoHasOwner(
      input_api, output_api, source_file_filter=source_file_filter)
  results += input_api.canned_checks.CheckChangeHasNoStrayWhitespace(
      input_api, output_api)
  results += input_api.canned_checks.CheckLongLines(
      input_api, output_api, 80, source_file_filter=source_file_filter)
  results += input_api.canned_checks.CheckLicense(
      input_api, output_api,
      r'(Copyright 201\d Google Inc. All Rights Reserved.|' +
       'Copyright.*The Chromium Authors. All rights reserved.|' +
       'Copyright.*The Goma Authors. All rights reserved.)')
  results += input_api.canned_checks.CheckDoNotSubmit(
      input_api, output_api)
  results += input_api.canned_checks.RunPylint(
      input_api, output_api,
      black_list=(
          r'build[\\/]config[\\/]mac[\\/].*',
          r'build[\\/]mac[\\/].*',
          r'build[\\/]mac_toolchain.py',
          r'build[\\/]tools[\\/].*',
          r'build[\\/]vs_toolchain.py',
          r'buildtools[\\/].*',
          r'out[\\/].*',
          r'third_party[\\/].*',
          r'tools[\\/].*',
      ))
  results += input_api.canned_checks.CheckGNFormatted(input_api, output_api)
  # TODO: make this work after the fix of depot_tools.
  # broken by https://chromium-review.googlesource.com/c/chromium/tools/depot_tools/+/1512058
  # Please see also: crbug.com/939959
  #
  # results += CheckGNGenChecked(input_api, output_api)
  return results


