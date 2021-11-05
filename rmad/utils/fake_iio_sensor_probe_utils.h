// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_FAKE_IIO_SENSOR_PROBE_UTILS_H_
#define RMAD_UTILS_FAKE_IIO_SENSOR_PROBE_UTILS_H_

#include "rmad/utils/iio_sensor_probe_utils.h"

#include <set>

namespace rmad {
namespace fake {

class FakeIioSensorProbeUtils : public IioSensorProbeUtils {
 public:
  FakeIioSensorProbeUtils() = default;
  ~FakeIioSensorProbeUtils() override = default;

  std::set<RmadComponent> Probe() override;
};

}  // namespace fake
}  // namespace rmad

#endif  // RMAD_UTILS_FAKE_IIO_SENSOR_PROBE_UTILS_H_
