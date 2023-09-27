// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flex_hwis/flex_hwis.h"
#include "flex_hwis/mock_http_sender.h"
#include "flex_hwis/telemetry_for_testing.h"

#include <string>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/strings/string_number_conversions.h>
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

constexpr char kPutMetricName[] = "Platform.FlexHwis.ServerPutSuccess";
constexpr char kPostMetricName[] = "Platform.FlexHwis.ServerPostSuccess";
constexpr char kPermissionMetricName[] =
    "Platform.FlexHwis.PermissionCheckResult";
constexpr char kHwisFilePath[] = "var/lib/flex_hwis_tool";
constexpr char kUuidForTesting[] = "uuid_for_testing";

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
    telemetry_.AddTelemetryInfo();
  }

  void CreateTimeStamp(const std::string& timestamp) {
    base::FilePath time_path = test_path_.Append(kHwisFilePath);
    CHECK(base::CreateDirectory(time_path));
    CHECK(base::WriteFile(time_path.Append("time"), timestamp));
  }

  void CreateUuid() {
    base::FilePath uuid_path = test_path_.Append(kHwisFilePath);
    CHECK(base::CreateDirectory(uuid_path));
    CHECK(base::WriteFile(uuid_path.Append("uuid"), kUuidForTesting));
  }

  void ExpectPermissionMetric(PermissionResult result) {
    EXPECT_CALL(library_, SendEnumToUMA(kPermissionMetricName,
                                        static_cast<int>(result), testing::_))
        .WillOnce(testing::Return(true));
  }

  void ExpectApiMetric(const std::string& metric_name, bool result) {
    EXPECT_CALL(library_, SendBoolToUMA(metric_name, result))
        .WillOnce(testing::Return(true));
  }

  void ExpectDeleteAction() {
    CreateUuid();
    EXPECT_CALL(mock_http_sender_, DeleteDevice(_)).WillOnce(Return(true));
  }

  void ExpectUpdateAction(bool api_call_success) {
    CreateUuid();
    EXPECT_CALL(mock_http_sender_, UpdateDevice(_))
        .WillOnce(Return(api_call_success));
    ExpectApiMetric(kPutMetricName, api_call_success);
  }

  void ExpectRegisterAction(bool api_call_success) {
    hwis_proto::Device hardware_info;
    hardware_info.set_uuid(kUuidForTesting);
    PostActionResponse response(api_call_success,
                                hardware_info.SerializeAsString());
    EXPECT_CALL(mock_http_sender_, RegisterNewDevice(_))
        .WillOnce(Return(response));
    ExpectApiMetric(kPostMetricName, api_call_success);
  }

  StrictMock<policy::MockPolicyProvider> mock_policy_provider_;
  StrictMock<policy::MockDevicePolicy> mock_device_policy_;
  StrictMock<MockHttpSender> mock_http_sender_;
  MetricsLibraryMock library_;
  base::ScopedTempDir test_dir_;
  base::FilePath test_path_;
  flex_hwis::TelemetryForTesting telemetry_;
};

TEST_F(FlexHwisTest, HasRunRecently) {
  auto flex_hwis_sender_ = flex_hwis::FlexHwisSender(
      test_path_, mock_policy_provider_, mock_http_sender_);
  flex_hwis_sender_.SetTelemetryInfoForTesting(telemetry_.Get());
  const auto current_from_epoch =
      (base::Time::Now() - base::Time::UnixEpoch()).InSeconds();
  CreateTimeStamp(base::NumberToString(current_from_epoch));

  EXPECT_EQ(flex_hwis_sender_.CollectAndSend(library_, Debug::None),
            Result::HasRunRecently);
}

TEST_F(FlexHwisTest, ManagedWithoutPermission) {
  auto flex_hwis_sender_ = flex_hwis::FlexHwisSender(
      test_path_, mock_policy_provider_, mock_http_sender_);
  flex_hwis_sender_.SetTelemetryInfoForTesting(telemetry_.Get());
  EXPECT_CALL(mock_device_policy_, GetReportSystemInfo(_))
      .WillOnce(SetEnabled(false));
  ExpectPermissionMetric(PermissionResult::kPolicyDenial);
  ExpectDeleteAction();

  EXPECT_EQ(flex_hwis_sender_.CollectAndSend(library_, Debug::None),
            Result::NotAuthorized);
}

TEST_F(FlexHwisTest, UnManagedWithoutPermission) {
  auto flex_hwis_sender_ = flex_hwis::FlexHwisSender(
      test_path_, mock_policy_provider_, mock_http_sender_);
  flex_hwis_sender_.SetTelemetryInfoForTesting(telemetry_.Get());
  EXPECT_CALL(mock_device_policy_, IsEnterpriseEnrolled())
      .WillOnce(SetEnterpriseEnrolled(false));
  EXPECT_CALL(mock_device_policy_, GetHwDataUsageEnabled(_))
      .WillOnce(SetEnabled(false));
  ExpectPermissionMetric(PermissionResult::kOptInDenial);
  ExpectDeleteAction();

  EXPECT_EQ(flex_hwis_sender_.CollectAndSend(library_, Debug::None),
            Result::NotAuthorized);
}

TEST_F(FlexHwisTest, ManagedWithPermission) {
  auto flex_hwis_sender_ = flex_hwis::FlexHwisSender(
      test_path_, mock_policy_provider_, mock_http_sender_);
  flex_hwis_sender_.SetTelemetryInfoForTesting(telemetry_.Get());
  ExpectPermissionMetric(PermissionResult::kPolicySuccess);
  ExpectRegisterAction(/*api_call_success=*/true);
  EXPECT_EQ(flex_hwis_sender_.CollectAndSend(library_, Debug::None),
            Result::Sent);
}

TEST_F(FlexHwisTest, UnManagedWithPermission) {
  auto flex_hwis_sender_ = flex_hwis::FlexHwisSender(
      test_path_, mock_policy_provider_, mock_http_sender_);
  flex_hwis_sender_.SetTelemetryInfoForTesting(telemetry_.Get());
  EXPECT_CALL(mock_device_policy_, IsEnterpriseEnrolled())
      .WillOnce(SetEnterpriseEnrolled(false));
  ExpectPermissionMetric(PermissionResult::kOptInSuccess);
  ExpectRegisterAction(/*api_call_success=*/true);
  EXPECT_EQ(flex_hwis_sender_.CollectAndSend(library_, Debug::None),
            Result::Sent);
}

TEST_F(FlexHwisTest, PutHwDataFail) {
  auto flex_hwis_sender_ = flex_hwis::FlexHwisSender(
      test_path_, mock_policy_provider_, mock_http_sender_);
  flex_hwis_sender_.SetTelemetryInfoForTesting(telemetry_.Get());
  ExpectPermissionMetric(PermissionResult::kPolicySuccess);
  ExpectUpdateAction(/*api_call_success=*/false);
  EXPECT_EQ(flex_hwis_sender_.CollectAndSend(library_, Debug::None),
            Result::Error);
}

}  // namespace flex_hwis
