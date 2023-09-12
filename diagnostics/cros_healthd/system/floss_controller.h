// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_SYSTEM_FLOSS_CONTROLLER_H_
#define DIAGNOSTICS_CROS_HEALTHD_SYSTEM_FLOSS_CONTROLLER_H_

#include "diagnostics/dbus_bindings/bluetooth_manager/dbus-proxies.h"

namespace diagnostics {

// Interface for accessing Bluetooth instances via Floss proxy.
class FlossController {
 public:
  explicit FlossController(
      org::chromium::bluetooth::Manager::ObjectManagerProxy*
          bluetooth_manager_proxy = nullptr);
  FlossController(const FlossController&) = delete;
  FlossController& operator=(const FlossController&) = delete;
  virtual ~FlossController() = default;

  virtual org::chromium::bluetooth::ManagerProxyInterface* GetManager() const;

 private:
  // Unowned pointer that should outlive this instance.
  org::chromium::bluetooth::Manager::ObjectManagerProxy* const
      bluetooth_manager_proxy_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_SYSTEM_FLOSS_CONTROLLER_H_
