// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEMS_SETUP_SENSOR_LOCATION_H_
#define MEMS_SETUP_SENSOR_LOCATION_H_

namespace mems_setup {
constexpr char kBaseSensorLocation[] = "base";
constexpr char kLidSensorLocation[] = "lid";

constexpr char kBaseSensorLabel[] = "accel-base";
constexpr char kLidSensorLabel[] = "accel-display";
}  // namespace mems_setup

#endif  // MEMS_SETUP_SENSOR_LOCATION_H_
