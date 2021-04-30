// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_EXTERNAL_AMBIENT_LIGHT_SENSOR_FACTORY_FILE_H_
#define POWER_MANAGER_POWERD_SYSTEM_EXTERNAL_AMBIENT_LIGHT_SENSOR_FACTORY_FILE_H_

#include <memory>
#include <string>

#include "power_manager/powerd/system/external_ambient_light_sensor_factory_interface.h"

namespace power_manager {
namespace system {

// Creates external ambient light sensors that use
// AmbientLightSensorDelegateFile.
class ExternalAmbientLightSensorFactoryFile
    : public ExternalAmbientLightSensorFactoryInterface {
 public:
  std::unique_ptr<AmbientLightSensorInterface> CreateSensor(
      const std::string& device_name) const override;
};

}  // namespace system
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_SYSTEM_EXTERNAL_AMBIENT_LIGHT_SENSOR_FACTORY_FILE_H_
