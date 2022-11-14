// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_GYROSCOPE_CALIBRATION_UTILS_IMPL_H_
#define RMAD_UTILS_GYROSCOPE_CALIBRATION_UTILS_IMPL_H_

#include <memory>
#include <string>
#include <vector>

#include "rmad/utils/iio_ec_sensor_utils.h"
#include "rmad/utils/mojo_service_utils.h"
#include "rmad/utils/sensor_calibration_utils.h"

namespace rmad {

class GyroscopeCalibrationUtilsImpl : public SensorCalibrationUtils {
 public:
  explicit GyroscopeCalibrationUtilsImpl(
      scoped_refptr<MojoServiceUtils> mojo_service,
      const std::string& location);
  // Used to inject iio_ec_sensor_utils for testing.
  explicit GyroscopeCalibrationUtilsImpl(
      const std::string& location,
      std::unique_ptr<IioEcSensorUtils> iio_ec_sensor_utils);
  ~GyroscopeCalibrationUtilsImpl() override = default;

  void Calibrate(CalibrationProgressCallback progress_callback,
                 CalibrationResultCallback result_callback) override;

 private:
  void HandleGetAvgDataResult(CalibrationProgressCallback progress_callback,
                              CalibrationResultCallback result_callback,
                              const std::vector<double>& original_calibbias,
                              const std::vector<double>& avg_data,
                              const std::vector<double>& variance_data);
  // utils part.
  std::unique_ptr<IioEcSensorUtils> iio_ec_sensor_utils_;
};

}  // namespace rmad

#endif  // RMAD_UTILS_GYROSCOPE_CALIBRATION_UTILS_IMPL_H_
