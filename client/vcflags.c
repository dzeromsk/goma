// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


// cl /nologo /Bxvcflags.exe none_exist_file.cc
// will dump all VC++ predefined macro.
// This hack works for VC++ 2008 and 2010.

#include <stdio.h>
#include <stdlib.h>

int main(void) {
  char* env_flags = NULL;

  if (_dupenv_s(&env_flags, NULL, "MSC_CMD_FLAGS") == 0 && env_flags != NULL) {
    printf("%s\n", env_flags);
    free(env_flags);
  }

  if (_dupenv_s(&env_flags, NULL, "MSC_IDE_FLAGS") == 0 && env_flags != NULL) {
    printf("%s\n", env_flags);
    free(env_flags);
  }

  /* We must return EXIT_FAILURE here to stop cl.exe.
     Our goal for vcflags is to dump preprocessor definitions and stop,
     not to actually compile the code.
   */
  return EXIT_FAILURE;
}
