// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/system/floss_controller.h"

namespace diagnostics {

FlossController::FlossController(
    org::chromium::bluetooth::Manager::ObjectManagerProxy*
        bluetooth_manager_proxy,
    org::chromium::bluetooth::ObjectManagerProxy* bluetooth_proxy)
    : bluetooth_manager_proxy_(bluetooth_manager_proxy),
      bluetooth_proxy_(bluetooth_proxy) {}

org::chromium::bluetooth::ManagerProxyInterface* FlossController::GetManager()
    const {
  auto managers = bluetooth_manager_proxy_->GetManagerInstances();
  if (managers.empty()) {
    return nullptr;
  }
  return managers[0];
}

std::vector<org::chromium::bluetooth::BluetoothProxyInterface*>
FlossController::GetAdapters() const {
  return bluetooth_proxy_->GetBluetoothInstances();
}

}  // namespace diagnostics
