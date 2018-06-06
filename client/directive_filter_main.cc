// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "directive_filter.h"

#include <stdio.h>

using devtools_goma::Content;
using devtools_goma::DirectiveFilter;

int main(int argc, char* argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Usage: directive_filter <header or source>\n");
    return 1;
  }

  std::unique_ptr<Content> content(Content::CreateFromFile(argv[1]));
  if (!content.get()) {
    fprintf(stderr, "Cannot read %s\n", argv[1]);
    return 1;
  }

  std::unique_ptr<Content> filtered(
      DirectiveFilter::MakeFilteredContent(*content));

  fwrite(filtered->buf(), sizeof(char), filtered->size(), stdout);
  fflush(stdout);

  return 0;
}
