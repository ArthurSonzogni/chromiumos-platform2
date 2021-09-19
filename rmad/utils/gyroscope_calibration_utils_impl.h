// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_GYROSCOPE_CALIBRATION_UTILS_IMPL_H_
#define RMAD_UTILS_GYROSCOPE_CALIBRATION_UTILS_IMPL_H_

#include "rmad/utils/sensor_calibration_utils.h"

#include <memory>
#include <string>

#include <base/synchronization/lock.h>

#include "rmad/utils/iio_ec_sensor_utils.h"
#include "rmad/utils/vpd_utils.h"

namespace rmad {

class GyroscopeCalibrationUtilsImpl : public SensorCalibrationUtils {
 public:
  GyroscopeCalibrationUtilsImpl(const std::string& location,
                                const std::string& name = "cros-ec-gyro");
  // Used to inject mock |vpd_utils_|, and |iio_ec_sensor_utils_| for testing.
  GyroscopeCalibrationUtilsImpl(
      const std::string& location,
      const std::string& name,
      std::unique_ptr<VpdUtils> vpd_utils,
      std::unique_ptr<IioEcSensorUtils> iio_ec_sensor_utils);
  ~GyroscopeCalibrationUtilsImpl() override = default;

  bool Calibrate() override;
  bool GetProgress(double* progress) const override;

 private:
  void SetProgress(double progress);

  // utils part
  std::unique_ptr<VpdUtils> vpd_utils_;
  std::unique_ptr<IioEcSensorUtils> iio_ec_sensor_utils_;

  // progress part
  double progress_;
  mutable base::Lock progress_lock_;
};

}  // namespace rmad

#endif  // RMAD_UTILS_GYROSCOPE_CALIBRATION_UTILS_IMPL_H_
