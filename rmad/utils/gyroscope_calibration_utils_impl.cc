// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/gyroscope_calibration_utils_impl.h"

#include <cmath>
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

constexpr double kDegree2Radian = M_PI / 180.0;
// The calibbias data unit is 1/1024 dps, and the sensor reading is rad/s.
constexpr double kCalibbias2SensorReading = kDegree2Radian / 1024;

constexpr double kProgressComplete = 1.0;
constexpr double kProgressFailed = -1.0;
constexpr double kProgressInit = 0.0;
constexpr double kProgressGetOriginalCalibbias = 0.2;
constexpr double kProgressSensorDataReceived = 0.7;
constexpr double kProgressBiasCalculated = 0.8;
constexpr double kProgressBiasWritten = kProgressComplete;

constexpr char kCalibbiasPrefix[] = "in_";
constexpr char kCalibbiasPostfix[] = "_calibbias";

const std::vector<std::string> kGyroscopeCalibbias = {"in_anglvel_x_calibbias",
                                                      "in_anglvel_y_calibbias",
                                                      "in_anglvel_z_calibbias"};
const std::vector<std::string> kGyroscopeChannels = {"anglvel_x", "anglvel_y",
                                                     "anglvel_z"};

const std::vector<double> kGyroscopIdealValues = {0, 0, 0};

}  // namespace

namespace rmad {

GyroscopeCalibrationUtilsImpl::GyroscopeCalibrationUtilsImpl(
    scoped_refptr<VpdUtilsImplThreadSafe> vpd_utils_impl_thread_safe,
    const std::string& location,
    const std::string& name)
    : SensorCalibrationUtils(location, name),
      vpd_utils_impl_thread_safe_(vpd_utils_impl_thread_safe),
      progress_(kProgressInit) {
  iio_ec_sensor_utils_ = std::make_unique<IioEcSensorUtilsImpl>(location, name);
}

bool GyroscopeCalibrationUtilsImpl::Calibrate() {
  std::vector<double> avg_data;
  std::vector<double> original_calibbias;
  std::map<std::string, int> calibbias;
  SetProgress(kProgressInit);

  // Before the calibration, we get original calibbias by reading sysfs.
  if (!iio_ec_sensor_utils_->GetSysValues(kGyroscopeCalibbias,
                                          &original_calibbias)) {
    SetProgress(kProgressFailed);
    return false;
  }
  SetProgress(kProgressGetOriginalCalibbias);

  // Due to the uncertainty of the sensor value, we use the average value to
  // calibrate it.
  if (!iio_ec_sensor_utils_->GetAvgData(kGyroscopeChannels, kSamples,
                                        &avg_data)) {
    LOG(ERROR) << location_ << ":" << name_ << ": Failed to accumulate data.";
    SetProgress(kProgressFailed);
    return false;
  }
  SetProgress(kProgressSensorDataReceived);

  // For each axis, we calculate the difference between the ideal values.
  if (avg_data.size() != kGyroscopIdealValues.size()) {
    LOG(ERROR) << location_ << ":" << name_ << ": Get wrong data size "
               << avg_data.size();
    SetProgress(kProgressFailed);
    return false;
  }

  // For each axis, we calculate the difference between the ideal values.
  for (int i = 0; i < kGyroscopIdealValues.size(); i++) {
    double offset = kGyroscopIdealValues[i] - avg_data[i] +
                    original_calibbias[i] * kCalibbias2SensorReading;
    std::string entry = kCalibbiasPrefix + kGyroscopeChannels[i] + "_" +
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

bool GyroscopeCalibrationUtilsImpl::GetProgress(double* progress) const {
  CHECK(progress);

  base::AutoLock lock_scope(progress_lock_);
  *progress = progress_;
  return true;
}

void GyroscopeCalibrationUtilsImpl::SetProgress(double progress) {
  base::AutoLock lock_scope(progress_lock_);
  progress_ = progress;
}

}  // namespace rmad
