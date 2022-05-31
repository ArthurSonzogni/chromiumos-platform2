// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/metrics/metrics_utils.h"
#include "rmad/metrics/metrics_utils_impl.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/memory/scoped_refptr.h>
#include <base/strings/string_number_conversions.h>
#include <base/time/time.h>
#include <gtest/gtest.h>

#include "rmad/common/types.h"
#include "rmad/constants.h"
#include "rmad/metrics/metrics_constants.h"
#include "rmad/utils/json_store.h"

namespace {

constexpr char kTestJsonStoreFilename[] = "test.json";
constexpr char kDefaultMetricsJson[] =
    R"({
      "metrics": {
        "first_setup_timestamp": 123.456,
        "setup_timestamp": 456.789,
        "running_time": 333.333,
        "ro_firmware_verified": true,
        "occurred_errors": [1],
        "additional_activities": [2],
        "state_metrics": {
          "1": {
            "state_setup_timestamp": 0.0,
            "state_overall_time": 123.456
          },
          "2": {
            "state_setup_timestamp": 123.456,
            "state_overall_time": 332.544
          }
        }
      }
    })";
constexpr char kEmptyMetricsJson[] = "{}";

constexpr double kDefaultFirstSetupTimestamp = 123.456;
constexpr double kDefaultSetupTimestamp = 456.789;
constexpr double kDefaultRunningTime = 333.333;
constexpr bool kDefaultRoFirmwareVerified = true;
const std::vector<int> kDefaultOccurredErrors = {1};
const std::vector<int> kDefaultAdditionalActivities = {2};
const std::map<std::string, std::map<std::string, double>>
    kDefaultStateMetrics = {
        {"1",
         {{"state_setup_timestamp", 0.0}, {"state_overall_time", 123.456}}},
        {"2",
         {{"state_setup_timestamp", 123.456},
          {"state_overall_time", 332.544}}}};

constexpr double kTestFirstSetupTimestamp = 111.111;
constexpr double kTestSetupTimestamp = 666.666;
constexpr double kTestRunningTime = 555.555;
constexpr bool kTestRoFirmwareVerified = false;
const std::vector<int> kTestOccurredErrors = {1, 2, 3};
const std::vector<int> kTestAdditionalActivities = {4, 5, 6};
const std::map<std::string, std::map<std::string, double>> kTestStateMetrics = {
    {"3",
     {{"state_setup_timestamp", 111.111}, {"state_overall_time", 555.555}}},
    {"4",
     {{"state_setup_timestamp", 666.666}, {"state_overall_time", 123.456}}}};

constexpr double kTestStateSetupTimestamp = 111.111;
constexpr double kTestStateLeaveTimestamp = 666.666;
constexpr double kTestStateOverallTime = 555.555;

}  // namespace

namespace rmad {

class MetricsUtilsTest : public testing::Test {
 public:
  MetricsUtilsTest() = default;

 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    file_path_ = temp_dir_.GetPath().AppendASCII(kTestJsonStoreFilename);
  }

  bool CreateInputFile(const char* str, int size) {
    if (base::WriteFile(file_path_, str, size) == size) {
      json_store_ = base::MakeRefCounted<JsonStore>(file_path_);
      return true;
    }
    return false;
  }

  base::ScopedTempDir temp_dir_;
  scoped_refptr<JsonStore> json_store_;
  base::FilePath file_path_;
};

TEST_F(MetricsUtilsTest, GetValue) {
  EXPECT_TRUE(
      CreateInputFile(kDefaultMetricsJson, std::size(kDefaultMetricsJson) - 1));

  double first_setup_ts;
  EXPECT_TRUE(MetricsUtils::GetMetricsValue(json_store_, kFirstSetupTimestamp,
                                            &first_setup_ts));
  EXPECT_EQ(first_setup_ts, kDefaultFirstSetupTimestamp);

  double setup_ts;
  EXPECT_TRUE(
      MetricsUtils::GetMetricsValue(json_store_, kSetupTimestamp, &setup_ts));
  EXPECT_EQ(setup_ts, kDefaultSetupTimestamp);

  double running_time;
  EXPECT_TRUE(
      MetricsUtils::GetMetricsValue(json_store_, kRunningTime, &running_time));
  EXPECT_EQ(running_time, kDefaultRunningTime);

  bool ro_fw_verified;
  EXPECT_TRUE(MetricsUtils::GetMetricsValue(json_store_, kRoFirmwareVerified,
                                            &ro_fw_verified));
  EXPECT_EQ(ro_fw_verified, kDefaultRoFirmwareVerified);

  std::vector<int> occurred_errors;
  EXPECT_TRUE(MetricsUtils::GetMetricsValue(json_store_, kOccurredErrors,
                                            &occurred_errors));
  EXPECT_EQ(occurred_errors, kDefaultOccurredErrors);

  std::vector<int> additional_activities;
  EXPECT_TRUE(MetricsUtils::GetMetricsValue(json_store_, kAdditionalActivities,
                                            &additional_activities));
  EXPECT_EQ(additional_activities, kDefaultAdditionalActivities);

  std::map<std::string, std::map<std::string, double>> state_metrics;
  EXPECT_TRUE(MetricsUtils::GetMetricsValue(json_store_, kStateMetrics,
                                            &state_metrics));
  EXPECT_EQ(state_metrics, kDefaultStateMetrics);
}

