// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_MOCK_SENSOR_CALIBRATION_UTILS_H_
#define RMAD_UTILS_MOCK_SENSOR_CALIBRATION_UTILS_H_

#include "rmad/utils/sensor_calibration_utils.h"

#include <string>

#include <gmock/gmock.h>

namespace rmad {

class MockSensorCalibrationUtils : public SensorCalibrationUtils {
 public:
  MockSensorCalibrationUtils(const std::string& location,
                             const std::string& name)
      : SensorCalibrationUtils(location, name) {}
  ~MockSensorCalibrationUtils() override = default;

  MOCK_METHOD(bool, Calibrate, (), (override));
  MOCK_METHOD(bool, GetProgress, (double*), (const, override));
};

}  // namespace rmad

#endif  // RMAD_UTILS_MOCK_SENSOR_CALIBRATION_UTILS_H_
