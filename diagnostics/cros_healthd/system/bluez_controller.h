// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_SYSTEM_BLUEZ_CONTROLLER_H_
#define DIAGNOSTICS_CROS_HEALTHD_SYSTEM_BLUEZ_CONTROLLER_H_

#include <vector>

namespace org {
class bluezProxy;

namespace bluez {
class Adapter1ProxyInterface;
class AdminPolicyStatus1ProxyInterface;
class Battery1ProxyInterface;
class Device1ProxyInterface;
}  // namespace bluez

}  // namespace org

namespace diagnostics {

// Interface for accessing properties of Bluetooth adapters and devices.
class BluezController {
 public:
  explicit BluezController(org::bluezProxy* bluez_proxy = nullptr);
  BluezController(const BluezController&) = delete;
  BluezController& operator=(const BluezController&) = delete;
  virtual ~BluezController() = default;

  virtual std::vector<org::bluez::Adapter1ProxyInterface*> GetAdapters() const;
  virtual std::vector<org::bluez::Device1ProxyInterface*> GetDevices() const;
  virtual std::vector<org::bluez::AdminPolicyStatus1ProxyInterface*>
  GetAdminPolicies() const;
  virtual std::vector<org::bluez::Battery1ProxyInterface*> GetBatteries() const;

 private:
  // Unowned pointer that should outlive this instance.
  org::bluezProxy* const bluez_proxy_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_SYSTEM_BLUEZ_CONTROLLER_H_
