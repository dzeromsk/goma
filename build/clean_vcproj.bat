REM Copyright 2012 The Goma Authors. All rights reserved.
REM Use of this source code is governed by a BSD-style license that can be
REM found in the LICENSE file.

pushd
cd %~dp0
rd /s /q ipch
rd /s /q Debug
rd /s /q Release
del /q *.sln *.vcproj *.user *.filters *.vcxproj *.ncb *.sdf
cd ..\lib
del /q *.sln *.vcproj *.user *.filters *.vcxproj *.ncb *.sdf
cd ..\client
del /q *.sln *.vcproj *.user *.filters *.vcxproj *.ncb *.sdf
cd ..\third_party
del /q *.sln *.vcproj *.user *.filters *.vcxproj *.ncb *.sdf
popd

