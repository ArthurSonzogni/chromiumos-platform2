// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/accelerometer_calibration_utils_impl.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/synchronization/lock.h>

#include "rmad/utils/iio_ec_sensor_utils_impl.h"
#include "rmad/utils/vpd_utils_impl.h"

namespace {

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
    scoped_refptr<VpdUtilsImplThreadSafe> vpd_utils_impl_thread_safe,
    const std::string& location,
    const std::string& name)
    : SensorCalibrationUtils(location, name),
      vpd_utils_impl_thread_safe_(vpd_utils_impl_thread_safe),
      progress_(kProgressInit) {
  iio_ec_sensor_utils_ = std::make_unique<IioEcSensorUtilsImpl>(location, name);
}

bool AccelerometerCalibrationUtilsImpl::Calibrate() {
  std::vector<double> avg_data;
  std::vector<double> variance_data;
  std::vector<double> original_calibbias;
  std::map<std::string, int> calibbias;
  SetProgress(kProgressInit);

  // Before the calibration, we get original calibbias by reading sysfs.
  if (!iio_ec_sensor_utils_->GetSysValues(kAccelerometerCalibbias,
                                          &original_calibbias)) {
    SetProgress(kProgressFailed);
    return false;
  }
  SetProgress(kProgressGetOriginalCalibbias);

  // Due to the uncertainty of the sensor value, we use the average value to
  // calibrate it.
  if (!iio_ec_sensor_utils_->GetAvgData(kAccelerometerChannels, kSamples,
                                        &avg_data, &variance_data)) {
    LOG(ERROR) << location_ << ":" << name_ << ": Failed to accumulate data.";
    SetProgress(kProgressFailed);
    return false;
  }
  SetProgress(kProgressSensorDataReceived);

  if (avg_data.size() != kAccelerometerIdealValues.size()) {
    LOG(ERROR) << location_ << ":" << name_ << ": Get wrong data size "
               << avg_data.size();
    SetProgress(kProgressFailed);
    return false;
  }

  if (variance_data.size() != kAccelerometerIdealValues.size()) {
    LOG(ERROR) << location_ << ":" << name_ << ": Get wrong variance data size "
               << variance_data.size();
    SetProgress(kProgressFailed);
    return false;
  }

  for (int i = 0; i < variance_data.size(); i++) {
    if (variance_data[i] > kVarianceThreshold) {
      LOG(ERROR) << location_ << ":" << name_
                 << ": Data variance=" << variance_data[i]
                 << " too high in channel " << kAccelerometerChannels[i]
                 << ". Expected to be less than " << kVarianceThreshold;
      SetProgress(kProgressFailed);
      return false;
    }
  }

  for (int i = 0; i < avg_data.size(); i++) {
    double offset = kAccelerometerIdealValues[i] - avg_data[i] +
                    original_calibbias[i] * kCalibbias2SensorReading;
    if (std::fabs(offset) > kOffsetThreshold) {
      LOG(ERROR) << location_ << ":" << name_
                 << ": Data is out of range, the accelerometer may be damaged "
                    "or the device setup is incorrect.";
      SetProgress(kProgressFailed);
      return false;
    }
    std::string entry = kCalibbiasPrefix + kAccelerometerChannels[i] + "_" +
                        location_ + kCalibbiasPostfix;
    calibbias[entry] = round(offset / kCalibbias2SensorReading);
  }
  SetProgress(kProgressBiasCalculated);

  // We first write the calibbias data to vpd, and then update the sensor via
  // sysfs accordingly.
  if (!vpd_utils_impl_thread_safe_->SetCalibbias(calibbias)) {
    SetProgress(kProgressFailed);
    return false;
  }
  SetProgress(kProgressBiasWritten);

  return true;
}

bool AccelerometerCalibrationUtilsImpl::GetProgress(double* progress) const {
  CHECK(progress);

  base::AutoLock lock_scope(progress_lock_);
  *progress = progress_;
  return true;
}

void AccelerometerCalibrationUtilsImpl::SetProgress(double progress) {
  base::AutoLock lock_scope(progress_lock_);
  progress_ = progress;
}

}  // namespace rmad
