// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "options.h"

namespace file {

Options Defaults() {
  return Options();
}

Options CreationMode(mode_t mode) {
  Options opt;
  opt.creation_mode_ = mode;
  return opt;
}

Options Overwrite() {
  Options opt;
  opt.overwrite_ = true;
  return opt;
}

}  // namespace file
