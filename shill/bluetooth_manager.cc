// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/bluetooth_manager.h"
#include "shill/control_interface.h"

namespace shill {
BluetoothManager::BluetoothManager(ControlInterface* control_interface)
    : control_interface_(control_interface) {}

void BluetoothManager::Start() {
  bluetooth_manager_proxy_ = control_interface_->CreateBluetoothManagerProxy();
}

void BluetoothManager::Stop() {
  bluetooth_manager_proxy_.reset();
}
}  // namespace shill
