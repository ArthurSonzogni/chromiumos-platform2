// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include "diagnostics/cros_healthd/system/bluez_controller.h"

namespace diagnostics {

BluezController::BluezController(org::bluezProxy* bluez_proxy)
    : bluez_proxy_(bluez_proxy) {}

std::vector<org::bluez::Adapter1ProxyInterface*> BluezController::GetAdapters()
    const {
  return bluez_proxy_->GetAdapter1Instances();
}
std::vector<org::bluez::Device1ProxyInterface*> BluezController::GetDevices()
    const {
  return bluez_proxy_->GetDevice1Instances();
}
std::vector<org::bluez::AdminPolicyStatus1ProxyInterface*>
BluezController::GetAdminPolicies() const {
  return bluez_proxy_->GetAdminPolicyStatus1Instances();
}
std::vector<org::bluez::Battery1ProxyInterface*> BluezController::GetBatteries()
    const {
  return bluez_proxy_->GetBattery1Instances();
}

}  // namespace diagnostics
