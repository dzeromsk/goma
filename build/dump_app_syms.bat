REM Copyright 2013 The Goma Authors. All rights reserved.
REM Use of this source code is governed by a BSD-style license that can be
REM found in the LICENSE file.

REM dump_sym_app for Windows based on:
REM https://chromium.googlesource.com/chromium/+/trunk/build/linux/dump_app_syms

set DUMPSYMS_TMP="%1"
set DUMPSYMS=%DUMPSYMS_TMP:/=\%
set INFILE="%2"
set OUTFILE="%3"

%DUMPSYMS% %INFILE% > %OUTFILE%
