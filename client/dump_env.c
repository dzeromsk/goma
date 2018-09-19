// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include <stdio.h>

int main(int argc, char** argv, char** envp) {
  int i;
  char** env;
  for (i = 0; i < argc; i++) {
    fprintf(stdout, "%s\n", argv[i]);
  }
  fflush(stdout);
  for (env = envp; *env; env++) {
    fprintf(stderr, "%s\n", *env);
  }
  fflush(stderr);

  return 0;
}