TEST_F(MetricsUtilsTest, SetValue_FirstSetupTimestamp) {
  EXPECT_TRUE(
      CreateInputFile(kDefaultMetricsJson, std::size(kDefaultMetricsJson) - 1));

  EXPECT_TRUE(MetricsUtils::SetMetricsValue(json_store_, kFirstSetupTimestamp,
                                            kTestFirstSetupTimestamp));

  double first_setup_ts;
  EXPECT_TRUE(MetricsUtils::GetMetricsValue(json_store_, kFirstSetupTimestamp,
                                            &first_setup_ts));
  EXPECT_EQ(first_setup_ts, kTestFirstSetupTimestamp);
}

TEST_F(MetricsUtilsTest, SetValue_SetupTimestamp) {
  EXPECT_TRUE(
      CreateInputFile(kDefaultMetricsJson, std::size(kDefaultMetricsJson) - 1));

  EXPECT_TRUE(MetricsUtils::SetMetricsValue(json_store_, kSetupTimestamp,
                                            kTestSetupTimestamp));

  double setup_ts;
  EXPECT_TRUE(
      MetricsUtils::GetMetricsValue(json_store_, kSetupTimestamp, &setup_ts));
  EXPECT_EQ(setup_ts, kTestSetupTimestamp);
}

TEST_F(MetricsUtilsTest, SetValue_RunningTime) {
  EXPECT_TRUE(
      CreateInputFile(kDefaultMetricsJson, std::size(kDefaultMetricsJson) - 1));

  EXPECT_TRUE(MetricsUtils::SetMetricsValue(json_store_, kRunningTime,
                                            kTestRunningTime));

  double running_time;
  EXPECT_TRUE(
      MetricsUtils::GetMetricsValue(json_store_, kRunningTime, &running_time));
  EXPECT_EQ(running_time, kTestRunningTime);
}

TEST_F(MetricsUtilsTest, SetValue_RoFirmwareVerified) {
  EXPECT_TRUE(
      CreateInputFile(kDefaultMetricsJson, std::size(kDefaultMetricsJson) - 1));

  EXPECT_TRUE(MetricsUtils::SetMetricsValue(json_store_, kRoFirmwareVerified,
                                            kTestRoFirmwareVerified));

  bool ro_fw_verified;
  EXPECT_TRUE(MetricsUtils::GetMetricsValue(json_store_, kRoFirmwareVerified,
                                            &ro_fw_verified));
  EXPECT_EQ(ro_fw_verified, kTestRoFirmwareVerified);
}

TEST_F(MetricsUtilsTest, SetValue_OccurredErrors) {
  EXPECT_TRUE(
      CreateInputFile(kDefaultMetricsJson, std::size(kDefaultMetricsJson) - 1));

  EXPECT_TRUE(MetricsUtils::SetMetricsValue(json_store_, kOccurredErrors,
                                            kTestOccurredErrors));

  std::vector<int> occurred_errors;
  EXPECT_TRUE(MetricsUtils::GetMetricsValue(json_store_, kOccurredErrors,
                                            &occurred_errors));
  EXPECT_EQ(occurred_errors, kTestOccurredErrors);
}

TEST_F(MetricsUtilsTest, SetValue_AddtionalActivities) {
  EXPECT_TRUE(
      CreateInputFile(kDefaultMetricsJson, std::size(kDefaultMetricsJson) - 1));

  EXPECT_TRUE(MetricsUtils::SetMetricsValue(json_store_, kAdditionalActivities,
                                            kTestAdditionalActivities));

  std::vector<int> additional_activities;
  EXPECT_TRUE(MetricsUtils::GetMetricsValue(json_store_, kAdditionalActivities,
                                            &additional_activities));
  EXPECT_EQ(additional_activities, kTestAdditionalActivities);
}

