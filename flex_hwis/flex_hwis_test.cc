// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flex_hwis/flex_hwis.h"
#include "flex_hwis/mock_mojo.h"

#include <string>
#include <utility>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <metrics/metrics_library_mock.h>
#include <policy/mock_device_policy.h>
#include <policy/mock_libpolicy.h>

using ::testing::_;
using ::testing::AtMost;
using ::testing::Return;
using ::testing::StrictMock;

namespace flex_hwis {

ACTION_P(SetEnabled, enabled) {
  *arg0 = enabled;
  return true;
}

ACTION_P(SetEnterpriseEnrolled, managed) {
  return managed;
}

class FlexHwisTest : public ::testing::Test {
 protected:
  void SetUp() override {
    constexpr char kUuid[] = "reven-uuid";

    CHECK(test_dir_.CreateUniqueTempDir());
    test_path_ = test_dir_.GetPath();

    // The default setting is for the device to be managed and
    // all device policies to be enabled.
    EXPECT_CALL(mock_device_policy_, LoadPolicy(false))
        .Times(AtMost(1))
        .WillOnce(Return(true));
    EXPECT_CALL(mock_device_policy_, GetHwDataUsageEnabled(_))
        .Times(AtMost(1))
        .WillOnce(SetEnabled(true));
    EXPECT_CALL(mock_device_policy_, GetReportSystemInfo(_))
        .Times(AtMost(1))
        .WillOnce(SetEnabled(true));
    EXPECT_CALL(mock_device_policy_, GetReportCpuInfo(_))
        .Times(AtMost(1))
        .WillOnce(SetEnabled(true));
    EXPECT_CALL(mock_device_policy_, GetReportGraphicsStatus(_))
        .Times(AtMost(1))
        .WillOnce(SetEnabled(true));
    EXPECT_CALL(mock_device_policy_, GetReportMemoryInfo(_))
        .Times(AtMost(1))
        .WillOnce(SetEnabled(true));
    EXPECT_CALL(mock_device_policy_, GetReportVersionInfo(_))
        .Times(AtMost(1))
        .WillOnce(SetEnabled(true));
    EXPECT_CALL(mock_device_policy_, GetReportNetworkConfig(_))
        .Times(AtMost(1))
        .WillOnce(SetEnabled(true));
    EXPECT_CALL(mock_device_policy_, IsEnterpriseEnrolled())
        .Times(AtMost(1))
        .WillOnce(SetEnterpriseEnrolled(true));
    EXPECT_CALL(mock_policy_provider_, Reload())
        .Times(AtMost(1))
        .WillOnce(Return(true));
    EXPECT_CALL(mock_policy_provider_, GetDevicePolicy())
        .Times(AtMost(1))
        .WillOnce(ReturnRef(mock_device_policy_));
    EXPECT_CALL(mock_policy_provider_, device_policy_is_loaded())
        .Times(AtMost(1))
        .WillOnce(Return(true));

    CreateUuid(kUuid);
    info_ = mock_mojo_.MockTelemetryInfo();
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

  void ExpectEnumMetric(PermissionResult result) {
    EXPECT_CALL(library_,
                SendEnumToUMA("Platform.FlexHwis.PermissionCheckResult",
                              static_cast<int>(result), testing::_))
        .WillOnce(testing::Return(true));
  }

  void ExpectBooleanMetric(bool result) {
    EXPECT_CALL(library_,
                SendBoolToUMA("Platform.FlexHwis.ServerPostResult", result))
        .WillOnce(testing::Return(true));
  }

  StrictMock<policy::MockPolicyProvider> mock_policy_provider_;
  StrictMock<policy::MockDevicePolicy> mock_device_policy_;
  mojom::TelemetryInfoPtr info_;
  MetricsLibraryMock library_;
  base::ScopedTempDir test_dir_;
  base::FilePath test_path_;
  flex_hwis::MockMojo mock_mojo_;
};

TEST_F(FlexHwisTest, HasRunRecently) {
  auto flex_hwis_sender_ =
      flex_hwis::FlexHwisSender(test_path_, mock_policy_provider_);
  flex_hwis_sender_.SetTelemetryInfoForTesting(std::move(info_));
  CreateTimeStamp(base::TimeFormatHTTP(base::Time::Now()));

  EXPECT_EQ(flex_hwis_sender_.CollectAndSend(library_, Debug::None),
            Result::HasRunRecently);
}

TEST_F(FlexHwisTest, ManagedWithoutPermission) {
  auto flex_hwis_sender_ =
      flex_hwis::FlexHwisSender(test_path_, mock_policy_provider_);
  flex_hwis_sender_.SetTelemetryInfoForTesting(std::move(info_));
  EXPECT_CALL(mock_device_policy_, GetReportSystemInfo(_))
      .WillOnce(SetEnabled(false));
  ExpectEnumMetric(PermissionResult::kPolicyDenial);

  EXPECT_EQ(flex_hwis_sender_.CollectAndSend(library_, Debug::None),
            Result::NotAuthorized);
}

TEST_F(FlexHwisTest, UnManagedWithoutPermission) {
  auto flex_hwis_sender_ =
      flex_hwis::FlexHwisSender(test_path_, mock_policy_provider_);
  flex_hwis_sender_.SetTelemetryInfoForTesting(std::move(info_));
  EXPECT_CALL(mock_device_policy_, IsEnterpriseEnrolled())
      .WillOnce(SetEnterpriseEnrolled(false));
  EXPECT_CALL(mock_device_policy_, GetHwDataUsageEnabled(_))
      .WillOnce(SetEnabled(false));
  ExpectEnumMetric(PermissionResult::kOptInDenial);

  EXPECT_EQ(flex_hwis_sender_.CollectAndSend(library_, Debug::None),
            Result::NotAuthorized);
}

TEST_F(FlexHwisTest, ManagedWithPermission) {
  auto flex_hwis_sender_ =
      flex_hwis::FlexHwisSender(test_path_, mock_policy_provider_);
  flex_hwis_sender_.SetTelemetryInfoForTesting(std::move(info_));
  ExpectEnumMetric(PermissionResult::kPolicySuccess);
  ExpectBooleanMetric(true);

  EXPECT_EQ(flex_hwis_sender_.CollectAndSend(library_, Debug::None),
            Result::Sent);
}

TEST_F(FlexHwisTest, UnManagedWithPermission) {
  auto flex_hwis_sender_ =
      flex_hwis::FlexHwisSender(test_path_, mock_policy_provider_);
  flex_hwis_sender_.SetTelemetryInfoForTesting(std::move(info_));
  EXPECT_CALL(mock_device_policy_, IsEnterpriseEnrolled())
      .WillOnce(SetEnterpriseEnrolled(false));
  ExpectEnumMetric(PermissionResult::kOptInSuccess);
  ExpectBooleanMetric(true);

  EXPECT_EQ(flex_hwis_sender_.CollectAndSend(library_, Debug::None),
            Result::Sent);
}

}  // namespace flex_hwis
