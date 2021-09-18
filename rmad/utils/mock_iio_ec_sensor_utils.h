// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_MOCK_IIO_EC_SENSOR_UTILS_H_
#define RMAD_UTILS_MOCK_IIO_EC_SENSOR_UTILS_H_

#include "rmad/utils/iio_ec_sensor_utils.h"

#include <string>
#include <vector>

#include <gmock/gmock.h>

namespace rmad {

class MockIioEcSensorUtils : public IioEcSensorUtils {
 public:
  MockIioEcSensorUtils(const std::string& location, const std::string& name)
      : IioEcSensorUtils(location, name) {}
  ~MockIioEcSensorUtils() override = default;

  MOCK_METHOD(bool,
              GetData,
              (const std::vector<std::string>&, int, std::vector<double>*),
              (override));
  MOCK_METHOD(bool,
              SetSysValues,
              (const std::vector<std::string>&, const std::vector<int>&),
              (override));
};

}  // namespace rmad

#endif  // RMAD_UTILS_MOCK_IIO_EC_SENSOR_UTILS_H_