TEST_F(MetricsUtilsTest, SetValue_StateMetrics) {
  EXPECT_TRUE(
      CreateInputFile(kDefaultMetricsJson, std::size(kDefaultMetricsJson) - 1));

  EXPECT_TRUE(MetricsUtils::SetMetricsValue(json_store_, kStateMetrics,
                                            kTestStateMetrics));

  std::map<std::string, std::map<std::string, double>> state_metrics;
  EXPECT_TRUE(MetricsUtils::GetMetricsValue(json_store_, kStateMetrics,
                                            &state_metrics));
  EXPECT_EQ(state_metrics, kTestStateMetrics);
}

TEST_F(MetricsUtilsTest, SetStateSetupTimestamp) {
  EXPECT_TRUE(
      CreateInputFile(kEmptyMetricsJson, std::size(kEmptyMetricsJson) - 1));

  std::map<std::string, std::map<std::string, double>> state_metrics;
  EXPECT_FALSE(MetricsUtils::GetMetricsValue(json_store_, kStateMetrics,
                                             &state_metrics));

  RmadState::StateCase state_case = RmadState::StateCase::kRestock;
  EXPECT_TRUE(MetricsUtils::SetStateSetupTimestamp(
      json_store_, RmadState::StateCase::kRestock, kTestStateSetupTimestamp));

  EXPECT_TRUE(MetricsUtils::GetMetricsValue(json_store_, kStateMetrics,
                                            &state_metrics));

  auto state_it =
      state_metrics.find(base::NumberToString(static_cast<int>(state_case)));
  EXPECT_NE(state_it, state_metrics.end());

  auto ts_it = state_it->second.find(kStateSetupTimestamp);
  EXPECT_NE(ts_it, state_it->second.end());
  EXPECT_EQ(ts_it->second, kTestStateSetupTimestamp);
}

TEST_F(MetricsUtilsTest, CalculateStateOverallTime) {
  EXPECT_TRUE(
      CreateInputFile(kEmptyMetricsJson, std::size(kEmptyMetricsJson) - 1));

  RmadState::StateCase state_case = RmadState::StateCase::kRestock;
  EXPECT_TRUE(MetricsUtils::SetStateSetupTimestamp(
      json_store_, RmadState::StateCase::kRestock, kTestStateSetupTimestamp));

  EXPECT_TRUE(MetricsUtils::CalculateStateOverallTime(
      json_store_, RmadState::StateCase::kRestock, kTestStateLeaveTimestamp));

  std::map<std::string, std::map<std::string, double>> state_metrics;
  EXPECT_TRUE(MetricsUtils::GetMetricsValue(json_store_, kStateMetrics,
                                            &state_metrics));

  auto state_it =
      state_metrics.find(base::NumberToString(static_cast<int>(state_case)));
  EXPECT_NE(state_it, state_metrics.end());

  auto ts_it = state_it->second.find(kStateSetupTimestamp);
  EXPECT_NE(ts_it, state_it->second.end());
  EXPECT_EQ(ts_it->second, kTestStateLeaveTimestamp);

  auto time_it = state_it->second.find(kStateOverallTime);
  EXPECT_NE(time_it, state_it->second.end());
  EXPECT_DOUBLE_EQ(time_it->second, kTestStateOverallTime);
}

class MetricsUtilsImplTest : public testing::Test {
 public:
  MetricsUtilsImplTest() = default;

 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    base::FilePath file_path =
        temp_dir_.GetPath().AppendASCII(kTestJsonStoreFilename);
    json_store_ = base::MakeRefCounted<JsonStore>(file_path);
    double current_timestamp = base::Time::Now().ToDoubleT();
    EXPECT_TRUE(MetricsUtils::SetMetricsValue(json_store_, kFirstSetupTimestamp,
                                              current_timestamp));
    EXPECT_TRUE(MetricsUtils::SetMetricsValue(json_store_, kSetupTimestamp,
                                              current_timestamp));
  }

  base::ScopedTempDir temp_dir_;
  scoped_refptr<JsonStore> json_store_;
};

TEST_F(MetricsUtilsImplTest, Record_Success) {
  auto metrics_utils = std::make_unique<MetricsUtilsImpl>(false);
  double current_timestamp = base::Time::Now().ToDoubleT();
  EXPECT_TRUE(MetricsUtils::SetMetricsValue(json_store_, kFirstSetupTimestamp,
                                            current_timestamp));
  EXPECT_TRUE(MetricsUtils::SetMetricsValue(json_store_, kSetupTimestamp,
                                            current_timestamp));
  EXPECT_TRUE(
      MetricsUtils::SetMetricsValue(json_store_, kRoFirmwareVerified, true));

  EXPECT_TRUE(metrics_utils->Record(json_store_, true));
}

