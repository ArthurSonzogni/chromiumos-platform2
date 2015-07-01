// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/payload_generator/payload_file.h"

#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "update_engine/payload_constants.h"
#include "update_engine/payload_generator/extent_ranges.h"
#include "update_engine/test_utils.h"

using std::string;
using std::vector;

namespace chromeos_update_engine {

class PayloadFileTest : public ::testing::Test {
 protected:
  PayloadFile payload_;
};

TEST_F(PayloadFileTest, ReorderBlobsTest) {
  string orig_blobs;
  EXPECT_TRUE(utils::MakeTempFile("ReorderBlobsTest.orig.XXXXXX", &orig_blobs,
                                  nullptr));
  ScopedPathUnlinker orig_blobs_unlinker(orig_blobs);

  // The operations have three blob and one gap (the whitespace):
  // Rootfs operation 1: [8, 3] bcd
  // Rootfs operation 2: [7, 1] a
  // Kernel operation 1: [0, 6] kernel
  string orig_data = "kernel abcd";
  EXPECT_TRUE(
      utils::WriteFile(orig_blobs.c_str(), orig_data.data(), orig_data.size()));

  string new_blobs;
  EXPECT_TRUE(
      utils::MakeTempFile("ReorderBlobsTest.new.XXXXXX", &new_blobs, nullptr));
  ScopedPathUnlinker new_blobs_unlinker(new_blobs);

  vector<AnnotatedOperation> aops;
  AnnotatedOperation aop;
  aop.op.set_data_offset(8);
  aop.op.set_data_length(3);
  aops.push_back(aop);

  aop.op.set_data_offset(7);
  aop.op.set_data_length(1);
  aops.push_back(aop);
  payload_.AddPartitionOperations(PartitionName::kRootfs, aops);

  aop.op.set_data_offset(0);
  aop.op.set_data_length(6);
  aops = {aop};
  payload_.AddPartitionOperations(PartitionName::kKernel, aops);

  EXPECT_TRUE(payload_.ReorderDataBlobs(orig_blobs, new_blobs));

  const vector<AnnotatedOperation>& rootfs_aops =
      payload_.aops_map_[PartitionName::kRootfs];
  const vector<AnnotatedOperation>& kernel_aops =
      payload_.aops_map_[PartitionName::kKernel];
  string new_data;
  EXPECT_TRUE(utils::ReadFile(new_blobs, &new_data));
  // Kernel blobs should appear at the end.
  EXPECT_EQ("bcdakernel", new_data);

  EXPECT_EQ(2, rootfs_aops.size());
  EXPECT_EQ(0, rootfs_aops[0].op.data_offset());
  EXPECT_EQ(3, rootfs_aops[0].op.data_length());
  EXPECT_EQ(3, rootfs_aops[1].op.data_offset());
  EXPECT_EQ(1, rootfs_aops[1].op.data_length());

  EXPECT_EQ(1, kernel_aops.size());
  EXPECT_EQ(4, kernel_aops[0].op.data_offset());
  EXPECT_EQ(6, kernel_aops[0].op.data_length());
}

}  // namespace chromeos_update_engine
