// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/external_ambient_light_sensor_factory_stub.h"

#include <utility>

#include "power_manager/powerd/system/ambient_light_sensor_stub.h"

namespace power_manager {
namespace system {

std::unique_ptr<AmbientLightSensorInterface>
ExternalAmbientLightSensorFactoryStub::CreateSensor(
    const AmbientLightSensorInfo& als_info) const {
  return std::make_unique<AmbientLightSensorStub>(0);
}

}  // namespace system
}  // namespace power_manager
