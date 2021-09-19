// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/gyroscope_calibration_utils_impl.h"

#include <cmath>
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
// The calibbias data is converted to 1/1024dps unit,
// and the sensor reading is rad/s.
constexpr double kCalibbiasDataScale = kDegree2Radian / 1024;

constexpr double kProgressComplete = 1.0;
constexpr double kProgressFailed = -1.0;
constexpr double kProgressInit = 0.0;
constexpr double kProgressSensorReset = 0.15;
constexpr double kProgressSensorDataReceived = 0.6;
constexpr double kProgressBiasCalculated = 0.65;
constexpr double kProgressBiasWritten = 0.85;
constexpr double kProgressBiasSet = kProgressComplete;

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
    const std::string& location, const std::string& name)
    : SensorCalibrationUtils(location, name), progress_(kProgressInit) {
  vpd_utils_ = std::make_unique<VpdUtilsImpl>();
  iio_ec_sensor_utils_ = std::make_unique<IioEcSensorUtilsImpl>(location, name);
}

GyroscopeCalibrationUtilsImpl::GyroscopeCalibrationUtilsImpl(
    const std::string& location,
    const std::string& name,
    std::unique_ptr<VpdUtils> vpd_utils,
    std::unique_ptr<IioEcSensorUtils> iio_ec_sensor_utils)
    : SensorCalibrationUtils(location, name),
      vpd_utils_(std::move(vpd_utils)),
      iio_ec_sensor_utils_(std::move(iio_ec_sensor_utils)),
      progress_(kProgressInit) {}

bool GyroscopeCalibrationUtilsImpl::Calibrate() {
  std::vector<double> avg_data;
  std::vector<int> scaled_data;
  SetProgress(kProgressInit);

  // Before starting the calibration, we clear the sensor state by writing 0 to
  // sysfs.
  if (!iio_ec_sensor_utils_->SetSysValues(kGyroscopeCalibbias, {0, 0, 0})) {
    SetProgress(kProgressFailed);
    return false;
  }
  SetProgress(kProgressSensorReset);

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
  scaled_data.resize(kGyroscopIdealValues.size());

  // For each axis, we calculate the difference between the ideal values.
  for (int i = 0; i < kGyroscopIdealValues.size(); i++) {
    scaled_data[i] =
        (kGyroscopIdealValues[i] - avg_data[i]) / kCalibbiasDataScale;
  }
  SetProgress(kProgressBiasCalculated);

  // We first write the calibbias data to vpd, and then update the sensor via
  // sysfs accordingly.
  std::vector<std::string> calibbias_entries;
  for (auto channel : kGyroscopeChannels) {
    calibbias_entries.push_back(kCalibbiasPrefix + channel + "_" + location_ +
                                kCalibbiasPostfix);
  }
  if (!vpd_utils_->SetCalibbias(calibbias_entries, scaled_data)) {
    SetProgress(kProgressFailed);
    return false;
  }
  SetProgress(kProgressBiasWritten);

  if (!iio_ec_sensor_utils_->SetSysValues(kGyroscopeCalibbias, scaled_data)) {
    SetProgress(kProgressFailed);
    return false;
  }
  SetProgress(kProgressBiasSet);

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
