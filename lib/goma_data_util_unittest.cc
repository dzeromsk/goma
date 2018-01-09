// Copyright 2014 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.



#include "goma_data_util.h"

#include "prototmp/goma_data.pb.h"
#include <gtest/gtest.h>

namespace devtools_goma {

TEST(GomaProtoUtilTest, IsSameSubprogramShouldBeTrueOnEmptyProto) {
  ExecReq req;
  ExecResp resp;

  EXPECT_TRUE(IsSameSubprograms(req, resp));
}

TEST(GomaProtoUtilTest, IsSameSubprogramShouldIgnorePath) {
  ExecReq req;
  ExecResp resp;

  SubprogramSpec dummy_spec;
  dummy_spec.set_binary_hash("dummy_hash");

  SubprogramSpec* spec;
  spec = req.add_subprogram();
  *spec = dummy_spec;
  spec->set_path("request/path");

  spec = resp.mutable_result()->add_subprogram();
  *spec = dummy_spec;
  spec->set_path("response/path");

  EXPECT_TRUE(IsSameSubprograms(req, resp));
}

TEST(GomaProtoUtilTest, IsSameSubprogramShouldBeTrueIfSameEntries) {
  ExecReq req;
  ExecResp resp;

  SubprogramSpec dummy_spec;
  dummy_spec.set_path("dummy_path");
  dummy_spec.set_binary_hash("dummy_hash");

  SubprogramSpec dummy_spec2;
  dummy_spec.set_path("dummy_path2");
  dummy_spec.set_binary_hash("dummy_hash2");

  SubprogramSpec* spec;
  spec = req.add_subprogram();
  *spec = dummy_spec;
  spec = req.add_subprogram();
  *spec = dummy_spec2;

  spec = resp.mutable_result()->add_subprogram();
  *spec = dummy_spec;
  spec = resp.mutable_result()->add_subprogram();
  *spec = dummy_spec2;

  EXPECT_TRUE(IsSameSubprograms(req, resp));
}

TEST(GomaProtoUtilTest, IsSameSubprogramShouldBeTrueEvenIfOderIsDifferent) {
  ExecReq req;
  ExecResp resp;

  SubprogramSpec dummy_spec;
  dummy_spec.set_path("dummy_path");
  dummy_spec.set_binary_hash("dummy_hash");

  SubprogramSpec dummy_spec2;
  dummy_spec.set_path("dummy_path2");
  dummy_spec.set_binary_hash("dummy_hash2");

  SubprogramSpec* spec;
  spec = req.add_subprogram();
  *spec = dummy_spec;
  spec = req.add_subprogram();
  *spec = dummy_spec2;

  spec = resp.mutable_result()->add_subprogram();
  *spec = dummy_spec2;
  spec = resp.mutable_result()->add_subprogram();
  *spec = dummy_spec;

  EXPECT_TRUE(IsSameSubprograms(req, resp));
}

TEST(GomaProtoUtilTest, IsSameSubprogramShouldBeFalseOnSizeMismatch) {
  ExecReq req;
  ExecResp resp;

  SubprogramSpec* spec;
  spec = req.add_subprogram();
  spec->set_path("dummy_path");
  spec->set_binary_hash("dummy_hash");

  EXPECT_FALSE(IsSameSubprograms(req, resp));
}

TEST(GomaProtoUtilTest, IsSameSubprogramShouldBeFalseOnContentsMismatch) {
  ExecReq req;
  ExecResp resp;

  SubprogramSpec dummy_spec;
  dummy_spec.set_path("dummy_path");

  SubprogramSpec* spec;
  spec = req.add_subprogram();
  *spec = dummy_spec;
  spec->set_binary_hash("dummy_hash");

  spec = resp.mutable_result()->add_subprogram();
  *spec = dummy_spec;
  spec->set_binary_hash("different_hash");

  EXPECT_FALSE(IsSameSubprograms(req, resp));
}

}  // namespace devtools_goma
