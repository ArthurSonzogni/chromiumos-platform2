// Copyright 2016 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_CROS_POWER_MANAGER_CHROMEOS_H_
#define UPDATE_ENGINE_CROS_POWER_MANAGER_CHROMEOS_H_

#include <power_manager/dbus-proxies.h>

#include "update_engine/cros/power_manager_interface.h"

namespace chromeos_update_engine {

class PowerManagerChromeOS : public PowerManagerInterface {
 public:
  PowerManagerChromeOS();
  PowerManagerChromeOS(const PowerManagerChromeOS&) = delete;
  PowerManagerChromeOS& operator=(const PowerManagerChromeOS&) = delete;

  ~PowerManagerChromeOS() override = default;

  // PowerManagerInterface overrides.
  bool RequestReboot() override;
  bool RequestShutdown() override;

 private:
  // Real DBus proxy using the DBus connection.
  org::chromium::PowerManagerProxy power_manager_proxy_;
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_CROS_POWER_MANAGER_CHROMEOS_H_
