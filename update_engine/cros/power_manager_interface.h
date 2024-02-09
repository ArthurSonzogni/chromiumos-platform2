// Copyright 2016 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_CROS_POWER_MANAGER_INTERFACE_H_
#define UPDATE_ENGINE_CROS_POWER_MANAGER_INTERFACE_H_

#include <memory>

namespace chromeos_update_engine {

class PowerManagerInterface {
 public:
  PowerManagerInterface(const PowerManagerInterface&) = delete;
  PowerManagerInterface& operator=(const PowerManagerInterface&) = delete;

  virtual ~PowerManagerInterface() = default;

  // Request the power manager to restart the device. Returns true on success.
  virtual bool RequestReboot() = 0;

  // Request the power manager to shutdown the device. Returns true on success.
  virtual bool RequestShutdown() = 0;

 protected:
  PowerManagerInterface() = default;
};

namespace power_manager {
// Factory function which create a PowerManager.
std::unique_ptr<PowerManagerInterface> CreatePowerManager();
}  // namespace power_manager

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_CROS_POWER_MANAGER_INTERFACE_H_
