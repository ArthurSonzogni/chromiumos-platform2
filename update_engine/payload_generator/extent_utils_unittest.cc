// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/payload_generator/extent_utils.h"

#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "update_engine/payload_constants.h"
#include "update_engine/payload_generator/extent_ranges.h"
#include "update_engine/test_utils.h"

using std::vector;

namespace chromeos_update_engine {

class ExtentUtilsTest : public ::testing::Test {};

TEST(ExtentUtilsTest, AppendSparseToExtentsTest) {
  vector<Extent> extents;

  EXPECT_EQ(0, extents.size());
  AppendBlockToExtents(&extents, kSparseHole);
  EXPECT_EQ(1, extents.size());
  AppendBlockToExtents(&extents, 0);
  EXPECT_EQ(2, extents.size());
  AppendBlockToExtents(&extents, kSparseHole);
  AppendBlockToExtents(&extents, kSparseHole);

  ASSERT_EQ(3, extents.size());
  EXPECT_EQ(kSparseHole, extents[0].start_block());
  EXPECT_EQ(1, extents[0].num_blocks());
  EXPECT_EQ(0, extents[1].start_block());
  EXPECT_EQ(1, extents[1].num_blocks());
  EXPECT_EQ(kSparseHole, extents[2].start_block());
  EXPECT_EQ(2, extents[2].num_blocks());
}

TEST(ExtentUtilsTest, BlocksInExtentsTest) {
  {
    vector<Extent> extents;
    EXPECT_EQ(0, BlocksInExtents(extents));
    extents.push_back(ExtentForRange(0, 1));
    EXPECT_EQ(1, BlocksInExtents(extents));
    extents.push_back(ExtentForRange(23, 55));
    EXPECT_EQ(56, BlocksInExtents(extents));
    extents.push_back(ExtentForRange(1, 2));
    EXPECT_EQ(58, BlocksInExtents(extents));
  }
  {
    google::protobuf::RepeatedPtrField<Extent> extents;
    EXPECT_EQ(0, BlocksInExtents(extents));
    *extents.Add() = ExtentForRange(0, 1);
    EXPECT_EQ(1, BlocksInExtents(extents));
    *extents.Add() = ExtentForRange(23, 55);
    EXPECT_EQ(56, BlocksInExtents(extents));
    *extents.Add() = ExtentForRange(1, 2);
    EXPECT_EQ(58, BlocksInExtents(extents));
  }
}

TEST(ExtentUtilsTest, ExtendExtentsTest) {
  InstallOperation first_op;
  *(first_op.add_src_extents()) = ExtentForRange(1, 1);
  *(first_op.add_src_extents()) = ExtentForRange(3, 1);

  InstallOperation second_op;
  *(second_op.add_src_extents()) = ExtentForRange(4, 2);
  *(second_op.add_src_extents()) = ExtentForRange(8, 2);

  ExtendExtents(first_op.mutable_src_extents(), second_op.src_extents());
  vector<Extent> first_op_vec;
  ExtentsToVector(first_op.src_extents(), &first_op_vec);
  EXPECT_EQ((vector<Extent>{
      ExtentForRange(1, 1),
      ExtentForRange(3, 3),
      ExtentForRange(8, 2)}), first_op_vec);
}

TEST(ExtentUtilsTest, NormalizeExtentsSimpleList) {
  // Make sure it works when there's just one extent.
  vector<Extent> extents;
  NormalizeExtents(&extents);
  EXPECT_EQ(0, extents.size());

  extents = { ExtentForRange(0, 3) };
  NormalizeExtents(&extents);
  EXPECT_EQ(1, extents.size());
  EXPECT_EQ(ExtentForRange(0, 3), extents[0]);
}

TEST(ExtentUtilsTest, NormalizeExtentsTest) {
  vector<Extent> extents = {
      ExtentForRange(0, 3),
      ExtentForRange(3, 2),
      ExtentForRange(5, 1),
      ExtentForRange(8, 4),
      ExtentForRange(13, 1),
      ExtentForRange(14, 2)
  };
  NormalizeExtents(&extents);
  EXPECT_EQ(3, extents.size());
  EXPECT_EQ(ExtentForRange(0, 6), extents[0]);
  EXPECT_EQ(ExtentForRange(8, 4), extents[1]);
  EXPECT_EQ(ExtentForRange(13, 3), extents[2]);
}

TEST(ExtentUtilsTest, ExtentsSublistTest) {
  vector<Extent> extents = {
      ExtentForRange(10, 10),
      ExtentForRange(30, 10),
      ExtentForRange(50, 10)
  };

  // Simple empty result cases.
  EXPECT_EQ(vector<Extent>(),
            ExtentsSublist(extents, 1000, 20));
  EXPECT_EQ(vector<Extent>(),
            ExtentsSublist(extents, 5, 0));
  EXPECT_EQ(vector<Extent>(),
            ExtentsSublist(extents, 30, 1));

  // Normal test cases.
  EXPECT_EQ(vector<Extent>{ ExtentForRange(13, 2) },
            ExtentsSublist(extents, 3, 2));
  EXPECT_EQ(vector<Extent>{ ExtentForRange(15, 5) },
            ExtentsSublist(extents, 5, 5));
  EXPECT_EQ((vector<Extent>{ ExtentForRange(15, 5), ExtentForRange(30, 5) }),
            ExtentsSublist(extents, 5, 10));
  EXPECT_EQ((vector<Extent>{
                 ExtentForRange(13, 7),
                 ExtentForRange(30, 10),
                 ExtentForRange(50, 3), }),
            ExtentsSublist(extents, 3, 20));

  // Extact match case.
  EXPECT_EQ(vector<Extent>{ ExtentForRange(30, 10) },
            ExtentsSublist(extents, 10, 10));
  EXPECT_EQ(vector<Extent>{ ExtentForRange(50, 10) },
            ExtentsSublist(extents, 20, 10));

  // Cases where the requested num_blocks is too big.
  EXPECT_EQ(vector<Extent>{ ExtentForRange(53, 7) },
            ExtentsSublist(extents, 23, 100));
  EXPECT_EQ((vector<Extent>{ ExtentForRange(34, 6), ExtentForRange(50, 10) }),
            ExtentsSublist(extents, 14, 100));
}

}  // namespace chromeos_update_engine
