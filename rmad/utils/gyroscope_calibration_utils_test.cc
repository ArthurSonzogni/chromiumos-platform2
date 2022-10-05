// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/gyroscope_calibration_utils_impl.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "rmad/utils/mock_iio_ec_sensor_utils.h"

using testing::_;
using testing::DoAll;
using testing::Return;
using testing::SetArgPointee;
using testing::StrictMock;
using testing::WithArg;

namespace {

constexpr char kLocation[] = "TestLocation";
constexpr char kName[] = "TestName";

constexpr double kDegree2Radian = M_PI / 180.0;
// The calibbias data unit is 1/1024 dps, and the sensor reading is rad/s.
constexpr double kCalibbias2SensorReading = kDegree2Radian / 1024;

constexpr double kProgressComplete = 1.0;
constexpr double kProgressFailed = -1.0;
constexpr double kProgressInit = 0.0;

const std::vector<std::string> kGyroscopeCalibbias = {"in_anglvel_x_calibbias",
                                                      "in_anglvel_y_calibbias",
                                                      "in_anglvel_z_calibbias"};
const std::vector<std::string> kGyroscopeChannels = {"anglvel_x", "anglvel_y",
                                                     "anglvel_z"};

const std::vector<double> kAvgTestData = {111, 222, 333};
const std::vector<double> kOriginalBias = {123, 456, 789};
const std::vector<double> kZeroOriginalBias = {0, 0, 0};

}  // namespace

namespace rmad {

class GyroscopeCalibrationUtilsImplTest : public testing::Test {
 public:
  GyroscopeCalibrationUtilsImplTest() {}

  std::unique_ptr<GyroscopeCalibrationUtilsImpl>
  CreateGyroscopeCalibrationUtils(const std::vector<double>& avg_data,
                                  const std::vector<double>& sys_values) {
    auto mock_output_helper = [](const std::vector<double>& data,
                                 std::vector<double>* output) {
      if (data.size() == 0 || !output) {
        return false;
      }
      *output = data;
      return true;
    };

    auto mock_iio_ec_sensor_utils =
        std::make_unique<StrictMock<MockIioEcSensorUtils>>(kLocation, kName);
    EXPECT_CALL(*mock_iio_ec_sensor_utils,
                GetAvgData(kGyroscopeChannels, _, _, _))
        .WillRepeatedly(WithArg<2>(
            [avg_data, &mock_output_helper](std::vector<double>* output) {
              return mock_output_helper(avg_data, output);
            }));

    EXPECT_CALL(*mock_iio_ec_sensor_utils, GetSysValues(kGyroscopeCalibbias, _))
        .WillRepeatedly(WithArg<1>(
            [sys_values, &mock_output_helper](std::vector<double>* output) {
              return mock_output_helper(sys_values, output);
            }));

    return std::make_unique<GyroscopeCalibrationUtilsImpl>(
        kLocation, kName, std::move(mock_iio_ec_sensor_utils));
  }

  void QueueProgress(double progress) {
    received_progresses_.push_back(progress);
  }

  void QueueResult(const std::map<std::string, int>& result) {
    for (auto [ignore_keyname, value] : result) {
      received_results_.push_back(value);
    }
  }

