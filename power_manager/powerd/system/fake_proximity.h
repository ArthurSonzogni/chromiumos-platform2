// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_FAKE_PROXIMITY_H_
#define POWER_MANAGER_POWERD_SYSTEM_FAKE_PROXIMITY_H_

#include "power_manager/powerd/system/fake_sensor_device.h"

namespace power_manager {
namespace system {

class FakeProximity : public FakeSensorDevice {
 public:
  FakeProximity();

  // Implementation of FakeSensorDevice.
  cros::mojom::DeviceType GetDeviceType() const override;

  // Implementation of cros::mojom::SensorDevice.
  void GetAllEvents(GetAllEventsCallback callback) override;
};

}  // namespace system
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_SYSTEM_FAKE_PROXIMITY_H_
