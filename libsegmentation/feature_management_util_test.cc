// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "libsegmentation/feature_management_util.h"

namespace segmentation {

using ::testing::Return;

// Test fixture for testing feature management.
class FeatureManagementUtilTest : public ::testing::Test {
 public:
  FeatureManagementUtilTest() = default;
  ~FeatureManagementUtilTest() override = default;
};

TEST_F(FeatureManagementUtilTest, DecodeHwidFail) {
  // Test to check we are finding badly formatted HWID strings.
  EXPECT_FALSE(FeatureManagementUtil::DecodeHWID(""));
  EXPECT_FALSE(FeatureManagementUtil::DecodeHWID("ZZZZ"));
  EXPECT_FALSE(FeatureManagementUtil::DecodeHWID("REDRIX-ZZCR D3A-39-27K-E6B"));
  EXPECT_TRUE(FeatureManagementUtil::DecodeHWID("REDRIX-ZZCR D3A-39F-27K-E6B"));
}

TEST_F(FeatureManagementUtilTest, DecodeHwidValid) {
  EXPECT_EQ(FeatureManagementUtil::DecodeHWID("ZEROONE A2A-797").value(),
            "00000000000001111111111111");
  EXPECT_EQ(
      FeatureManagementUtil::DecodeHWID("REDRIX-ZZCR D3A-39F-27K-E6B").value(),
      "0001100100000110111110010111010101010100010010000001");
}

}  // namespace segmentation