 protected:
  std::vector<double> received_progresses_;
  std::vector<int> received_results_;
};

TEST_F(GyroscopeCalibrationUtilsImplTest,
       Calibrate_WithoutOriginalBias_Success) {
  auto gyro_calib_utils =
      CreateGyroscopeCalibrationUtils(kAvgTestData, kZeroOriginalBias);

  gyro_calib_utils->Calibrate(
      base::BindRepeating(&GyroscopeCalibrationUtilsImplTest::QueueProgress,
                          base::Unretained(this)),
      base::BindOnce(&GyroscopeCalibrationUtilsImplTest::QueueResult,
                     base::Unretained(this)));

  // Check if sent progresses contain kProgressInit to kProgressComplete.
  EXPECT_GE(received_progresses_.size(), 2);
  EXPECT_DOUBLE_EQ(received_progresses_.front(), kProgressInit);
  EXPECT_DOUBLE_EQ(received_progresses_.back(), kProgressComplete);

  EXPECT_EQ(received_results_.size(), kGyroscopeChannels.size());
  for (int i = 0; i < kAvgTestData.size(); i++) {
    EXPECT_EQ(received_results_[i],
              round(-kAvgTestData[i] / kCalibbias2SensorReading));
  }
}

TEST_F(GyroscopeCalibrationUtilsImplTest, Calibrate_WithOriginalBias_Success) {
  auto gyro_calib_utils =
      CreateGyroscopeCalibrationUtils(kAvgTestData, kOriginalBias);

  gyro_calib_utils->Calibrate(
      base::BindRepeating(&GyroscopeCalibrationUtilsImplTest::QueueProgress,
                          base::Unretained(this)),
      base::BindOnce(&GyroscopeCalibrationUtilsImplTest::QueueResult,
                     base::Unretained(this)));

  // Check if sent progresses contain kProgressInit to kProgressComplete.
  EXPECT_GE(received_progresses_.size(), 2);
  EXPECT_DOUBLE_EQ(received_progresses_.front(), kProgressInit);
  EXPECT_DOUBLE_EQ(received_progresses_.back(), kProgressComplete);

  EXPECT_EQ(received_results_.size(), kGyroscopeChannels.size());
  for (int i = 0; i < kAvgTestData.size(); i++) {
    EXPECT_EQ(
        received_results_[i],
        kOriginalBias[i] + round(-kAvgTestData[i] / kCalibbias2SensorReading));
  }
}

TEST_F(GyroscopeCalibrationUtilsImplTest, Calibrate_NoAvgData_Failed) {
  auto gyro_calib_utils =
      CreateGyroscopeCalibrationUtils({}, kZeroOriginalBias);

  gyro_calib_utils->Calibrate(
      base::BindRepeating(&GyroscopeCalibrationUtilsImplTest::QueueProgress,
                          base::Unretained(this)),
      base::BindOnce(&GyroscopeCalibrationUtilsImplTest::QueueResult,
                     base::Unretained(this)));

  // Check if sent progresses contain kProgressInit to kProgressFailed.
  EXPECT_GE(received_progresses_.size(), 2);
  EXPECT_DOUBLE_EQ(received_progresses_.front(), kProgressInit);
  EXPECT_DOUBLE_EQ(received_progresses_.back(), kProgressFailed);

  // Check if nothing is sent on failure.
  EXPECT_EQ(received_results_.size(), 0);
}

TEST_F(GyroscopeCalibrationUtilsImplTest, Calibrate_NoSysValues_Failed) {
  auto gyro_calib_utils = CreateGyroscopeCalibrationUtils(kAvgTestData, {});

  gyro_calib_utils->Calibrate(
      base::BindRepeating(&GyroscopeCalibrationUtilsImplTest::QueueProgress,
                          base::Unretained(this)),
      base::BindOnce(&GyroscopeCalibrationUtilsImplTest::QueueResult,
                     base::Unretained(this)));

  // Check if sent progresses contain kProgressInit to kProgressFailed.
  EXPECT_GE(received_progresses_.size(), 2);
  EXPECT_DOUBLE_EQ(received_progresses_.front(), kProgressInit);
  EXPECT_DOUBLE_EQ(received_progresses_.back(), kProgressFailed);

  // Check if nothing is sent on failure.
  EXPECT_EQ(received_results_.size(), 0);
}

TEST_F(GyroscopeCalibrationUtilsImplTest, Calibrate_WrongAvgDataSize_Failed) {
  auto gyro_calib_utils =
      CreateGyroscopeCalibrationUtils({1}, kZeroOriginalBias);

  gyro_calib_utils->Calibrate(
      base::BindRepeating(&GyroscopeCalibrationUtilsImplTest::QueueProgress,
                          base::Unretained(this)),
      base::BindOnce(&GyroscopeCalibrationUtilsImplTest::QueueResult,
                     base::Unretained(this)));

  // Check if sent progresses contain kProgressInit to kProgressFailed.
  EXPECT_GE(received_progresses_.size(), 2);
  EXPECT_DOUBLE_EQ(received_progresses_.front(), kProgressInit);
  EXPECT_DOUBLE_EQ(received_progresses_.back(), kProgressFailed);

  // Check if nothing is sent on failure.
  EXPECT_EQ(received_results_.size(), 0);
}

}  // namespace rmad
