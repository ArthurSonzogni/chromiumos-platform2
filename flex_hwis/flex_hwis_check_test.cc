// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flex_hwis/flex_hwis_check.h"

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <policy/mock_device_policy.h>

using ::testing::_;
using ::testing::AtMost;
using ::testing::Return;

namespace flex_hwis {

ACTION_P(SetEnabled, enabled) {
  *arg0 = enabled;
  return true;
}

ACTION_P(SetEnterpriseEnrolled, managed) {
  return managed;
}

class FlexHwisCheckTest : public ::testing::Test {
 protected:
  void SetUp() override {
    CHECK(test_dir_.CreateUniqueTempDir());
    test_path_ = test_dir_.GetPath();

    // The default setting is for the device to be managed and
    // all device policies to be enabled.
    device_policy_ = new policy::MockDevicePolicy();
    EXPECT_CALL(*device_policy_, LoadPolicy(false))
        .Times(AtMost(1))
        .WillOnce(Return(true));
    EXPECT_CALL(*device_policy_, GetHwDataUsageEnabled(_))
        .Times(AtMost(1))
        .WillOnce(SetEnabled(true));
    EXPECT_CALL(*device_policy_, GetReportSystemInfo(_))
        .Times(AtMost(1))
        .WillOnce(SetEnabled(true));
    EXPECT_CALL(*device_policy_, GetReportCpuInfo(_))
        .Times(AtMost(1))
        .WillOnce(SetEnabled(true));
    EXPECT_CALL(*device_policy_, GetReportGraphicsStatus(_))
        .Times(AtMost(1))
        .WillOnce(SetEnabled(true));
    EXPECT_CALL(*device_policy_, GetReportMemoryInfo(_))
        .Times(AtMost(1))
        .WillOnce(SetEnabled(true));
    EXPECT_CALL(*device_policy_, GetReportVersionInfo(_))
        .Times(AtMost(1))
        .WillOnce(SetEnabled(true));
    EXPECT_CALL(*device_policy_, GetReportNetworkConfig(_))
        .Times(AtMost(1))
        .WillOnce(SetEnabled(true));
    EXPECT_CALL(*device_policy_, IsEnterpriseEnrolled())
        .Times(AtMost(1))
        .WillOnce(SetEnterpriseEnrolled(true));
    flex_hwis_check_ = flex_hwis::FlexHwisCheck(
        test_path_,
        std::make_unique<policy::PolicyProvider>(
            std::unique_ptr<policy::MockDevicePolicy>(device_policy_)));
  }

  void CreateTimeStamp(const std::string& timestamp) {
    base::FilePath time_path = test_path_.Append("var/lib/flex_hwis_tool");
    CHECK(base::CreateDirectory(time_path));
    CHECK(base::WriteFile(time_path.Append("time"), timestamp));
  }

  void CreateUuid(const std::string& uuid) {
    base::FilePath uuid_path = test_path_.Append("proc/sys/kernel/random");
    CHECK(base::CreateDirectory(uuid_path));
    CHECK(base::WriteFile(uuid_path.Append("uuid"), uuid));
  }

  void EmptyHwisUuid() {
    base::FilePath uuid_path = test_path_.Append("var/lib/flex_hwis_tool");
    CHECK(base::WriteFile(uuid_path.Append("uuid"), ""));
  }

  std::optional<flex_hwis::FlexHwisCheck> flex_hwis_check_;
  policy::MockDevicePolicy* device_policy_;
  base::ScopedTempDir test_dir_;
  base::FilePath test_path_;
};

TEST_F(FlexHwisCheckTest, CheckTime) {
  flex_hwis_check_->RecordSendTime();
  EXPECT_TRUE(flex_hwis_check_->HasRunRecently());
}

TEST_F(FlexHwisCheckTest, CheckTimeEmpty) {
  EXPECT_FALSE(flex_hwis_check_->HasRunRecently());
}

TEST_F(FlexHwisCheckTest, CheckUuid) {
  constexpr char kUuid[] = "reven-uuid";
  CreateUuid(kUuid);
  UuidInfo info = flex_hwis_check_->GetOrCreateUuid();
  EXPECT_FALSE(info.already_exists);
  EXPECT_EQ(kUuid, info.uuid);
  info = flex_hwis_check_->GetOrCreateUuid();
  EXPECT_TRUE(info.already_exists);

  EmptyHwisUuid();
  info = flex_hwis_check_->GetOrCreateUuid();
  EXPECT_FALSE(info.already_exists);
  EXPECT_EQ(kUuid, info.uuid);
}

TEST_F(FlexHwisCheckTest, CheckManagedPermission) {
  EXPECT_TRUE(flex_hwis_check_->CheckPermission());
}

TEST_F(FlexHwisCheckTest, CheckUnManagedPermission) {
  EXPECT_CALL(*device_policy_, IsEnterpriseEnrolled())
      .WillOnce(SetEnterpriseEnrolled(false));
  EXPECT_TRUE(flex_hwis_check_->CheckPermission());
}

TEST_F(FlexHwisCheckTest, CheckDisableSystemInfo) {
  EXPECT_CALL(*device_policy_, GetReportSystemInfo(_))
      .WillOnce(SetEnabled(false));
  EXPECT_FALSE(flex_hwis_check_->CheckPermission());
}

TEST_F(FlexHwisCheckTest, CheckDisableCpuInfo) {
  EXPECT_CALL(*device_policy_, GetReportCpuInfo(_)).WillOnce(SetEnabled(false));
  EXPECT_FALSE(flex_hwis_check_->CheckPermission());
}

TEST_F(FlexHwisCheckTest, CheckDisableVersionInfo) {
  EXPECT_CALL(*device_policy_, GetReportVersionInfo(_))
      .WillOnce(SetEnabled(false));
  EXPECT_FALSE(flex_hwis_check_->CheckPermission());
}

TEST_F(FlexHwisCheckTest, CheckDisableGraphicsStatus) {
  EXPECT_CALL(*device_policy_, GetReportGraphicsStatus(_))
      .WillOnce(SetEnabled(false));
  EXPECT_FALSE(flex_hwis_check_->CheckPermission());
}

TEST_F(FlexHwisCheckTest, CheckDisableMemoryInfo) {
  EXPECT_CALL(*device_policy_, GetReportMemoryInfo(_))
      .WillOnce(SetEnabled(false));
  EXPECT_FALSE(flex_hwis_check_->CheckPermission());
}

TEST_F(FlexHwisCheckTest, CheckDisableNetworkConfig) {
  EXPECT_CALL(*device_policy_, GetReportNetworkConfig(_))
      .WillOnce(SetEnabled(false));
  EXPECT_FALSE(flex_hwis_check_->CheckPermission());
}

TEST_F(FlexHwisCheckTest, CheckDisableHwDataUsage) {
  EXPECT_CALL(*device_policy_, IsEnterpriseEnrolled())
      .WillOnce(SetEnterpriseEnrolled(false));
  EXPECT_CALL(*device_policy_, GetHwDataUsageEnabled(_))
      .WillOnce(SetEnabled(false));
  EXPECT_FALSE(flex_hwis_check_->CheckPermission());
}

}  // namespace flex_hwis
