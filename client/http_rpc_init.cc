// Copyright 2016 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "http_rpc_init.h"

#define GOMA_DECLARE_FLAGS_ONLY
#include "goma_flags.cc"

namespace devtools_goma {

void InitHttpRPCOptions(HttpRPC::Options* options) {
  options->compression_level = FLAGS_HTTP_RPC_COMPRESSION_LEVEL;
  options->start_compression = FLAGS_HTTP_RPC_START_COMPRESSION;
  options->accept_encoding = FLAGS_HTTP_ACCEPT_ENCODING;
  options->content_type_for_protobuf =
      FLAGS_CONTENT_TYPE_FOR_PROTOBUF;
}

}  // namespace devtools_goma
