// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_SENSOR_CALIBRATION_UTILS_H_
#define RMAD_UTILS_SENSOR_CALIBRATION_UTILS_H_

#include <map>
#include <string>

#include <base/callback.h>

namespace rmad {

class SensorCalibrationUtils {
 public:
  explicit SensorCalibrationUtils(const std::string& location,
                                  const std::string& name)
      : location_(location), name_(name) {}
  virtual ~SensorCalibrationUtils() = default;

  // Define callback to update calibration progress via doubles (failed: -1.0,
  // in progress: [0.0, 1.0), done: 1.0).
  using CalibrationProgressCallback = base::RepeatingCallback<void(double)>;
  // Define callback to update calibration result via map (keyname in vpd ->
  // calibration bias).
  using CalibrationResultCallback =
      base::OnceCallback<void(const std::map<std::string, int>&)>;

  // Get the location of the ec sensor, which can be "base" or "lid".
  const std::string& GetLocation() const { return location_; }

  // Get sensor name of the ec sensor.
  const std::string& GetName() const { return name_; }

  virtual void Calibrate(CalibrationProgressCallback progress_callback,
                         CalibrationResultCallback result_callback) = 0;

 protected:
  // For each sensor, we can identify it by its location (base or lid)
  // and name (cros-ec-accel or cros-ec-gyro)
  std::string location_;
  std::string name_;
};

}  // namespace rmad

#endif  // RMAD_UTILS_SENSOR_CALIBRATION_UTILS_H_
