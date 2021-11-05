// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_FAKE_SENSOR_CALIBRATION_UTILS_H_
#define RMAD_UTILS_FAKE_SENSOR_CALIBRATION_UTILS_H_

#include "rmad/utils/sensor_calibration_utils.h"

namespace rmad {
namespace fake {

class FakeSensorCalibrationUtils : public SensorCalibrationUtils {
 public:
  FakeSensorCalibrationUtils()
      : SensorCalibrationUtils("fake_location", "fake_name") {}
  ~FakeSensorCalibrationUtils() override = default;

  bool Calibrate() override { return true; }
  bool GetProgress(double* progress) const override {
    *progress = 1.0;
    return true;
  }
};

}  // namespace fake
}  // namespace rmad

#endif  // RMAD_UTILS_FAKE_SENSOR_CALIBRATION_UTILS_H_
