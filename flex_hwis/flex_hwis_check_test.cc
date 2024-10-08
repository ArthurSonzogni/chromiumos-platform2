// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flex_hwis/flex_hwis_check.h"

#include <optional>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <policy/mock_device_policy.h>
#include <policy/mock_libpolicy.h>

using ::testing::_;
using ::testing::AtMost;
using ::testing::Return;
using ::testing::StrictMock;

namespace flex_hwis {

ACTION_P(SetEnterpriseEnrolled, enrolled) {
  return enrolled;
}

class FlexHwisCheckTest : public ::testing::Test {
 protected:
  void SetUp() override {
    CHECK(test_dir_.CreateUniqueTempDir());
    test_path_ = test_dir_.GetPath();

    // The default setting is for the device to be enrolled and
    // all device policies to be enabled.
    EXPECT_CALL(mock_device_policy_, LoadPolicy(false))
        .Times(AtMost(1))
        .WillOnce(Return(true));
    EXPECT_CALL(mock_policy_provider_, Reload())
        .Times(AtMost(1))
        .WillOnce(Return(true));
    EXPECT_CALL(mock_policy_provider_, GetDevicePolicy())
        .Times(AtMost(1))
        .WillOnce(ReturnRef(mock_device_policy_));
    EXPECT_CALL(mock_policy_provider_, device_policy_is_loaded())
        .Times(AtMost(1))
        .WillOnce(Return(true));
  }

  void CreateTimeStamp(const std::string& timestamp) {
    base::FilePath time_path = test_path_.Append("var/lib/flex_hwis_tool");
    CHECK(base::CreateDirectory(time_path));
    CHECK(base::WriteFile(time_path.Append("time"), timestamp));
  }

  void ExpectEnrolled(bool enrolled) {
    EXPECT_CALL(mock_device_policy_, IsEnterpriseEnrolled())
        .WillOnce(SetEnterpriseEnrolled(enrolled));
  }

  void ExpectUnenrolledHwCollectionResult(std::optional<bool> result) {
    EXPECT_CALL(mock_device_policy_, GetUnenrolledHwDataUsageEnabled())
        .WillOnce(Return(result));
  }

  void ExpectEnrolledHwCollectionResult(std::optional<bool> result) {
    EXPECT_CALL(mock_device_policy_, GetEnrolledHwDataUsageEnabled())
        .WillOnce(Return(result));
  }

  StrictMock<policy::MockPolicyProvider> mock_policy_provider_;
  StrictMock<policy::MockDevicePolicy> mock_device_policy_;
  base::ScopedTempDir test_dir_;
  base::FilePath test_path_;
};

TEST_F(FlexHwisCheckTest, CheckTime) {
  auto flex_hwis_check =
      flex_hwis::FlexHwisCheck(test_path_, mock_policy_provider_);
  flex_hwis_check.RecordSendTime();
  EXPECT_TRUE(flex_hwis_check.HasRunRecently());
}

TEST_F(FlexHwisCheckTest, CheckTimeEmpty) {
  auto flex_hwis_check =
      flex_hwis::FlexHwisCheck(test_path_, mock_policy_provider_);
  EXPECT_FALSE(flex_hwis_check.HasRunRecently());
}

TEST_F(FlexHwisCheckTest, CheckDeviceName) {
  auto flex_hwis_check =
      flex_hwis::FlexHwisCheck(test_path_, mock_policy_provider_);

  // Simulate failure to read device name.
  EXPECT_EQ(std::nullopt, flex_hwis_check.GetDeviceName());

  constexpr char kDeviceName[] = "reven_device_name";
  flex_hwis_check.SetDeviceName(kDeviceName);
  EXPECT_EQ(kDeviceName, flex_hwis_check.GetDeviceName().value());

  flex_hwis_check.DeleteDeviceName();
  EXPECT_EQ(std::nullopt, flex_hwis_check.GetDeviceName());
}

TEST_F(FlexHwisCheckTest, CheckEnrolledWithPermission) {
  auto flex_hwis_check =
      flex_hwis::FlexHwisCheck(test_path_, mock_policy_provider_);

  ExpectEnrolled(true);
  ExpectEnrolledHwCollectionResult(true);

  PermissionInfo info = flex_hwis_check.CheckPermission();
  EXPECT_TRUE(info.enrolled);
  EXPECT_TRUE(info.loaded);
  EXPECT_TRUE(info.permission);
}

TEST_F(FlexHwisCheckTest, CheckEnrolledWithoutPermission) {
  auto flex_hwis_check =
      flex_hwis::FlexHwisCheck(test_path_, mock_policy_provider_);

  ExpectEnrolled(true);
  ExpectEnrolledHwCollectionResult(false);

  PermissionInfo info = flex_hwis_check.CheckPermission();
  EXPECT_TRUE(info.enrolled);
  EXPECT_TRUE(info.loaded);
  EXPECT_FALSE(info.permission);
}

TEST_F(FlexHwisCheckTest, CheckEnrolledFailedRead) {
  auto flex_hwis_check =
      flex_hwis::FlexHwisCheck(test_path_, mock_policy_provider_);

  ExpectEnrolled(true);
  ExpectEnrolledHwCollectionResult(std::nullopt);

  PermissionInfo info = flex_hwis_check.CheckPermission();
  EXPECT_TRUE(info.enrolled);
  EXPECT_TRUE(info.loaded);
  EXPECT_FALSE(info.permission);
}

TEST_F(FlexHwisCheckTest, CheckUnenrolledWithPermission) {
  auto flex_hwis_check =
      flex_hwis::FlexHwisCheck(test_path_, mock_policy_provider_);

  ExpectEnrolled(false);
  ExpectUnenrolledHwCollectionResult(true);

  PermissionInfo info = flex_hwis_check.CheckPermission();
  EXPECT_FALSE(info.enrolled);
  EXPECT_TRUE(info.loaded);
  EXPECT_TRUE(info.permission);
}

TEST_F(FlexHwisCheckTest, CheckUnenrolledWithoutPermission) {
  auto flex_hwis_check =
      flex_hwis::FlexHwisCheck(test_path_, mock_policy_provider_);

  ExpectEnrolled(false);
  ExpectUnenrolledHwCollectionResult(false);

  PermissionInfo info = flex_hwis_check.CheckPermission();
  EXPECT_FALSE(info.enrolled);
  EXPECT_TRUE(info.loaded);
  EXPECT_FALSE(info.permission);
}

TEST_F(FlexHwisCheckTest, CheckUnenrolledFailedRead) {
  auto flex_hwis_check =
      flex_hwis::FlexHwisCheck(test_path_, mock_policy_provider_);

  ExpectEnrolled(false);
  ExpectUnenrolledHwCollectionResult(std::nullopt);

  PermissionInfo info = flex_hwis_check.CheckPermission();
  EXPECT_FALSE(info.enrolled);
  EXPECT_TRUE(info.loaded);
  EXPECT_FALSE(info.permission);
}

}  // namespace flex_hwis
