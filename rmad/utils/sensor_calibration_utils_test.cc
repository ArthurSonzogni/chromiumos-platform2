// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/sensor_calibration_utils_impl.h"

#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
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

constexpr double kGravity = 9.80665;
constexpr double kDegree2Radian = M_PI / 180.0;

const std::vector<double> kAccelAvgTestData = {0, 0, kGravity};
const std::vector<double> kAccelInvalidAvgTestData = {0, 0, 0};
const std::vector<double> kGyroAvgTestData = {0, 0, 0};
const std::vector<double> kOriginalBias = {123, 456, 789};
const std::vector<double> kZeroOriginalBias = {0, 0, 0};
const std::vector<double> kValidVariance = {1, 2, 3};
const std::vector<double> kInvalidVariance = {1, 2, 30};

constexpr double kProgressFailed = -1.0;
constexpr double kProgressInit = 0.0;
constexpr double kProgressGetOriginalCalibbias = 0.2;
constexpr double kProgressComplete = 1.0;

const std::set<std::string> kValidSensorNames = {
    rmad::SensorCalibrationUtilsImpl::kGyroSensorName,
    rmad::SensorCalibrationUtilsImpl::kAccelSensorName};

const std::map<std::string, std::vector<std::string>> kSensorChannels = {
    {rmad::SensorCalibrationUtilsImpl::kGyroSensorName,
     {"anglvel_x", "anglvel_y", "anglvel_z"}},
    {rmad::SensorCalibrationUtilsImpl::kAccelSensorName,
     {"accel_x", "accel_y", "accel_z"}}};

const std::map<std::string, std::vector<std::string>> kSensorCalibbias = {
    {rmad::SensorCalibrationUtilsImpl::kGyroSensorName,
     {"in_anglvel_x_calibbias", "in_anglvel_y_calibbias",
      "in_anglvel_z_calibbias"}},
    {rmad::SensorCalibrationUtilsImpl::kAccelSensorName,
     {"in_accel_x_calibbias", "in_accel_y_calibbias", "in_accel_z_calibbias"}}};

const std::map<std::string, std::vector<double>> kSensorIdealValues = {
    {rmad::SensorCalibrationUtilsImpl::kGyroSensorName, {0, 0, 0}},
    {rmad::SensorCalibrationUtilsImpl::kAccelSensorName, {0, 0, kGravity}}};

// The calibbias data unit in gyroscope is 1/1024 dps, and the sensor reading is
// rad/s. The calibbias data unit in accelerometer is G/1024, and the sensor
// reading unit is m/s^2.
const std::map<std::string, double> kCalibbias2SensorReading = {
    {rmad::SensorCalibrationUtilsImpl::kGyroSensorName, kDegree2Radian / 1024},
    {rmad::SensorCalibrationUtilsImpl::kAccelSensorName, kGravity / 1024.0}};

}  // namespace

namespace rmad {

class SensorCalibrationUtilsImplTest : public testing::Test {
 public:
  SensorCalibrationUtilsImplTest() = default;

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

    EXPECT_CALL(*mock_iio_ec_sensor_utils,
                GetSysValues(kSensorCalibbias.at(
                                 SensorCalibrationUtilsImpl::kGyroSensorName),
                             _))
        .WillRepeatedly(WithArg<1>(
            [sys_values, &mock_output_helper](std::vector<double>* output) {
              return mock_output_helper(sys_values, output);
            }));

