// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_GYROSCOPE_CALIBRATION_UTILS_IMPL_H_
#define RMAD_UTILS_GYROSCOPE_CALIBRATION_UTILS_IMPL_H_

#include "rmad/utils/sensor_calibration_utils.h"

#include <memory>
#include <string>

#include "rmad/utils/iio_ec_sensor_utils.h"

namespace rmad {

class GyroscopeCalibrationUtilsImpl : public SensorCalibrationUtils {
 public:
  explicit GyroscopeCalibrationUtilsImpl(const std::string& location);
  // Used to inject iio_ec_sensor_utils for testing.
  explicit GyroscopeCalibrationUtilsImpl(
      const std::string& location,
      std::unique_ptr<IioEcSensorUtils> iio_ec_sensor_utils);
  ~GyroscopeCalibrationUtilsImpl() override = default;

  void Calibrate(CalibrationProgressCallback progress_callback,
                 CalibrationResultCallback result_callback) override;

 private:
  // utils part
  std::unique_ptr<IioEcSensorUtils> iio_ec_sensor_utils_;
};

}  // namespace rmad

#endif  // RMAD_UTILS_GYROSCOPE_CALIBRATION_UTILS_IMPL_H_
