// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/accelerometer_calibration_utils_impl.h"

#include <cmath>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <base/logging.h>

#include "rmad/utils/iio_ec_sensor_utils_impl.h"

namespace {

constexpr char kSensorName[] = "cros-ec-accel";

constexpr int kSamples = 100;

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
constexpr double kProgressGetOriginalCalibbias = 0.2;
constexpr double kProgressSensorDataReceived = 0.7;
constexpr double kProgressBiasCalculated = 0.8;
constexpr double kProgressBiasWritten = kProgressComplete;

constexpr char kCalibbiasPrefix[] = "in_";
constexpr char kCalibbiasPostfix[] = "_calibbias";

const std::vector<std::string> kAccelerometerCalibbias = {
    "in_accel_x_calibbias", "in_accel_y_calibbias", "in_accel_z_calibbias"};
const std::vector<std::string> kAccelerometerChannels = {"accel_x", "accel_y",
                                                         "accel_z"};

const std::vector<double> kAccelerometerIdealValues = {0, 0, kGravity};

}  // namespace

namespace rmad {

AccelerometerCalibrationUtilsImpl::AccelerometerCalibrationUtilsImpl(
    scoped_refptr<MojoServiceUtils> mojo_service, const std::string& location)
    : SensorCalibrationUtils(location, kSensorName) {
  iio_ec_sensor_utils_ = std::make_unique<IioEcSensorUtilsImpl>(
      mojo_service, location, kSensorName);
}

AccelerometerCalibrationUtilsImpl::AccelerometerCalibrationUtilsImpl(
    const std::string& location,
    std::unique_ptr<IioEcSensorUtils> iio_ec_sensor_utils)
    : SensorCalibrationUtils(location, kSensorName),
      iio_ec_sensor_utils_(std::move(iio_ec_sensor_utils)) {}

void AccelerometerCalibrationUtilsImpl::Calibrate(
    CalibrationProgressCallback progress_callback,
    CalibrationResultCallback result_callback) {
  CHECK(iio_ec_sensor_utils_);
  CHECK_EQ(GetLocation(), iio_ec_sensor_utils_->GetLocation());
  CHECK_EQ(GetName(), iio_ec_sensor_utils_->GetName());

  std::vector<double> original_calibbias;
  progress_callback.Run(kProgressInit);

  // Before the calibration, we get original calibbias by reading sysfs.
  if (!iio_ec_sensor_utils_->GetSysValues(kAccelerometerCalibbias,
                                          &original_calibbias)) {
    progress_callback.Run(kProgressFailed);
    return;
  }
  progress_callback.Run(kProgressGetOriginalCalibbias);

  // Due to the uncertainty of the sensor value, we use the average value to
  // calibrate it.
  if (!iio_ec_sensor_utils_->GetAvgData(
          base::BindOnce(
              &AccelerometerCalibrationUtilsImpl::HandleGetAvgDataResult,
              base::Unretained(this), progress_callback,
              std::move(result_callback), original_calibbias),
          kAccelerometerChannels, kSamples)) {
    LOG(ERROR) << location_ << ":" << name_ << ": Failed to accumulate data.";
    progress_callback.Run(kProgressFailed);
    return;
  }
}

void AccelerometerCalibrationUtilsImpl::HandleGetAvgDataResult(
    CalibrationProgressCallback progress_callback,
    CalibrationResultCallback result_callback,
    const std::vector<double>& original_calibbias,
    const std::vector<double>& avg_data,
    const std::vector<double>& variance_data) {
  std::map<std::string, int> calibbias;

  progress_callback.Run(kProgressSensorDataReceived);

  if (avg_data.size() != kAccelerometerIdealValues.size()) {
    LOG(ERROR) << location_ << ":" << name_ << ": Get wrong data size "
               << avg_data.size();
    progress_callback.Run(kProgressFailed);
    return;
  }

  if (variance_data.size() != kAccelerometerIdealValues.size()) {
    LOG(ERROR) << location_ << ":" << name_ << ": Get wrong variance data size "
               << variance_data.size();
    progress_callback.Run(kProgressFailed);
    return;
  }

  for (int i = 0; i < variance_data.size(); i++) {
    if (variance_data[i] > kVarianceThreshold) {
      LOG(ERROR) << location_ << ":" << name_
                 << ": Data variance=" << variance_data[i]
                 << " too high in channel " << kAccelerometerChannels[i]
                 << ". Expected to be less than " << kVarianceThreshold;
      progress_callback.Run(kProgressFailed);
      return;
    }
  }

  for (int i = 0; i < avg_data.size(); i++) {
    double offset = kAccelerometerIdealValues[i] - avg_data[i] +
                    original_calibbias[i] * kCalibbias2SensorReading;
    if (std::fabs(offset) > kOffsetThreshold) {
      LOG(ERROR) << location_ << ":" << name_
                 << ": Data is out of range, the accelerometer may be damaged "
                    "or the device setup is incorrect.";
      progress_callback.Run(kProgressFailed);
      return;
    }
    std::string entry = kCalibbiasPrefix + kAccelerometerChannels[i] + "_" +
                        location_ + kCalibbiasPostfix;
    calibbias[entry] = round(offset / kCalibbias2SensorReading);
  }
  progress_callback.Run(kProgressBiasCalculated);

  std::move(result_callback).Run(calibbias);
  progress_callback.Run(kProgressBiasWritten);
}

}  // namespace rmad
