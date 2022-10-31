// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/accelerometer_calibration_utils_impl.h"

#include <array>
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
using testing::WithArgs;

namespace {

constexpr char kLocation[] = "TestLocation";
constexpr char kSensorName[] = "cros-ec-accel";

constexpr double kGravity = 9.80665;
// The calibbias data unit is G/1024, and the sensor reading unit is m/s^2.
constexpr double kCalibbias2SensorReading = kGravity / 1024.0;
// Both thresholds are used in m/s^2 units.
// The offset is indicating the tolerance in m/s^2 for
// the digital output of sensors under 0 and 1G.
constexpr double kOffsetThreshold = 2.0;
// The variance of capture data can not be larger than the threshold.
constexpr double kVarianceThreshold = 5.0;

constexpr double kProgressComplete = 1.0;
constexpr double kProgressFailed = -1.0;
constexpr double kProgressInit = 0.0;

const std::vector<std::string> kAccelerometerCalibbias = {
    "in_accel_x_calibbias", "in_accel_y_calibbias", "in_accel_z_calibbias"};
const std::vector<std::string> kAccelerometerChannels = {"accel_x", "accel_y",
                                                         "accel_z"};

constexpr std::array<double, 3> kAccelerometerIdealValues = {0, 0, kGravity};

const std::vector<double> kValidAvgData = {1.1, 1.2, kGravity + 1.3};
const std::vector<double> kInvalidAvgData = {kOffsetThreshold + 0.1,
                                             kOffsetThreshold + 0.2,
                                             kGravity + kOffsetThreshold + 0.3};
const std::vector<double> kValidVariance = {1, 2, 3};
const std::vector<double> kInvalidVariance = {
    kVarianceThreshold + 1, kVarianceThreshold + 2, kVarianceThreshold + 3};
const std::vector<double> kOriginalBias = {101, 102, 103};
const std::vector<double> kZeroOriginalBias = {0, 0, 0};

}  // namespace

namespace rmad {

class AccelerometerCalibrationUtilsImplTest : public testing::Test {
 public:
  AccelerometerCalibrationUtilsImplTest() {}

