// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/metrics/metrics_utils_impl.h"

#include <memory>
#include <string>
#include <vector>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/memory/scoped_refptr.h>
#include <base/time/time.h>
#include <gtest/gtest.h>

#include "rmad/constants.h"
#include "rmad/metrics/metrics_constants.h"
#include "rmad/utils/json_store.h"

namespace {

constexpr char kTestJsonStoreFilename[] = "test.json";

}

namespace rmad {

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
    EXPECT_TRUE(json_store_->SetValue(kFirstSetupTimestamp, current_timestamp));
    EXPECT_TRUE(json_store_->SetValue(kSetupTimestamp, current_timestamp));
  }

  base::ScopedTempDir temp_dir_;
  scoped_refptr<JsonStore> json_store_;
};

TEST_F(MetricsUtilsImplTest, Record_Success) {
  auto metrics_utils = std::make_unique<MetricsUtilsImpl>(false);
  double current_timestamp = base::Time::Now().ToDoubleT();
  EXPECT_TRUE(json_store_->SetValue(kFirstSetupTimestamp, current_timestamp));
  EXPECT_TRUE(json_store_->SetValue(kSetupTimestamp, current_timestamp));
  EXPECT_TRUE(json_store_->SetValue(kRoFirmwareVerified, true));

  EXPECT_TRUE(metrics_utils->Record(json_store_, true));
}

TEST_F(MetricsUtilsImplTest, Record_RoUnsupportedSuccess) {
  auto metrics_utils = std::make_unique<MetricsUtilsImpl>(false);
  EXPECT_TRUE(json_store_->SetValue(kRoFirmwareVerified, false));

  EXPECT_TRUE(metrics_utils->Record(json_store_, true));
}

TEST_F(MetricsUtilsImplTest, Record_RoUnknownSuccess) {
  auto metrics_utils = std::make_unique<MetricsUtilsImpl>(false);

  EXPECT_TRUE(metrics_utils->Record(json_store_, true));
}

TEST_F(MetricsUtilsImplTest, Record_AbortSuccess) {
  auto metrics_utils = std::make_unique<MetricsUtilsImpl>(false);
  EXPECT_TRUE(json_store_->SetValue(kRoFirmwareVerified, true));

  EXPECT_TRUE(metrics_utils->Record(json_store_, false));
}

TEST_F(MetricsUtilsImplTest, Record_SameOnwerSuccess) {
  auto metrics_utils = std::make_unique<MetricsUtilsImpl>(false);
  EXPECT_TRUE(json_store_->SetValue(kRoFirmwareVerified, true));
  EXPECT_TRUE(json_store_->SetValue(kSameOwner, true));

  EXPECT_TRUE(metrics_utils->Record(json_store_, true));
}

TEST_F(MetricsUtilsImplTest, Record_DifferentOnwerSuccess) {
  auto metrics_utils = std::make_unique<MetricsUtilsImpl>(false);
  EXPECT_TRUE(json_store_->SetValue(kRoFirmwareVerified, true));
  EXPECT_TRUE(json_store_->SetValue(kSameOwner, false));

  EXPECT_TRUE(metrics_utils->Record(json_store_, true));
}

TEST_F(MetricsUtilsImplTest, Record_MainboardReplacedSuccess) {
  auto metrics_utils = std::make_unique<MetricsUtilsImpl>(false);
  EXPECT_TRUE(json_store_->SetValue(kRoFirmwareVerified, true));
  EXPECT_TRUE(json_store_->SetValue(kMlbRepair, true));

  EXPECT_TRUE(metrics_utils->Record(json_store_, true));
}

TEST_F(MetricsUtilsImplTest, Record_MainboardOriginalSuccess) {
  auto metrics_utils = std::make_unique<MetricsUtilsImpl>(false);
  EXPECT_TRUE(json_store_->SetValue(kRoFirmwareVerified, true));
  EXPECT_TRUE(json_store_->SetValue(kMlbRepair, false));

  EXPECT_TRUE(metrics_utils->Record(json_store_, true));
}

TEST_F(MetricsUtilsImplTest, Record_WriteProtectDisableSuccess) {
  auto metrics_utils = std::make_unique<MetricsUtilsImpl>(false);
  EXPECT_TRUE(json_store_->SetValue(kRoFirmwareVerified, true));

  // The write protect disable method hasn't been set yet.
  EXPECT_TRUE(metrics_utils->Record(json_store_, true));

  for (auto wp_disable_method : kValidWpDisableMethods) {
    EXPECT_TRUE(json_store_->SetValue(kWriteProtectDisableMethod,
                                      static_cast<int>(wp_disable_method)));
    EXPECT_TRUE(metrics_utils->Record(json_store_, true));
  }
}