    EXPECT_CALL(*mock_iio_ec_sensor_utils,
                GetSysValues(kSensorCalibbias.at(
                                 SensorCalibrationUtilsImpl::kAccelSensorName),
                             _))
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

TEST_F(SensorCalibrationUtilsImplTest, Calibrate_WithoutOriginalBias_Success) {
  auto mock_iio_ec_sensor_utils =
      std::make_unique<StrictMock<MockIioEcSensorUtils>>(
          kLocation, SensorCalibrationUtilsImpl::kGyroSensorName);

  DefineGetSysValuesActions(mock_iio_ec_sensor_utils, kZeroOriginalBias);

  EXPECT_CALL(
      *mock_iio_ec_sensor_utils,
      GetAvgData(
          _, kSensorChannels.at(SensorCalibrationUtilsImpl::kGyroSensorName),
          _))
      .Times(1)
      .WillRepeatedly(Return(true));
  EXPECT_CALL(
      *mock_iio_ec_sensor_utils,
      GetAvgData(
          _, kSensorChannels.at(SensorCalibrationUtilsImpl::kAccelSensorName),
          _))
      .Times(0);

  auto calib_utils = std::make_unique<SensorCalibrationUtilsImpl>(
      kLocation, SensorCalibrationUtilsImpl::kGyroSensorName,
      std::move(mock_iio_ec_sensor_utils));

  calib_utils->Calibrate(
      base::BindRepeating(&SensorCalibrationUtilsImplTest::QueueProgress,
                          base::Unretained(this)),
      base::BindOnce(&SensorCalibrationUtilsImplTest::QueueResult,
                     base::Unretained(this)));

  // Check if sent progresses contain kProgressInit to kProgressComplete.
  EXPECT_EQ(received_progresses_.size(), 2);
  EXPECT_DOUBLE_EQ(received_progresses_.front(), kProgressInit);
  EXPECT_DOUBLE_EQ(received_progresses_.back(), kProgressGetOriginalCalibbias);
}

TEST_F(SensorCalibrationUtilsImplTest, Calibrate_WithOriginalBias_Success) {
  auto mock_iio_ec_sensor_utils =
      std::make_unique<StrictMock<MockIioEcSensorUtils>>(
          kLocation, SensorCalibrationUtilsImpl::kGyroSensorName);

  DefineGetSysValuesActions(mock_iio_ec_sensor_utils, kOriginalBias);

  EXPECT_CALL(
      *mock_iio_ec_sensor_utils,
      GetAvgData(
          _, kSensorChannels.at(SensorCalibrationUtilsImpl::kGyroSensorName),
          _))
      .Times(1)
      .WillRepeatedly(Return(true));
  EXPECT_CALL(
      *mock_iio_ec_sensor_utils,
      GetAvgData(
          _, kSensorChannels.at(SensorCalibrationUtilsImpl::kAccelSensorName),
          _))
      .Times(0);

  auto calib_utils = std::make_unique<SensorCalibrationUtilsImpl>(
      kLocation, SensorCalibrationUtilsImpl::kGyroSensorName,
      std::move(mock_iio_ec_sensor_utils));

  calib_utils->Calibrate(
      base::BindRepeating(&SensorCalibrationUtilsImplTest::QueueProgress,
                          base::Unretained(this)),
      base::BindOnce(&SensorCalibrationUtilsImplTest::QueueResult,
                     base::Unretained(this)));

  // Check if sent progresses contain kProgressInit to
  // kProgressGetOriginalCalibbias.
  EXPECT_EQ(received_progresses_.size(), 2);
  EXPECT_DOUBLE_EQ(received_progresses_.front(), kProgressInit);
  EXPECT_DOUBLE_EQ(received_progresses_.back(), kProgressGetOriginalCalibbias);
}

TEST_F(SensorCalibrationUtilsImplTest, Calibrate_NoAvgData_Failed) {
  auto mock_iio_ec_sensor_utils =
      std::make_unique<StrictMock<MockIioEcSensorUtils>>(
          kLocation, SensorCalibrationUtilsImpl::kGyroSensorName);

  DefineGetSysValuesActions(mock_iio_ec_sensor_utils, kZeroOriginalBias);
  EXPECT_CALL(
      *mock_iio_ec_sensor_utils,
      GetAvgData(
          _, kSensorChannels.at(SensorCalibrationUtilsImpl::kGyroSensorName),
          _))
      .Times(1)
      .WillRepeatedly(Return(false));
  EXPECT_CALL(
      *mock_iio_ec_sensor_utils,
      GetAvgData(
          _, kSensorChannels.at(SensorCalibrationUtilsImpl::kAccelSensorName),
          _))
      .Times(0);

  auto calib_utils = std::make_unique<SensorCalibrationUtilsImpl>(
      kLocation, SensorCalibrationUtilsImpl::kGyroSensorName,
      std::move(mock_iio_ec_sensor_utils));

  calib_utils->Calibrate(
      base::BindRepeating(&SensorCalibrationUtilsImplTest::QueueProgress,
                          base::Unretained(this)),
      base::BindOnce(&SensorCalibrationUtilsImplTest::QueueResult,
                     base::Unretained(this)));

  // Check if sent progresses contain kProgressInit,
  // kProgressGetOriginalCalibbias, and kProgressFailed.
  EXPECT_EQ(received_progresses_.size(), 3);
  EXPECT_DOUBLE_EQ(received_progresses_.front(), kProgressInit);
  EXPECT_DOUBLE_EQ(received_progresses_.back(), kProgressFailed);
}

TEST_F(SensorCalibrationUtilsImplTest, Calibrate_NoSysValues_Failed) {
  auto mock_iio_ec_sensor_utils =
      std::make_unique<StrictMock<MockIioEcSensorUtils>>(
          kLocation, SensorCalibrationUtilsImpl::kGyroSensorName);

  DefineGetSysValuesActions(mock_iio_ec_sensor_utils, {});
  EXPECT_CALL(
      *mock_iio_ec_sensor_utils,
      GetAvgData(
          _, kSensorChannels.at(SensorCalibrationUtilsImpl::kGyroSensorName),
          _))
      .Times(0);

  auto calib_utils = std::make_unique<SensorCalibrationUtilsImpl>(
      kLocation, SensorCalibrationUtilsImpl::kGyroSensorName,
      std::move(mock_iio_ec_sensor_utils));

  calib_utils->Calibrate(
      base::BindRepeating(&SensorCalibrationUtilsImplTest::QueueProgress,
                          base::Unretained(this)),
      base::BindOnce(&SensorCalibrationUtilsImplTest::QueueResult,
                     base::Unretained(this)));

  // Check if sent progresses contain kProgressInit and kProgressFailed.
  EXPECT_EQ(received_progresses_.size(), 2);
  EXPECT_DOUBLE_EQ(received_progresses_.front(), kProgressInit);
  EXPECT_DOUBLE_EQ(received_progresses_.back(), kProgressFailed);
}

TEST_F(SensorCalibrationUtilsImplTest, HandleGetAvgDataResult_Success) {
  auto mock_iio_ec_sensor_utils =
      std::make_unique<StrictMock<MockIioEcSensorUtils>>(
          kLocation, SensorCalibrationUtilsImpl::kGyroSensorName);

  DefineGetSysValuesActions(mock_iio_ec_sensor_utils, kZeroOriginalBias);
  EXPECT_CALL(
      *mock_iio_ec_sensor_utils,
      GetAvgData(
          _, kSensorChannels.at(SensorCalibrationUtilsImpl::kGyroSensorName),
          _))
      .Times(1)
      .WillRepeatedly(WithArg<0>([](GetAvgDataCallback result_callback) {
        std::move(result_callback).Run(kGyroAvgTestData, kValidVariance);
        return true;
      }));

  auto calib_utils = std::make_unique<SensorCalibrationUtilsImpl>(
      kLocation, SensorCalibrationUtilsImpl::kGyroSensorName,
      std::move(mock_iio_ec_sensor_utils));

  calib_utils->Calibrate(
      base::BindRepeating(&SensorCalibrationUtilsImplTest::QueueProgress,
                          base::Unretained(this)),
      base::BindOnce(&SensorCalibrationUtilsImplTest::QueueResult,
                     base::Unretained(this)));

  // Check if sent progresses contain kProgressInit to kProgressComplete.
  EXPECT_EQ(received_progresses_.size(), 5);
  EXPECT_DOUBLE_EQ(received_progresses_.front(), kProgressInit);
  EXPECT_DOUBLE_EQ(received_progresses_.back(), kProgressComplete);
}

TEST_F(SensorCalibrationUtilsImplTest,
       HandleGetAvgDataResult_Inconsistent_Channel_Size) {
  auto mock_iio_ec_sensor_utils =
      std::make_unique<StrictMock<MockIioEcSensorUtils>>(
          kLocation, SensorCalibrationUtilsImpl::kGyroSensorName);

  DefineGetSysValuesActions(mock_iio_ec_sensor_utils, kZeroOriginalBias);
  EXPECT_CALL(
      *mock_iio_ec_sensor_utils,
      GetAvgData(
          _, kSensorChannels.at(SensorCalibrationUtilsImpl::kGyroSensorName),
          _))
      .Times(1)
      .WillRepeatedly(WithArg<0>([](GetAvgDataCallback result_callback) {
        std::move(result_callback).Run({}, {});
        return true;
      }));

  auto calib_utils = std::make_unique<SensorCalibrationUtilsImpl>(
      kLocation, SensorCalibrationUtilsImpl::kGyroSensorName,
      std::move(mock_iio_ec_sensor_utils));

  calib_utils->Calibrate(
      base::BindRepeating(&SensorCalibrationUtilsImplTest::QueueProgress,
                          base::Unretained(this)),
      base::BindOnce(&SensorCalibrationUtilsImplTest::QueueResult,
                     base::Unretained(this)));

  // Check if sent progresses contain kProgressInit to kProgressFailed.
  EXPECT_EQ(received_progresses_.size(), 4);
  EXPECT_DOUBLE_EQ(received_progresses_.front(), kProgressInit);
  EXPECT_DOUBLE_EQ(received_progresses_.back(), kProgressFailed);
}

TEST_F(SensorCalibrationUtilsImplTest, Calibrate_Check_Variance_Success) {
  auto mock_iio_ec_sensor_utils =
      std::make_unique<StrictMock<MockIioEcSensorUtils>>(
          kLocation, SensorCalibrationUtilsImpl::kAccelSensorName);

  DefineGetSysValuesActions(mock_iio_ec_sensor_utils, kZeroOriginalBias);
  EXPECT_CALL(
      *mock_iio_ec_sensor_utils,
      GetAvgData(
          _, kSensorChannels.at(SensorCalibrationUtilsImpl::kAccelSensorName),
          _))
      .Times(1)
      .WillRepeatedly(WithArg<0>([](GetAvgDataCallback result_callback) {
        std::move(result_callback).Run(kAccelAvgTestData, kValidVariance);
        return true;
      }));

  auto calib_utils = std::make_unique<SensorCalibrationUtilsImpl>(
      kLocation, SensorCalibrationUtilsImpl::kAccelSensorName,
      std::move(mock_iio_ec_sensor_utils));

  calib_utils->Calibrate(
      base::BindRepeating(&SensorCalibrationUtilsImplTest::QueueProgress,
                          base::Unretained(this)),
      base::BindOnce(&SensorCalibrationUtilsImplTest::QueueResult,
                     base::Unretained(this)));

  // Check if sent progresses contain kProgressInit to kProgressComplete.
  EXPECT_EQ(received_progresses_.size(), 5);
  EXPECT_DOUBLE_EQ(received_progresses_.front(), kProgressInit);
  EXPECT_DOUBLE_EQ(received_progresses_.back(), kProgressComplete);
}

TEST_F(SensorCalibrationUtilsImplTest, Calibrate_Check_Variance_Wrong_Size) {
  auto mock_iio_ec_sensor_utils =
      std::make_unique<StrictMock<MockIioEcSensorUtils>>(
          kLocation, SensorCalibrationUtilsImpl::kAccelSensorName);

  DefineGetSysValuesActions(mock_iio_ec_sensor_utils, kZeroOriginalBias);
  EXPECT_CALL(
      *mock_iio_ec_sensor_utils,
      GetAvgData(
          _, kSensorChannels.at(SensorCalibrationUtilsImpl::kAccelSensorName),
          _))
      .Times(1)
      .WillRepeatedly(WithArg<0>([](GetAvgDataCallback result_callback) {
        std::move(result_callback).Run(kAccelAvgTestData, {});
        return true;
      }));

  auto calib_utils = std::make_unique<SensorCalibrationUtilsImpl>(
      kLocation, SensorCalibrationUtilsImpl::kAccelSensorName,
      std::move(mock_iio_ec_sensor_utils));

  calib_utils->Calibrate(
      base::BindRepeating(&SensorCalibrationUtilsImplTest::QueueProgress,
                          base::Unretained(this)),
      base::BindOnce(&SensorCalibrationUtilsImplTest::QueueResult,
                     base::Unretained(this)));

  // Check if sent progresses contain kProgressInit to kProgressComplete.
  EXPECT_EQ(received_progresses_.size(), 4);
  EXPECT_DOUBLE_EQ(received_progresses_.front(), kProgressInit);
  EXPECT_DOUBLE_EQ(received_progresses_.back(), kProgressFailed);
}

TEST_F(SensorCalibrationUtilsImplTest, Calibrate_Check_Variance_Too_High) {
  auto mock_iio_ec_sensor_utils =
      std::make_unique<StrictMock<MockIioEcSensorUtils>>(
          kLocation, SensorCalibrationUtilsImpl::kAccelSensorName);

  DefineGetSysValuesActions(mock_iio_ec_sensor_utils, kZeroOriginalBias);
  EXPECT_CALL(
      *mock_iio_ec_sensor_utils,
      GetAvgData(
          _, kSensorChannels.at(SensorCalibrationUtilsImpl::kAccelSensorName),
          _))
      .Times(1)
      .WillRepeatedly(WithArg<0>([](GetAvgDataCallback result_callback) {
        std::move(result_callback).Run(kAccelAvgTestData, kInvalidVariance);
        return true;
      }));

  auto calib_utils = std::make_unique<SensorCalibrationUtilsImpl>(
      kLocation, SensorCalibrationUtilsImpl::kAccelSensorName,
      std::move(mock_iio_ec_sensor_utils));

  calib_utils->Calibrate(
      base::BindRepeating(&SensorCalibrationUtilsImplTest::QueueProgress,
                          base::Unretained(this)),
      base::BindOnce(&SensorCalibrationUtilsImplTest::QueueResult,
                     base::Unretained(this)));

  // Check if sent progresses contain kProgressInit to kProgressComplete.
  EXPECT_EQ(received_progresses_.size(), 4);
  EXPECT_DOUBLE_EQ(received_progresses_.front(), kProgressInit);
  EXPECT_DOUBLE_EQ(received_progresses_.back(), kProgressFailed);
}

TEST_F(SensorCalibrationUtilsImplTest, Calibrate_Check_Offset_Too_High) {
  auto mock_iio_ec_sensor_utils =
      std::make_unique<StrictMock<MockIioEcSensorUtils>>(
          kLocation, SensorCalibrationUtilsImpl::kAccelSensorName);

  DefineGetSysValuesActions(mock_iio_ec_sensor_utils, kZeroOriginalBias);
  EXPECT_CALL(
      *mock_iio_ec_sensor_utils,
      GetAvgData(
          _, kSensorChannels.at(SensorCalibrationUtilsImpl::kAccelSensorName),
          _))
      .Times(1)
      .WillRepeatedly(WithArg<0>([](GetAvgDataCallback result_callback) {
        std::move(result_callback)
            .Run(kAccelInvalidAvgTestData, kValidVariance);
        return true;
      }));

  auto calib_utils = std::make_unique<SensorCalibrationUtilsImpl>(
      kLocation, SensorCalibrationUtilsImpl::kAccelSensorName,
      std::move(mock_iio_ec_sensor_utils));

  calib_utils->Calibrate(
      base::BindRepeating(&SensorCalibrationUtilsImplTest::QueueProgress,
                          base::Unretained(this)),
      base::BindOnce(&SensorCalibrationUtilsImplTest::QueueResult,
                     base::Unretained(this)));

  // Check if sent progresses contain kProgressInit to kProgressComplete.
  EXPECT_EQ(received_progresses_.size(), 4);
  EXPECT_DOUBLE_EQ(received_progresses_.front(), kProgressInit);
  EXPECT_DOUBLE_EQ(received_progresses_.back(), kProgressFailed);
}

}  // namespace rmad
