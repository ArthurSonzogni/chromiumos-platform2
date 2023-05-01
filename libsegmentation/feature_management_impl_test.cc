// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "libsegmentation/device_info.pb.h"
#include <libsegmentation/feature_management.h>
#include "libsegmentation/feature_management_impl.h"
#include "libsegmentation/feature_management_interface.h"
#include "libsegmentation/feature_management_util.h"

namespace segmentation {

using ::testing::Return;

// Test fixture for testing feature management.
class FeatureManagementImplTest : public ::testing::Test {
 public:
  FeatureManagementImplTest() = default;
  ~FeatureManagementImplTest() override = default;

  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    device_info_path_ = temp_dir_.GetPath().Append("device_info");
    auto fake = std::make_unique<FeatureManagementImpl>(device_info_path_);
    feature_management_ = std::make_unique<FeatureManagement>(std::move(fake));
  }

 protected:
  // Directory and file path used for simulating device info data.
  base::ScopedTempDir temp_dir_;

  // File path where device info data will be simulated.
  base::FilePath device_info_path_;

  // Object to test.
  std::unique_ptr<FeatureManagement> feature_management_;
};

#if USE_FEATURE_MANAGEMENT
// Use database produced by chromeos-base/feature-management-data
TEST_F(FeatureManagementImplTest, GetAndCacheStatefulFeatureLevelTest) {
  libsegmentation::DeviceInfo device_info;
  device_info.set_feature_level(libsegmentation::DeviceInfo_FeatureLevel::
                                    DeviceInfo_FeatureLevel_FEATURE_LEVEL_1);
  EXPECT_TRUE(FeatureManagementUtil::WriteDeviceInfoToFile(device_info,
                                                           device_info_path_));
  EXPECT_EQ(
      feature_management_->GetFeatureLevel(),
      FeatureManagementInterface::FeatureLevel::FEATURE_LEVEL_1 -
          FeatureManagementInterface::FeatureLevel::FEATURE_LEVEL_VALID_OFFSET);

  // Even though the file is changed we should still get the cached value stored
  // from the previous attempt.
  device_info.set_feature_level(libsegmentation::DeviceInfo_FeatureLevel::
                                    DeviceInfo_FeatureLevel_FEATURE_LEVEL_0);
  EXPECT_TRUE(FeatureManagementUtil::WriteDeviceInfoToFile(device_info,
                                                           device_info_path_));
  EXPECT_EQ(
      feature_management_->GetFeatureLevel(),
      FeatureManagementInterface::FeatureLevel::FEATURE_LEVEL_1 -
          FeatureManagementInterface::FeatureLevel::FEATURE_LEVEL_VALID_OFFSET);
}
#endif

}  // namespace segmentation
