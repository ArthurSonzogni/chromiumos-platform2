// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_GYROSCOPE_CALIBRATION_UTILS_IMPL_H_
#define RMAD_UTILS_GYROSCOPE_CALIBRATION_UTILS_IMPL_H_

#include "rmad/utils/sensor_calibration_utils.h"

#include <memory>
#include <string>

#include <base/memory/scoped_refptr.h>
#include <base/synchronization/lock.h>

#include "rmad/utils/iio_ec_sensor_utils.h"
#include "rmad/utils/vpd_utils_impl_thread_safe.h"

namespace rmad {

class GyroscopeCalibrationUtilsImpl : public SensorCalibrationUtils {
 public:
  GyroscopeCalibrationUtilsImpl(
      scoped_refptr<VpdUtilsImplThreadSafe> vpd_utils_impl_thread_safe,
      const std::string& location,
      const std::string& name = "cros-ec-gyro");
  ~GyroscopeCalibrationUtilsImpl() override = default;

  bool Calibrate() override;
  bool GetProgress(double* progress) const override;

 private:
  void SetProgress(double progress);

  // utils part
  scoped_refptr<VpdUtilsImplThreadSafe> vpd_utils_impl_thread_safe_;
  std::unique_ptr<IioEcSensorUtils> iio_ec_sensor_utils_;

  // progress part
  double progress_;
  mutable base::Lock progress_lock_;
};

}  // namespace rmad

#endif  // RMAD_UTILS_GYROSCOPE_CALIBRATION_UTILS_IMPL_H_
