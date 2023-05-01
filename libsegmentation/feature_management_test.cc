// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "libsegmentation/feature_management.h"
#include "libsegmentation/feature_management_fake.h"

namespace segmentation {

using ::testing::Return;

class FeatureManagementTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto fake = std::make_unique<fake::FeatureManagementFake>();
    fake_ = fake.get();
    feature_management_ = std::make_unique<FeatureManagement>(std::move(fake));
  }

  std::unique_ptr<FeatureManagement> feature_management_;
  fake::FeatureManagementFake* fake_;
};

TEST_F(FeatureManagementTest, GetFeature) {
  fake_->SetFeature("my_feature");
  EXPECT_EQ(feature_management_->IsFeatureEnabled("my_feature"), true);
}

TEST_F(FeatureManagementTest, GetFeatureDoesNotExist) {
  EXPECT_EQ(feature_management_->IsFeatureEnabled("fake"), false);
}

TEST_F(FeatureManagementTest, GetFeatureLevel) {
  EXPECT_EQ(feature_management_->GetFeatureLevel(), 0);
  fake_->SetFeatureLevel(
      FeatureManagementInterface::FeatureLevel::FEATURE_LEVEL_1);
  EXPECT_EQ(feature_management_->GetFeatureLevel(), 1);
}

}  // namespace segmentation
