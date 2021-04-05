// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_MOCK_BLUEZ_BATTERY_PROVIDER_H_
#define POWER_MANAGER_POWERD_SYSTEM_MOCK_BLUEZ_BATTERY_PROVIDER_H_

#include <string>

#include <gmock/gmock.h>

#include "power_manager/powerd/system/bluez_battery_provider.h"

namespace power_manager {
namespace system {

class MockBluezBatteryProvider : public BluezBatteryProvider {
 public:
  MockBluezBatteryProvider() = default;
  virtual ~MockBluezBatteryProvider() = default;

  MOCK_METHOD(void, UpdateDeviceBattery, (const std::string&, int), (override));
};

}  // namespace system
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_SYSTEM_MOCK_BLUEZ_BATTERY_PROVIDER_H_