TEST_F(MetricsUtilsImplTest, Record_ReplacedComponentsSuccess) {
  auto metrics_utils = std::make_unique<MetricsUtilsImpl>(false);
  EXPECT_TRUE(json_store_->SetValue(kRoFirmwareVerified, true));
  EXPECT_TRUE(json_store_->SetValue(
      kReplacedComponentNames,
      std::vector<std::string>({RmadComponent_Name(RMAD_COMPONENT_AUDIO_CODEC),
                                RmadComponent_Name(RMAD_COMPONENT_BATTERY)})));

  EXPECT_TRUE(metrics_utils->Record(json_store_, true));
}

TEST_F(MetricsUtilsImplTest, Record_OccurredErrorsSuccess) {
  auto metrics_utils = std::make_unique<MetricsUtilsImpl>(false);
  EXPECT_TRUE(json_store_->SetValue(kRoFirmwareVerified, true));
  EXPECT_TRUE(json_store_->SetValue(
      kOccurredErrors, std::vector<std::string>(
                           {RmadErrorCode_Name(RMAD_ERROR_CANNOT_CANCEL_RMA),
                            RmadErrorCode_Name(RMAD_ERROR_CANNOT_GET_LOG)})));

  EXPECT_TRUE(metrics_utils->Record(json_store_, true));
}

TEST_F(MetricsUtilsImplTest, Record_AdditionalActivitiesSuccess) {
  auto metrics_utils = std::make_unique<MetricsUtilsImpl>(false);
  EXPECT_TRUE(json_store_->SetValue(kRoFirmwareVerified, true));
  EXPECT_TRUE(json_store_->SetValue(
      kAdditionalActivities,
      std::vector<int>({static_cast<int>(AdditionalActivity::REBOOT),
                        static_cast<int>(AdditionalActivity::SHUTDOWN)})));

  EXPECT_TRUE(metrics_utils->Record(json_store_, true));
}

TEST_F(MetricsUtilsImplTest, Record_OverallTimeFailed) {
  auto metrics_utils = std::make_unique<MetricsUtilsImpl>(false);
  EXPECT_TRUE(json_store_->SetValue(kFirstSetupTimestamp, ""));
  EXPECT_TRUE(json_store_->SetValue(kRoFirmwareVerified, true));

  EXPECT_FALSE(metrics_utils->Record(json_store_, true));
}

TEST_F(MetricsUtilsImplTest, Record_RunningTimeFailed) {
  auto metrics_utils = std::make_unique<MetricsUtilsImpl>(false);
  EXPECT_TRUE(json_store_->SetValue(kSetupTimestamp, ""));
  EXPECT_TRUE(json_store_->SetValue(kRoFirmwareVerified, true));

  EXPECT_FALSE(metrics_utils->Record(json_store_, true));
}

TEST_F(MetricsUtilsImplTest, Record_UnknownWriteProtectDisableFailed) {
  auto metrics_utils = std::make_unique<MetricsUtilsImpl>(false);
  EXPECT_TRUE(json_store_->SetValue(kRoFirmwareVerified, true));
  EXPECT_TRUE(json_store_->SetValue(kWriteProtectDisableMethod, 123));

  EXPECT_FALSE(metrics_utils->Record(json_store_, true));
}

TEST_F(MetricsUtilsImplTest, Record_UnknownReplacedComponentFailed) {
  auto metrics_utils = std::make_unique<MetricsUtilsImpl>(false);
  EXPECT_TRUE(json_store_->SetValue(kRoFirmwareVerified, true));
  EXPECT_TRUE(json_store_->SetValue(kReplacedComponentNames,
                                    std::vector<std::string>({"ABC"})));

  EXPECT_FALSE(metrics_utils->Record(json_store_, true));
}

TEST_F(MetricsUtilsImplTest, Record_UnknownOccurredErrorFailed) {
  auto metrics_utils = std::make_unique<MetricsUtilsImpl>(false);
  EXPECT_TRUE(json_store_->SetValue(kRoFirmwareVerified, true));
  EXPECT_TRUE(json_store_->SetValue(kOccurredErrors,
                                    std::vector<std::string>({"ABC"})));

  EXPECT_FALSE(metrics_utils->Record(json_store_, true));
}

TEST_F(MetricsUtilsImplTest, Record_UnknownAdditionalActivityFailed) {
  auto metrics_utils = std::make_unique<MetricsUtilsImpl>(false);
  EXPECT_TRUE(json_store_->SetValue(kRoFirmwareVerified, true));
  EXPECT_TRUE(
      json_store_->SetValue(kAdditionalActivities, std::vector<int>(123)));

  EXPECT_FALSE(metrics_utils->Record(json_store_, true));
}

}  // namespace rmad
