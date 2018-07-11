// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
//  This tool is used to show diff of 2 ExecReqs after normalizing those.
//

#include <iostream>

#include "compiler_flag_type_specific.h"
#include "execreq_normalizer.h"
#include "file_helper.h"
#include "gcc_flags.h"
#include "glog/logging.h"
#include "google/protobuf/text_format.h"
#include "google/protobuf/util/message_differencer.h"

void NormalizeExecReq(devtools_goma::ExecReq* req) {
  const std::vector<string> kFlagToNormalize{
      "Xclang", "B", "I", "gcc-toolchain", "-sysroot", "resource-dir"};

  std::vector<string> args(req->arg().begin(), req->arg().end());
  devtools_goma::GCCFlags flags(args, req->cwd());

  auto normalizer = devtools_goma::CompilerFlagTypeSpecific::FromArg(
                        req->command_spec().name())
                        .NewExecReqNormalizer();
  normalizer->NormalizeForCacheKey(0, true, false, kFlagToNormalize,
                                   flags.fdebug_prefix_map(), req);
}

int main(int argc, char* argv[]) {
  if (argc != 3) {
    std::cerr << "Usage: \n"
              << argv[0] << " <text execreq1> <text execreq2>" << std::endl;
    exit(1);
  }

  std::string text_req1, text_req2;
  LOG_IF(FATAL, !devtools_goma::ReadFileToString(argv[1], &text_req1))
      << "failed to read " << argv[1];

  LOG_IF(FATAL, !devtools_goma::ReadFileToString(argv[2], &text_req2))
      << "failed to read " << argv[2];

  devtools_goma::ExecReq req1, req2;
  LOG_IF(FATAL,
         !google::protobuf::TextFormat::ParseFromString(text_req1, &req1))
      << "failed to parse " << text_req1;
  LOG_IF(FATAL,
         !google::protobuf::TextFormat::ParseFromString(text_req2, &req2))
      << "failed to parse " << text_req2;

  NormalizeExecReq(&req1);
  NormalizeExecReq(&req2);

  google::protobuf::util::MessageDifferencer differencer;
  std::string difference_reason;
  differencer.ReportDifferencesToString(&difference_reason);
  if (!differencer.Compare(req1, req2)) {
    std::cout << "diff " << argv[1] << " " << argv[2] << "\n"
              << difference_reason << std::endl;
  }
}