  std::unique_ptr<AccelerometerCalibrationUtilsImpl>
  CreateAccelerometerCalibrationUtils(const std::vector<double>& avg_data,
                                      const std::vector<double>& variance,
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
        std::make_unique<StrictMock<MockIioEcSensorUtils>>(kLocation,
                                                           kSensorName);
    EXPECT_CALL(*mock_iio_ec_sensor_utils,
                GetAvgData(kAccelerometerChannels, _, _, _))
        .WillRepeatedly(
            WithArgs<2, 3>([avg_data, variance, &mock_output_helper](
                               std::vector<double>* output_avg_data,
                               std::vector<double>* output_variance) {
              return mock_output_helper(avg_data, output_avg_data) &&
                     mock_output_helper(variance, output_variance);
            }));

    EXPECT_CALL(*mock_iio_ec_sensor_utils,
                GetSysValues(kAccelerometerCalibbias, _))
        .WillRepeatedly(
            WithArgs<1>([sys_values, &mock_output_helper](
                            std::vector<double>* output_sys_values) {
              return mock_output_helper(sys_values, output_sys_values);
            }));

    return std::make_unique<AccelerometerCalibrationUtilsImpl>(
        kLocation, std::move(mock_iio_ec_sensor_utils));
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

TEST_F(AccelerometerCalibrationUtilsImplTest,
       Calibrate_WithoutOriginalBias_Success) {
  auto acc_calib_utils = CreateAccelerometerCalibrationUtils(
      kValidAvgData, kValidVariance, kZeroOriginalBias);

  acc_calib_utils->Calibrate(
      base::BindRepeating(&AccelerometerCalibrationUtilsImplTest::QueueProgress,
                          base::Unretained(this)),
      base::BindOnce(&AccelerometerCalibrationUtilsImplTest::QueueResult,
                     base::Unretained(this)));

  // Check if sent progresses contain kProgressInit to kProgressComplete.
  EXPECT_GE(received_progresses_.size(), 2);
  EXPECT_DOUBLE_EQ(received_progresses_.front(), kProgressInit);
  EXPECT_DOUBLE_EQ(received_progresses_.back(), kProgressComplete);

  EXPECT_EQ(received_results_.size(), kAccelerometerChannels.size());
  for (int i = 0; i < kValidAvgData.size(); i++) {
    EXPECT_EQ(received_results_[i],
              round((kAccelerometerIdealValues[i] - kValidAvgData[i]) /
                    kCalibbias2SensorReading));
  }
}

TEST_F(AccelerometerCalibrationUtilsImplTest,
       Calibrate_WithOriginalBias_Success) {
  auto acc_calib_utils = CreateAccelerometerCalibrationUtils(
      kValidAvgData, kValidVariance, kOriginalBias);

  acc_calib_utils->Calibrate(
      base::BindRepeating(&AccelerometerCalibrationUtilsImplTest::QueueProgress,
                          base::Unretained(this)),
      base::BindOnce(&AccelerometerCalibrationUtilsImplTest::QueueResult,
                     base::Unretained(this)));

  // Check if sent progresses contain kProgressInit to kProgressComplete.
  EXPECT_GE(received_progresses_.size(), 2);
  EXPECT_DOUBLE_EQ(received_progresses_.front(), kProgressInit);
  EXPECT_DOUBLE_EQ(received_progresses_.back(), kProgressComplete);

  EXPECT_EQ(received_results_.size(), kAccelerometerChannels.size());
  for (int i = 0; i < kValidAvgData.size(); i++) {
    EXPECT_EQ(received_results_[i],
              kOriginalBias[i] +
                  round((kAccelerometerIdealValues[i] - kValidAvgData[i]) /
                        kCalibbias2SensorReading));
  }
}

TEST_F(AccelerometerCalibrationUtilsImplTest, Calibrate_NoSysValues_Failed) {
  auto acc_calib_utils =
      CreateAccelerometerCalibrationUtils(kValidAvgData, kValidVariance, {});

  acc_calib_utils->Calibrate(
      base::BindRepeating(&AccelerometerCalibrationUtilsImplTest::QueueProgress,
                          base::Unretained(this)),
      base::BindOnce(&AccelerometerCalibrationUtilsImplTest::QueueResult,
                     base::Unretained(this)));

  // Check if sent progresses contain kProgressInit to kProgressFailed.
  EXPECT_GE(received_progresses_.size(), 2);
  EXPECT_DOUBLE_EQ(received_progresses_.front(), kProgressInit);
  EXPECT_DOUBLE_EQ(received_progresses_.back(), kProgressFailed);
}

TEST_F(AccelerometerCalibrationUtilsImplTest, Calibrate_NoAvgData_Failed) {
  auto acc_calib_utils = CreateAccelerometerCalibrationUtils({}, kValidVariance,
                                                             kZeroOriginalBias);

  acc_calib_utils->Calibrate(
      base::BindRepeating(&AccelerometerCalibrationUtilsImplTest::QueueProgress,
                          base::Unretained(this)),
      base::BindOnce(&AccelerometerCalibrationUtilsImplTest::QueueResult,
                     base::Unretained(this)));

  // Check if sent progresses contain kProgressInit to kProgressFailed.
  EXPECT_GE(received_progresses_.size(), 2);
  EXPECT_DOUBLE_EQ(received_progresses_.front(), kProgressInit);
  EXPECT_DOUBLE_EQ(received_progresses_.back(), kProgressFailed);
}

TEST_F(AccelerometerCalibrationUtilsImplTest,
       Calibrate_WrongAvgDataSize_Failed) {
  auto acc_calib_utils = CreateAccelerometerCalibrationUtils(
      {1}, kValidVariance, kZeroOriginalBias);

  acc_calib_utils->Calibrate(
      base::BindRepeating(&AccelerometerCalibrationUtilsImplTest::QueueProgress,
                          base::Unretained(this)),
      base::BindOnce(&AccelerometerCalibrationUtilsImplTest::QueueResult,
                     base::Unretained(this)));

  // Check if sent progresses contain kProgressInit to kProgressFailed.
  EXPECT_GE(received_progresses_.size(), 2);
  EXPECT_DOUBLE_EQ(received_progresses_.front(), kProgressInit);
  EXPECT_DOUBLE_EQ(received_progresses_.back(), kProgressFailed);
}

TEST_F(AccelerometerCalibrationUtilsImplTest,
       Calibrate_AvgDataOverThreshold_Failed) {
  auto acc_calib_utils = CreateAccelerometerCalibrationUtils(
      kInvalidAvgData, kValidVariance, kZeroOriginalBias);

  acc_calib_utils->Calibrate(
      base::BindRepeating(&AccelerometerCalibrationUtilsImplTest::QueueProgress,
                          base::Unretained(this)),
      base::BindOnce(&AccelerometerCalibrationUtilsImplTest::QueueResult,
                     base::Unretained(this)));

  // Check if sent progresses contain kProgressInit to kProgressFailed.
  EXPECT_GE(received_progresses_.size(), 2);
  EXPECT_DOUBLE_EQ(received_progresses_.front(), kProgressInit);
  EXPECT_DOUBLE_EQ(received_progresses_.back(), kProgressFailed);
}

TEST_F(AccelerometerCalibrationUtilsImplTest, Calibrate_NoVariance_Failed) {
  auto acc_calib_utils =
      CreateAccelerometerCalibrationUtils(kValidAvgData, {}, kZeroOriginalBias);

  acc_calib_utils->Calibrate(
      base::BindRepeating(&AccelerometerCalibrationUtilsImplTest::QueueProgress,
                          base::Unretained(this)),
      base::BindOnce(&AccelerometerCalibrationUtilsImplTest::QueueResult,
                     base::Unretained(this)));

  // Check if sent progresses contain kProgressInit to kProgressFailed.
  EXPECT_GE(received_progresses_.size(), 2);
  EXPECT_DOUBLE_EQ(received_progresses_.front(), kProgressInit);
  EXPECT_DOUBLE_EQ(received_progresses_.back(), kProgressFailed);
}

TEST_F(AccelerometerCalibrationUtilsImplTest,
       Calibrate_WrongVarianceSize_Failed) {
  auto acc_calib_utils = CreateAccelerometerCalibrationUtils(kValidAvgData, {1},
                                                             kZeroOriginalBias);

  acc_calib_utils->Calibrate(
      base::BindRepeating(&AccelerometerCalibrationUtilsImplTest::QueueProgress,
                          base::Unretained(this)),
      base::BindOnce(&AccelerometerCalibrationUtilsImplTest::QueueResult,
                     base::Unretained(this)));

  // Check if sent progresses contain kProgressInit to kProgressFailed.
  EXPECT_GE(received_progresses_.size(), 2);
  EXPECT_DOUBLE_EQ(received_progresses_.front(), kProgressInit);
  EXPECT_DOUBLE_EQ(received_progresses_.back(), kProgressFailed);
}

TEST_F(AccelerometerCalibrationUtilsImplTest,
       Calibrate_VarianceOverThreshold_Failed) {
  auto acc_calib_utils = CreateAccelerometerCalibrationUtils(
      kValidAvgData, kInvalidVariance, kZeroOriginalBias);

  acc_calib_utils->Calibrate(
      base::BindRepeating(&AccelerometerCalibrationUtilsImplTest::QueueProgress,
                          base::Unretained(this)),
      base::BindOnce(&AccelerometerCalibrationUtilsImplTest::QueueResult,
                     base::Unretained(this)));

  // Check if sent progresses contain kProgressInit to kProgressFailed.
  EXPECT_GE(received_progresses_.size(), 2);
  EXPECT_DOUBLE_EQ(received_progresses_.front(), kProgressInit);
  EXPECT_DOUBLE_EQ(received_progresses_.back(), kProgressFailed);
}

}  // namespace rmad
