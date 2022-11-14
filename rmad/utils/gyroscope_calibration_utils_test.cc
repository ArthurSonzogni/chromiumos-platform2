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
constexpr char kSensorName[] = "cros-ec-gyro";

constexpr double kProgressFailed = -1.0;
constexpr double kProgressInit = 0.0;
constexpr double kProgressGetOriginalCalibbias = 0.2;
constexpr double kProgressComplete = 1.0;

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
  GyroscopeCalibrationUtilsImplTest() = default;

  void DefineGetSysValuesActions(
      std::unique_ptr<StrictMock<MockIioEcSensorUtils>>&
          mock_iio_ec_sensor_utils,
      const std::vector<double>& sys_values) {
    auto mock_output_helper = [](const std::vector<double>& data,
                                 std::vector<double>* output) {
      if (data.size() == 0 || !output) {
        return false;
      }
      *output = data;
      return true;
    };

    EXPECT_CALL(*mock_iio_ec_sensor_utils, GetSysValues(kGyroscopeCalibbias, _))
        .WillRepeatedly(WithArg<1>(
            [sys_values, &mock_output_helper](std::vector<double>* output) {
              return mock_output_helper(sys_values, output);
            }));
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
  auto mock_iio_ec_sensor_utils =
      std::make_unique<StrictMock<MockIioEcSensorUtils>>(kLocation,
                                                         kSensorName);

  DefineGetSysValuesActions(mock_iio_ec_sensor_utils, kZeroOriginalBias);
  EXPECT_CALL(*mock_iio_ec_sensor_utils, GetAvgData(_, kGyroscopeChannels, _))
      .Times(1)
      .WillRepeatedly(Return(true));

  auto gyro_calib_utils = std::make_unique<GyroscopeCalibrationUtilsImpl>(
      kLocation, std::move(mock_iio_ec_sensor_utils));

  gyro_calib_utils->Calibrate(
      base::BindRepeating(&GyroscopeCalibrationUtilsImplTest::QueueProgress,
                          base::Unretained(this)),
      base::BindOnce(&GyroscopeCalibrationUtilsImplTest::QueueResult,
                     base::Unretained(this)));

  // Check if sent progresses contain kProgressInit to kProgressComplete.
  EXPECT_EQ(received_progresses_.size(), 2);
  EXPECT_DOUBLE_EQ(received_progresses_.front(), kProgressInit);
  EXPECT_DOUBLE_EQ(received_progresses_.back(), kProgressGetOriginalCalibbias);
}

TEST_F(GyroscopeCalibrationUtilsImplTest, Calibrate_WithOriginalBias_Success) {
  auto mock_iio_ec_sensor_utils =
      std::make_unique<StrictMock<MockIioEcSensorUtils>>(kLocation,
                                                         kSensorName);

  DefineGetSysValuesActions(mock_iio_ec_sensor_utils, kOriginalBias);
  EXPECT_CALL(*mock_iio_ec_sensor_utils, GetAvgData(_, kGyroscopeChannels, _))
      .Times(1)
      .WillRepeatedly(Return(true));

  auto gyro_calib_utils = std::make_unique<GyroscopeCalibrationUtilsImpl>(
      kLocation, std::move(mock_iio_ec_sensor_utils));

  gyro_calib_utils->Calibrate(
      base::BindRepeating(&GyroscopeCalibrationUtilsImplTest::QueueProgress,
                          base::Unretained(this)),
      base::BindOnce(&GyroscopeCalibrationUtilsImplTest::QueueResult,
                     base::Unretained(this)));

  // Check if sent progresses contain kProgressInit to
  // kProgressGetOriginalCalibbias.
  EXPECT_EQ(received_progresses_.size(), 2);
  EXPECT_DOUBLE_EQ(received_progresses_.front(), kProgressInit);
  EXPECT_DOUBLE_EQ(received_progresses_.back(), kProgressGetOriginalCalibbias);
}

TEST_F(GyroscopeCalibrationUtilsImplTest, Calibrate_NoAvgData_Failed) {
  auto mock_iio_ec_sensor_utils =
      std::make_unique<StrictMock<MockIioEcSensorUtils>>(kLocation,
                                                         kSensorName);

  DefineGetSysValuesActions(mock_iio_ec_sensor_utils, kZeroOriginalBias);
  EXPECT_CALL(*mock_iio_ec_sensor_utils, GetAvgData(_, kGyroscopeChannels, _))
      .Times(1)
      .WillRepeatedly(Return(false));

  auto gyro_calib_utils = std::make_unique<GyroscopeCalibrationUtilsImpl>(
      kLocation, std::move(mock_iio_ec_sensor_utils));

  gyro_calib_utils->Calibrate(
      base::BindRepeating(&GyroscopeCalibrationUtilsImplTest::QueueProgress,
                          base::Unretained(this)),
      base::BindOnce(&GyroscopeCalibrationUtilsImplTest::QueueResult,
                     base::Unretained(this)));

  // Check if sent progresses contain kProgressInit,
  // kProgressGetOriginalCalibbias, and kProgressFailed.
  EXPECT_EQ(received_progresses_.size(), 3);
  EXPECT_DOUBLE_EQ(received_progresses_.front(), kProgressInit);
  EXPECT_DOUBLE_EQ(received_progresses_.back(), kProgressFailed);
}

