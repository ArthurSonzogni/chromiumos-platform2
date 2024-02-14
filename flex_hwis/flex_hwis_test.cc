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
constexpr char kDeleteMetricName[] = "Platform.FlexHwis.ServerDeleteSuccess";
constexpr char kPermissionMetricName[] =
    "Platform.FlexHwis.PermissionCheckResult";
constexpr char kHwisFilePath[] = "var/lib/flex_hwis_tool";
constexpr char kDeviceNameForTesting[] = "name_for_testing";

ACTION_P(SetEnabled, enabled) {
  *arg0 = enabled;
  return true;
}

ACTION_P(SetEnterpriseEnrolled, enrolled) {
  return enrolled;
}

class FlexHwisTest : public ::testing::Test {
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
    base::FilePath time_path = test_path_.Append(kHwisFilePath);
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

  void CreateDeviceName() {
    base::FilePath hwis_file_path = test_path_.Append(kHwisFilePath);
    CHECK(base::CreateDirectory(hwis_file_path));
    CHECK(
        base::WriteFile(hwis_file_path.Append("name"), kDeviceNameForTesting));
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
    CreateDeviceName();
    EXPECT_CALL(mock_http_sender_, DeleteDevice(_)).WillOnce(Return(true));
    ExpectApiMetric(kDeleteMetricName, true);
  }

  void ExpectRegisterAction(bool api_call_success) {
    hwis_proto::Device hardware_info;
    hardware_info.set_name(kDeviceNameForTesting);
    DeviceRegisterResult result(api_call_success,
                                hardware_info.SerializeAsString());
    EXPECT_CALL(mock_http_sender_, RegisterNewDevice(_))
        .WillOnce(Return(result));
    ExpectApiMetric(kPostMetricName, api_call_success);
  }

  StrictMock<policy::MockPolicyProvider> mock_policy_provider_;
  StrictMock<policy::MockDevicePolicy> mock_device_policy_;
  StrictMock<MockHttpSender> mock_http_sender_;
  MetricsLibraryMock library_;
  base::ScopedTempDir test_dir_;
  base::FilePath test_path_;
  hwis_proto::Device hardware_info_;
};

TEST_F(FlexHwisTest, HasRunRecently) {
  auto flex_hwis_sender = flex_hwis::FlexHwisSender(
      test_path_, mock_policy_provider_, mock_http_sender_);
  const auto current_from_epoch =
      (base::Time::Now() - base::Time::UnixEpoch()).InSeconds();
  CreateTimeStamp(base::NumberToString(current_from_epoch));

  EXPECT_EQ(flex_hwis_sender.MaybeSend(hardware_info_, library_),
            Result::HasRunRecently);
}

TEST_F(FlexHwisTest, EnrolledWithoutPermission) {
  auto flex_hwis_sender = flex_hwis::FlexHwisSender(
      test_path_, mock_policy_provider_, mock_http_sender_);

  ExpectEnrolled(true);
  ExpectEnrolledHwCollectionResult(false);
  ExpectPermissionMetric(PermissionResult::kPolicyDenial);
  ExpectDeleteAction();

  EXPECT_EQ(flex_hwis_sender.MaybeSend(hardware_info_, library_),
            Result::NotAuthorized);
}

TEST_F(FlexHwisTest, EnrolledWithPermission) {
  auto flex_hwis_sender = flex_hwis::FlexHwisSender(
      test_path_, mock_policy_provider_, mock_http_sender_);

  ExpectEnrolled(true);
  ExpectEnrolledHwCollectionResult(true);
  ExpectPermissionMetric(PermissionResult::kPolicySuccess);
  ExpectRegisterAction(/*api_call_success=*/true);

  EXPECT_EQ(flex_hwis_sender.MaybeSend(hardware_info_, library_), Result::Sent);
}

TEST_F(FlexHwisTest, EnrolledFailToReadPermission) {
  auto flex_hwis_sender = flex_hwis::FlexHwisSender(
      test_path_, mock_policy_provider_, mock_http_sender_);

  ExpectEnrolled(true);
  ExpectEnrolledHwCollectionResult(std::nullopt);
  ExpectPermissionMetric(PermissionResult::kPolicyDenial);

  EXPECT_EQ(flex_hwis_sender.MaybeSend(hardware_info_, library_),
            Result::NotAuthorized);
}

TEST_F(FlexHwisTest, UnenrolledWithoutPermission) {
  auto flex_hwis_sender = flex_hwis::FlexHwisSender(
      test_path_, mock_policy_provider_, mock_http_sender_);
  ExpectEnrolled(false);
  ExpectUnenrolledHwCollectionResult(false);
  ExpectPermissionMetric(PermissionResult::kOptInDenial);
  ExpectDeleteAction();

  EXPECT_EQ(flex_hwis_sender.MaybeSend(hardware_info_, library_),
            Result::NotAuthorized);
}

TEST_F(FlexHwisTest, UnenrolledWithPermission) {
  auto flex_hwis_sender = flex_hwis::FlexHwisSender(
      test_path_, mock_policy_provider_, mock_http_sender_);
  ExpectEnrolled(false);
  ExpectUnenrolledHwCollectionResult(true);
  ExpectPermissionMetric(PermissionResult::kOptInSuccess);
  ExpectRegisterAction(/*api_call_success=*/true);

  EXPECT_EQ(flex_hwis_sender.MaybeSend(hardware_info_, library_), Result::Sent);
}

TEST_F(FlexHwisTest, UnenrolledFailToReadPermission) {
  auto flex_hwis_sender = flex_hwis::FlexHwisSender(
      test_path_, mock_policy_provider_, mock_http_sender_);
  ExpectEnrolled(false);
  ExpectUnenrolledHwCollectionResult(std::nullopt);
  ExpectPermissionMetric(PermissionResult::kOptInDenial);
  ExpectDeleteAction();

  EXPECT_EQ(flex_hwis_sender.MaybeSend(hardware_info_, library_),
            Result::NotAuthorized);
}

TEST_F(FlexHwisTest, UpdateDeviceFail) {
  auto flex_hwis_sender = flex_hwis::FlexHwisSender(
      test_path_, mock_policy_provider_, mock_http_sender_);

  ExpectEnrolled(true);
  ExpectEnrolledHwCollectionResult(true);
  ExpectPermissionMetric(PermissionResult::kPolicySuccess);
  CreateDeviceName();
  EXPECT_CALL(mock_http_sender_, UpdateDevice(_))
      .WillOnce(Return(DeviceUpdateResult::Fail));
  ExpectApiMetric(kPutMetricName, /*device updated result=*/false);
  EXPECT_EQ(flex_hwis_sender.MaybeSend(hardware_info_, library_),
            Result::Error);
}

TEST_F(FlexHwisTest, UpdateDeviceNotFound) {
  auto flex_hwis_sender = flex_hwis::FlexHwisSender(
      test_path_, mock_policy_provider_, mock_http_sender_);

  ExpectEnrolled(true);
  ExpectEnrolledHwCollectionResult(true);
  ExpectPermissionMetric(PermissionResult::kPolicySuccess);
  CreateDeviceName();
  EXPECT_CALL(mock_http_sender_, UpdateDevice(_))
      .WillOnce(Return(DeviceUpdateResult::DeviceNotFound));
  ExpectRegisterAction(/*api_call_success=*/true);
  EXPECT_EQ(flex_hwis_sender.MaybeSend(hardware_info_, library_), Result::Sent);
}

}  // namespace flex_hwis
