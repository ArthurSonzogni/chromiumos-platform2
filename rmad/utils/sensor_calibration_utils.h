// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_SENSOR_CALIBRATION_UTILS_H_
#define RMAD_UTILS_SENSOR_CALIBRATION_UTILS_H_

#include <string>

namespace rmad {

class SensorCalibrationUtils {
 public:
  SensorCalibrationUtils(const std::string& location, const std::string& name)
      : location_(location), name_(name) {}
  virtual ~SensorCalibrationUtils() = default;

  // Get the location of the ec sensor, which can be "base" or "lid".
  const std::string& GetLocation() const { return location_; }

  // Get sensor name of the ec sensor.
  const std::string& GetName() const { return name_; }

  virtual bool Calibrate() = 0;
  virtual bool GetProgress(double* progress) const = 0;

 protected:
  // For each sensor, we can identify it by its location (base or lid)
  // and name (cros-ec-accel or cros-ec-gyro)
  std::string location_;
  std::string name_;
};

}  // namespace rmad

#endif  // RMAD_UTILS_SENSOR_CALIBRATION_UTILS_H_
