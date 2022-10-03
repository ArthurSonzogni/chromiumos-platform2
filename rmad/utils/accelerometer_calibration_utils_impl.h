// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_ACCELEROMETER_CALIBRATION_UTILS_IMPL_H_
#define RMAD_UTILS_ACCELEROMETER_CALIBRATION_UTILS_IMPL_H_

#include "rmad/utils/sensor_calibration_utils.h"

#include <memory>
#include <string>

#include "rmad/utils/iio_ec_sensor_utils.h"

namespace rmad {

class AccelerometerCalibrationUtilsImpl : public SensorCalibrationUtils {
 public:
  explicit AccelerometerCalibrationUtilsImpl(
      const std::string& location,
      const std::string& name = "cros-ec-accel");
  ~AccelerometerCalibrationUtilsImpl() override = default;

  void Calibrate(CalibrationProgressCallback progress_callback,
                 CalibrationResultCallback result_callback) override;

 private:
  // utils part
  std::unique_ptr<IioEcSensorUtils> iio_ec_sensor_utils_;
};

}  // namespace rmad

#endif  // RMAD_UTILS_ACCELEROMETER_CALIBRATION_UTILS_IMPL_H_