TEST_F(MetricsUtilsImplTest, Record_RoUnsupportedSuccess) {
  auto metrics_utils = std::make_unique<MetricsUtilsImpl>(false);
  EXPECT_TRUE(
      MetricsUtils::SetMetricsValue(json_store_, kRoFirmwareVerified, false));

  EXPECT_TRUE(metrics_utils->Record(json_store_, true));
}

TEST_F(MetricsUtilsImplTest, Record_RoUnknownSuccess) {
  auto metrics_utils = std::make_unique<MetricsUtilsImpl>(false);

  EXPECT_TRUE(metrics_utils->Record(json_store_, true));
}

TEST_F(MetricsUtilsImplTest, Record_AbortSuccess) {
  auto metrics_utils = std::make_unique<MetricsUtilsImpl>(false);
  EXPECT_TRUE(
      MetricsUtils::SetMetricsValue(json_store_, kRoFirmwareVerified, true));

  EXPECT_TRUE(metrics_utils->Record(json_store_, false));
}

TEST_F(MetricsUtilsImplTest, Record_SameOnwerSuccess) {
  auto metrics_utils = std::make_unique<MetricsUtilsImpl>(false);
  EXPECT_TRUE(
      MetricsUtils::SetMetricsValue(json_store_, kRoFirmwareVerified, true));
  EXPECT_TRUE(json_store_->SetValue(kSameOwner, true));

  EXPECT_TRUE(metrics_utils->Record(json_store_, true));
}

TEST_F(MetricsUtilsImplTest, Record_DifferentOnwerSuccess) {
  auto metrics_utils = std::make_unique<MetricsUtilsImpl>(false);
  EXPECT_TRUE(
      MetricsUtils::SetMetricsValue(json_store_, kRoFirmwareVerified, true));
  EXPECT_TRUE(json_store_->SetValue(kSameOwner, false));

  EXPECT_TRUE(metrics_utils->Record(json_store_, true));
}

TEST_F(MetricsUtilsImplTest, Record_MainboardReplacedSuccess) {
  auto metrics_utils = std::make_unique<MetricsUtilsImpl>(false);
  EXPECT_TRUE(
      MetricsUtils::SetMetricsValue(json_store_, kRoFirmwareVerified, true));
  EXPECT_TRUE(json_store_->SetValue(kMlbRepair, true));

  EXPECT_TRUE(metrics_utils->Record(json_store_, true));
}

TEST_F(MetricsUtilsImplTest, Record_MainboardOriginalSuccess) {
  auto metrics_utils = std::make_unique<MetricsUtilsImpl>(false);
  EXPECT_TRUE(
      MetricsUtils::SetMetricsValue(json_store_, kRoFirmwareVerified, true));
  EXPECT_TRUE(json_store_->SetValue(kMlbRepair, false));

  EXPECT_TRUE(metrics_utils->Record(json_store_, true));
}

TEST_F(MetricsUtilsImplTest, Record_WriteProtectDisableSuccess) {
  auto metrics_utils = std::make_unique<MetricsUtilsImpl>(false);
  EXPECT_TRUE(
      MetricsUtils::SetMetricsValue(json_store_, kRoFirmwareVerified, true));

  // The write protect disable method hasn't been set yet.
  EXPECT_TRUE(metrics_utils->Record(json_store_, true));

  std::array<WpDisableMethod, 5> methods = {
      WpDisableMethod::UNKNOWN, WpDisableMethod::SKIPPED, WpDisableMethod::RSU,
      WpDisableMethod::PHYSICAL_ASSEMBLE_DEVICE,
      WpDisableMethod::PHYSICAL_KEEP_DEVICE_OPEN};
  for (auto wp_disable_method : methods) {
    EXPECT_TRUE(
        MetricsUtils::SetMetricsValue(json_store_, kWpDisableMethod,
                                      WpDisableMethod_Name(wp_disable_method)));
    EXPECT_TRUE(metrics_utils->Record(json_store_, true));
  }
}

TEST_F(MetricsUtilsImplTest, Record_ReplacedComponentsSuccess) {
  auto metrics_utils = std::make_unique<MetricsUtilsImpl>(false);
  EXPECT_TRUE(
      MetricsUtils::SetMetricsValue(json_store_, kRoFirmwareVerified, true));
  EXPECT_TRUE(json_store_->SetValue(
      kReplacedComponentNames,
      std::vector<std::string>({RmadComponent_Name(RMAD_COMPONENT_AUDIO_CODEC),
                                RmadComponent_Name(RMAD_COMPONENT_BATTERY)})));

  EXPECT_TRUE(metrics_utils->Record(json_store_, true));
}

