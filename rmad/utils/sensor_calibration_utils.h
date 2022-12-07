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
  SensorCalibrationUtils() = default;
  virtual ~SensorCalibrationUtils() = default;

  // Define callback to update calibration progress via doubles (failed: -1.0,
  // in progress: [0.0, 1.0), done: 1.0).
  using CalibrationProgressCallback = base::RepeatingCallback<void(double)>;
  // Define callback to update calibration result via map (keyname in vpd ->
  // calibration bias).
  using CalibrationResultCallback =
      base::OnceCallback<void(const std::map<std::string, int>&)>;

  virtual void Calibrate(CalibrationProgressCallback progress_callback,
                         CalibrationResultCallback result_callback) = 0;
};

}  // namespace rmad

#endif  // RMAD_UTILS_SENSOR_CALIBRATION_UTILS_H_
