// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_SYSTEM_FLOSS_CONTROLLER_H_
#define DIAGNOSTICS_CROS_HEALTHD_SYSTEM_FLOSS_CONTROLLER_H_

#include <vector>

namespace org::chromium::bluetooth {
class BluetoothProxyInterface;
class BluetoothAdminProxyInterface;
class BluetoothQAProxyInterface;
class BatteryManagerProxyInterface;
class ManagerProxyInterface;
class ObjectManagerProxy;

namespace Manager {
class ObjectManagerProxy;
}  // namespace Manager

}  // namespace org::chromium::bluetooth

namespace diagnostics {

// Interface for accessing Bluetooth instances via Floss proxy.
class FlossController {
 public:
  explicit FlossController(
      org::chromium::bluetooth::Manager::ObjectManagerProxy*
          bluetooth_manager_proxy = nullptr,
      org::chromium::bluetooth::ObjectManagerProxy* bluetooth_proxy = nullptr);
  FlossController(const FlossController&) = delete;
  FlossController& operator=(const FlossController&) = delete;
  virtual ~FlossController() = default;

  virtual org::chromium::bluetooth::ManagerProxyInterface* GetManager() const;
  virtual std::vector<org::chromium::bluetooth::BluetoothProxyInterface*>
  GetAdapters() const;
  virtual std::vector<org::chromium::bluetooth::BluetoothAdminProxyInterface*>
  GetAdmins() const;
  virtual std::vector<org::chromium::bluetooth::BluetoothQAProxyInterface*>
  GetAdapterQAs() const;
  virtual std::vector<org::chromium::bluetooth::BatteryManagerProxyInterface*>
  GetBatteryManagers() const;

 private:
  // Unowned pointer that should outlive this instance.
  org::chromium::bluetooth::Manager::ObjectManagerProxy* const
      bluetooth_manager_proxy_;
  org::chromium::bluetooth::ObjectManagerProxy* const bluetooth_proxy_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_SYSTEM_FLOSS_CONTROLLER_H_
