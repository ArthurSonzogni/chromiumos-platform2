// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <memory>

#include "regmon/features/regmon_features_impl.h"

#include <dbus/mock_bus.h>
#include <base/memory/scoped_refptr.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <featured/fake_platform_features.h>
#include <featured/feature_library.h>

namespace regmon::features {

class RegmonFeaturesImplTest : public ::testing::Test {
 public:
  void SetUp() override {
    scoped_refptr<::testing::NiceMock<dbus::MockBus>> mock_bus =
        base::MakeRefCounted<::testing::NiceMock<dbus::MockBus>>(
            dbus::Bus::Options());
    fake_feature_lib_ =
        std::make_unique<feature::FakePlatformFeatures>(mock_bus);
    regmon_features_ =
        std::make_unique<RegmonFeaturesImpl>(fake_feature_lib_.get());
  }

 protected:
  std::unique_ptr<feature::FakePlatformFeatures> fake_feature_lib_;
  std::unique_ptr<RegmonFeaturesImpl> regmon_features_;
};

TEST_F(RegmonFeaturesImplTest, PolicyMonitoringEnabled) {
  fake_feature_lib_->SetEnabled(
      RegmonFeaturesImpl::kRegmonPolicyMonitoringEnabled.name, true);
  EXPECT_TRUE(regmon_features_->PolicyMonitoringEnabled());
}

TEST_F(RegmonFeaturesImplTest, PolicyMonitoringDisabled) {
  fake_feature_lib_->SetEnabled(
      RegmonFeaturesImpl::kRegmonPolicyMonitoringEnabled.name, false);
  EXPECT_FALSE(regmon_features_->PolicyMonitoringEnabled());
}

}  // namespace regmon::features
