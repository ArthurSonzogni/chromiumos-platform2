// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_EXTERNAL_AMBIENT_LIGHT_SENSOR_FACTORY_INTERFACE_H_
#define POWER_MANAGER_POWERD_SYSTEM_EXTERNAL_AMBIENT_LIGHT_SENSOR_FACTORY_INTERFACE_H_

#include <memory>
#include <string>

#include "power_manager/powerd/system/ambient_light_sensor_info.h"
#include "power_manager/powerd/system/ambient_light_sensor_interface.h"

namespace power_manager {
namespace system {

// Interface for creating external ambient light sensors.
class ExternalAmbientLightSensorFactoryInterface {
 public:
  virtual ~ExternalAmbientLightSensorFactoryInterface() {}

  virtual std::unique_ptr<AmbientLightSensorInterface> CreateSensor(
      const AmbientLightSensorInfo& als_info) const = 0;
};

}  // namespace system
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_SYSTEM_EXTERNAL_AMBIENT_LIGHT_SENSOR_FACTORY_INTERFACE_H_
