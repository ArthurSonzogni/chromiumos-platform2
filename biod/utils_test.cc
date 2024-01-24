// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "biod/utils.h"

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "biod/mock_cros_fp_device.h"

namespace biod {

using testing::Return;

TEST(UtilsTest, LogSafeID_Normal) {
  EXPECT_EQ(LogSafeID("0123456789_ABCDEF_0123456789"), "01*");
}

TEST(UtilsTest, LogSafeID_Small) {
  EXPECT_EQ(LogSafeID("K"), "K");
}

TEST(UtilsTest, LogSafeID_BlankString) {
  EXPECT_EQ(LogSafeID(""), "");
}

TEST(UtilsTest, TestGetDirtyList_Empty) {
  MockCrosFpDevice mock_cros_dev;
  EXPECT_CALL(mock_cros_dev, GetDirtyMap).WillOnce(Return(std::bitset<32>()));
  EXPECT_EQ(GetDirtyList(&mock_cros_dev), std::vector<int>());
}

TEST(UtilsTest, TestGetDirtyList) {
  MockCrosFpDevice mock_cros_dev;
  EXPECT_CALL(mock_cros_dev, GetDirtyMap)
      .WillOnce(Return(std::bitset<32>("1001")));
  EXPECT_EQ(GetDirtyList(&mock_cros_dev), (std::vector<int>{0, 3}));
}

}  // namespace biod
