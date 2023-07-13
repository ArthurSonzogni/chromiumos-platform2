// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/segmentation/segmentation_utils_impl.h"

#include <memory>
#include <utility>

#include <gtest/gtest.h>
#include <libsegmentation/feature_management_fake.h>

namespace rmad {

class SegmentationUtilsTest : public testing::Test {
 public:
  SegmentationUtilsTest() = default;
  ~SegmentationUtilsTest() override = default;

  std::unique_ptr<SegmentationUtils> CreateSegmentationUtils() const {
    auto fake_feature_management_interface =
        std::make_unique<segmentation::fake::FeatureManagementFake>();
    fake_feature_management_interface->SetFeatureLevel(
        segmentation::FeatureManagementInterface::FEATURE_LEVEL_1);

    return std::make_unique<SegmentationUtilsImpl>(
        std::move(fake_feature_management_interface));
  }
};

TEST_F(SegmentationUtilsTest, IsFeatureEnabled) {
  auto segmentation_utils = CreateSegmentationUtils();
  EXPECT_FALSE(segmentation_utils->IsFeatureEnabled());
}

TEST_F(SegmentationUtilsTest, IsFeatureMutable) {
  auto segmentation_utils = CreateSegmentationUtils();
  EXPECT_TRUE(segmentation_utils->IsFeatureMutable());
}

TEST_F(SegmentationUtilsTest, GetFeatureLevel) {
  auto segmentation_utils = CreateSegmentationUtils();
  EXPECT_EQ(1, segmentation_utils->GetFeatureLevel());
}

}  // namespace rmad
