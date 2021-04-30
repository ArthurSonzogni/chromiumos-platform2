// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_AMBIENT_LIGHT_SENSOR_INFO_H_
#define POWER_MANAGER_POWERD_SYSTEM_AMBIENT_LIGHT_SENSOR_INFO_H_

#include <string>

#include <base/files/file_path.h>

namespace power_manager {
namespace system {

// Information about a connected ambient light sensor.
struct AmbientLightSensorInfo {
 public:
  bool operator<(const AmbientLightSensorInfo& rhs) const;
  bool operator==(const AmbientLightSensorInfo& o) const;

  // Path to the directory in /sys representing the IIO device for the ambient
  // light sensor.
  base::FilePath iio_path;

  // IIO device name.
  std::string device;
};

}  // namespace system
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_SYSTEM_AMBIENT_LIGHT_SENSOR_INFO_H_
