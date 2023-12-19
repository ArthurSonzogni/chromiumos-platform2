// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_MOCK_FLOSS_BATTERY_PROVIDER_H_
#define POWER_MANAGER_POWERD_SYSTEM_MOCK_FLOSS_BATTERY_PROVIDER_H_

#include <string>

#include <gmock/gmock.h>

#include "power_manager/powerd/system/floss_battery_provider.h"

namespace power_manager::system {

class MockFlossBatteryProvider : public FlossBatteryProvider {
 public:
  MockFlossBatteryProvider() = default;
  ~MockFlossBatteryProvider() override = default;

  MOCK_METHOD(void, UpdateDeviceBattery, (const std::string&, int), (override));
};

}  // namespace power_manager::system

#endif  // POWER_MANAGER_POWERD_SYSTEM_MOCK_FLOSS_BATTERY_PROVIDER_H_
