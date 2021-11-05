// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/fake_iio_sensor_probe_utils.h"

#include <set>

#include "rmad/proto_bindings/rmad.pb.h"

namespace rmad {
namespace fake {

std::set<RmadComponent> FakeIioSensorProbeUtils::Probe() {
  return {RMAD_COMPONENT_BASE_ACCELEROMETER, RMAD_COMPONENT_LID_ACCELEROMETER,
          RMAD_COMPONENT_BASE_GYROSCOPE, RMAD_COMPONENT_LID_GYROSCOPE};
}

}  // namespace fake
}  // namespace rmad