TEST_F(MetricsUtilsImplTest, Record_OccurredErrorsSuccess) {
  auto metrics_utils = std::make_unique<MetricsUtilsImpl>(false);
  EXPECT_TRUE(
      MetricsUtils::SetMetricsValue(json_store_, kRoFirmwareVerified, true));
  EXPECT_TRUE(json_store_->SetValue(
      kOccurredErrors, std::vector<std::string>(
                           {RmadErrorCode_Name(RMAD_ERROR_CANNOT_CANCEL_RMA),
                            RmadErrorCode_Name(RMAD_ERROR_CANNOT_GET_LOG)})));

  EXPECT_TRUE(metrics_utils->Record(json_store_, true));
}

TEST_F(MetricsUtilsImplTest, Record_AdditionalActivitiesSuccess) {
  auto metrics_utils = std::make_unique<MetricsUtilsImpl>(false);
  EXPECT_TRUE(
      MetricsUtils::SetMetricsValue(json_store_, kRoFirmwareVerified, true));
  EXPECT_TRUE(MetricsUtils::SetMetricsValue(
      json_store_, kAdditionalActivities,
      std::vector<int>({static_cast<int>(AdditionalActivity::REBOOT),
                        static_cast<int>(AdditionalActivity::SHUTDOWN)})));

  EXPECT_TRUE(metrics_utils->Record(json_store_, true));
}

TEST_F(MetricsUtilsImplTest, Record_OverallTimeFailed) {
  auto metrics_utils = std::make_unique<MetricsUtilsImpl>(false);
  EXPECT_TRUE(
      MetricsUtils::SetMetricsValue(json_store_, kFirstSetupTimestamp, ""));
  EXPECT_TRUE(
      MetricsUtils::SetMetricsValue(json_store_, kRoFirmwareVerified, true));

  EXPECT_FALSE(metrics_utils->Record(json_store_, true));
}

TEST_F(MetricsUtilsImplTest, Record_RunningTimeFailed) {
  auto metrics_utils = std::make_unique<MetricsUtilsImpl>(false);
  EXPECT_TRUE(MetricsUtils::SetMetricsValue(json_store_, kSetupTimestamp, ""));
  EXPECT_TRUE(
      MetricsUtils::SetMetricsValue(json_store_, kRoFirmwareVerified, true));

  EXPECT_FALSE(metrics_utils->Record(json_store_, true));
}

TEST_F(MetricsUtilsImplTest, Record_UnknownWriteProtectDisableFailed) {
  auto metrics_utils = std::make_unique<MetricsUtilsImpl>(false);
  EXPECT_TRUE(
      MetricsUtils::SetMetricsValue(json_store_, kRoFirmwareVerified, true));
  EXPECT_TRUE(
      MetricsUtils::SetMetricsValue(json_store_, kWpDisableMethod, "abc"));

  EXPECT_FALSE(metrics_utils->Record(json_store_, true));
}

TEST_F(MetricsUtilsImplTest, Record_UnknownReplacedComponentFailed) {
  auto metrics_utils = std::make_unique<MetricsUtilsImpl>(false);
  EXPECT_TRUE(
      MetricsUtils::SetMetricsValue(json_store_, kRoFirmwareVerified, true));
  EXPECT_TRUE(json_store_->SetValue(kReplacedComponentNames,
                                    std::vector<std::string>({"ABC"})));

  EXPECT_FALSE(metrics_utils->Record(json_store_, true));
}

TEST_F(MetricsUtilsImplTest, Record_UnknownOccurredErrorFailed) {
  auto metrics_utils = std::make_unique<MetricsUtilsImpl>(false);
  EXPECT_TRUE(
      MetricsUtils::SetMetricsValue(json_store_, kRoFirmwareVerified, true));
  EXPECT_TRUE(MetricsUtils::SetMetricsValue(json_store_, kOccurredErrors,
                                            std::vector<std::string>({"ABC"})));

  EXPECT_FALSE(metrics_utils->Record(json_store_, true));
}

TEST_F(MetricsUtilsImplTest, Record_UnknownAdditionalActivityFailed) {
  auto metrics_utils = std::make_unique<MetricsUtilsImpl>(false);
  EXPECT_TRUE(
      MetricsUtils::SetMetricsValue(json_store_, kRoFirmwareVerified, true));
  EXPECT_TRUE(MetricsUtils::SetMetricsValue(json_store_, kAdditionalActivities,
                                            std::vector<int>(123)));

  EXPECT_FALSE(metrics_utils->Record(json_store_, true));
}

}  // namespace rmad