TEST_F(GyroscopeCalibrationUtilsImplTest, Calibrate_NoSysValues_Failed) {
  auto mock_iio_ec_sensor_utils =
      std::make_unique<StrictMock<MockIioEcSensorUtils>>(kLocation,
                                                         kSensorName);

  DefineGetSysValuesActions(mock_iio_ec_sensor_utils, {});
  EXPECT_CALL(*mock_iio_ec_sensor_utils, GetAvgData(_, kGyroscopeChannels, _))
      .Times(0);

  auto gyro_calib_utils = std::make_unique<GyroscopeCalibrationUtilsImpl>(
      kLocation, std::move(mock_iio_ec_sensor_utils));

  gyro_calib_utils->Calibrate(
      base::BindRepeating(&GyroscopeCalibrationUtilsImplTest::QueueProgress,
                          base::Unretained(this)),
      base::BindOnce(&GyroscopeCalibrationUtilsImplTest::QueueResult,
                     base::Unretained(this)));

  // Check if sent progresses contain kProgressInit and kProgressFailed.
  EXPECT_EQ(received_progresses_.size(), 2);
  EXPECT_DOUBLE_EQ(received_progresses_.front(), kProgressInit);
  EXPECT_DOUBLE_EQ(received_progresses_.back(), kProgressFailed);
}

TEST_F(GyroscopeCalibrationUtilsImplTest, HandleGetAvgDataResult_Success) {
  auto mock_iio_ec_sensor_utils =
      std::make_unique<StrictMock<MockIioEcSensorUtils>>(kLocation,
                                                         kSensorName);

  DefineGetSysValuesActions(mock_iio_ec_sensor_utils, kZeroOriginalBias);
  EXPECT_CALL(*mock_iio_ec_sensor_utils, GetAvgData(_, kGyroscopeChannels, _))
      .Times(1)
      .WillRepeatedly(WithArg<0>([](GetAvgDataCallback result_callback) {
        std::move(result_callback).Run(kAvgTestData, {});
        return true;
      }));

  auto gyro_calib_utils = std::make_unique<GyroscopeCalibrationUtilsImpl>(
      kLocation, std::move(mock_iio_ec_sensor_utils));

  gyro_calib_utils->Calibrate(
      base::BindRepeating(&GyroscopeCalibrationUtilsImplTest::QueueProgress,
                          base::Unretained(this)),
      base::BindOnce(&GyroscopeCalibrationUtilsImplTest::QueueResult,
                     base::Unretained(this)));

  // Check if sent progresses contain kProgressInit to kProgressComplete.
  EXPECT_EQ(received_progresses_.size(), 5);
  EXPECT_DOUBLE_EQ(received_progresses_.front(), kProgressInit);
  EXPECT_DOUBLE_EQ(received_progresses_.back(), kProgressComplete);
}

TEST_F(GyroscopeCalibrationUtilsImplTest,
       HandleGetAvgDataResult_Inconsistent_Channel_Size) {
  auto mock_iio_ec_sensor_utils =
      std::make_unique<StrictMock<MockIioEcSensorUtils>>(kLocation,
                                                         kSensorName);

  DefineGetSysValuesActions(mock_iio_ec_sensor_utils, kZeroOriginalBias);
  EXPECT_CALL(*mock_iio_ec_sensor_utils, GetAvgData(_, kGyroscopeChannels, _))
      .Times(1)
      .WillRepeatedly(WithArg<0>([](GetAvgDataCallback result_callback) {
        std::move(result_callback).Run({}, {});
        return true;
      }));

  auto gyro_calib_utils = std::make_unique<GyroscopeCalibrationUtilsImpl>(
      kLocation, std::move(mock_iio_ec_sensor_utils));

  gyro_calib_utils->Calibrate(
      base::BindRepeating(&GyroscopeCalibrationUtilsImplTest::QueueProgress,
                          base::Unretained(this)),
      base::BindOnce(&GyroscopeCalibrationUtilsImplTest::QueueResult,
                     base::Unretained(this)));

  // Check if sent progresses contain kProgressInit to kProgressFailed.
  EXPECT_EQ(received_progresses_.size(), 4);
  EXPECT_DOUBLE_EQ(received_progresses_.front(), kProgressInit);
  EXPECT_DOUBLE_EQ(received_progresses_.back(), kProgressFailed);
}

}  // namespace rmad
