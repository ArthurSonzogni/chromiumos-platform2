// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/bluetooth/bluetooth_manager.h"

#include "shill/control_interface.h"

namespace shill {
BluetoothManager::BluetoothManager(ControlInterface* control_interface)
    : control_interface_(control_interface) {}

void BluetoothManager::Start() {
  bluetooth_manager_proxy_ = control_interface_->CreateBluetoothManagerProxy();
  bluez_proxy_ = control_interface_->CreateBluetoothBlueZProxy();
}

void BluetoothManager::Stop() {
  bluetooth_manager_proxy_.reset();
  bluez_proxy_.reset();
}

bool BluetoothManager::GetAvailableAdapters(
    bool* is_floss,
    std::vector<BluetoothManagerInterface::BTAdapterWithEnabled>* adapters)
    const {
  if (!bluetooth_manager_proxy_->GetAvailableAdapters(is_floss, adapters)) {
    LOG(ERROR) << __func__ << ": Failed to query available BT adapters";
    return false;
  }
  if (*is_floss) {
    // The device is using Floss so in that case BluetoothManagerProxy was able
    // to report the state of the BT adapters. Nothing left to do, return
    // success.
    return true;
  }
  // Fallback to BlueZ if Floss is not in use.
  bool powered;
  if (!bluez_proxy_->GetAdapterPowered(&powered)) {
    LOG(ERROR) << __func__ << ": Failed to query BT powered state from BlueZ";
    return false;
  }
  // For BlueZ we only support 1 adapter, interface 0.
  adapters->push_back({.hci_interface = 0, .enabled = powered});

  return true;
}
}  // namespace shill
