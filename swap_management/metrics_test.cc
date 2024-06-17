// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "swap_management/mock_metrics.h"
#include "swap_management/mock_utils.h"

#include <absl/status/status.h>
#include <gtest/gtest.h>

using testing::_;
using testing::DoAll;
using testing::Return;
using testing::SetArgPointee;
using testing::StrictMock;

namespace swap_management {

class MetricsTest : public ::testing::Test {
 public:
  void SetUp() override {
    mock_metrics_ = std::make_unique<MockMetrics>();
    // Init Utils and then replace with mocked one.
    Utils::OverrideForTesting(&mock_util_);
  }

 protected:
  std::unique_ptr<MockMetrics> mock_metrics_;
  MockUtils mock_util_;
};

TEST_F(MetricsTest, PSIParser) {
  // Wrong period.
  EXPECT_THAT(
      mock_metrics_->PSIParser(base::FilePath("/proc/pressure/memory"), 20),
      absl::InvalidArgumentError("Invalid PSI period 20"));

  // Success.
  std::string psi =
      "some avg10=0.75 avg60=0.30 avg300=0.07 total=359504\n"
      "full avg10=0.46 avg60=0.19 avg300=0.04 total=228516";

  EXPECT_CALL(mock_util_,
              ReadFileToString(base::FilePath("/proc/pressure/memory"), _))
      .WillOnce(DoAll(SetArgPointee<1>(psi), Return(absl::OkStatus())));
  std::vector<uint32_t> res = {75, 46};
  EXPECT_THAT(
      mock_metrics_->PSIParser(base::FilePath("/proc/pressure/memory"), 10),
      std::move(res));

  EXPECT_CALL(mock_util_,
              ReadFileToString(base::FilePath("/proc/pressure/memory"), _))
      .WillOnce(DoAll(SetArgPointee<1>(psi), Return(absl::OkStatus())));
  res = {30, 19};
  EXPECT_THAT(
      mock_metrics_->PSIParser(base::FilePath("/proc/pressure/memory"), 60),
      std::move(res));

  EXPECT_CALL(mock_util_,
              ReadFileToString(base::FilePath("/proc/pressure/memory"), _))
      .WillOnce(DoAll(SetArgPointee<1>(psi), Return(absl::OkStatus())));
  res = {7, 4};
  EXPECT_THAT(
      mock_metrics_->PSIParser(base::FilePath("/proc/pressure/memory"), 300),
      std::move(res));
}
}  // namespace swap_management
