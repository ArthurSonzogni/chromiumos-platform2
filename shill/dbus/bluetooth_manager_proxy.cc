// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/dbus/bluetooth_manager_proxy.h"

#include <vector>

#include <brillo/variant_dictionary.h>

#include "bluetooth/dbus-proxies.h"
#include "shill/bluetooth/bluetooth_manager_interface.h"

namespace shill {

namespace {
constexpr char kBTManagerServiceName[] = "org.chromium.bluetooth.Manager";
}

BluetoothManagerProxy::BluetoothManagerProxy(
    const scoped_refptr<dbus::Bus>& bus)
    : manager_proxy_(new org::chromium::bluetooth::ManagerProxy(
          bus, kBTManagerServiceName)) {}

bool BluetoothManagerProxy::GetFlossEnabled(bool* enabled) const {
  brillo::ErrorPtr error;
  if (!manager_proxy_->GetFlossEnabled(enabled, &error)) {
    LOG(ERROR) << "Failed to query Floss status: " << error->GetCode() << " "
               << error->GetMessage();
    return false;
  }
  return true;
}

bool BluetoothManagerProxy::GetAvailableAdapters(
    bool* is_floss,
    std::vector<BluetoothManagerInterface::BTAdapterWithEnabled>* adapters)
    const {
  if (!GetFlossEnabled(is_floss)) {
    return false;
  }
  if (!*is_floss) {
    // The device is not using Floss at the moment. Return immediately since
    // Floss won't know if the BT adapters are enabled or not in that case.
    // Callers may choose to fallback to BlueZ.
    return true;
  }
  std::vector<brillo::VariantDictionary> bt_adapters;
  brillo::ErrorPtr error;
  if (!manager_proxy_->GetAvailableAdapters(&bt_adapters, &error)) {
    LOG(ERROR) << "Failed to query available BT adapters: " << error->GetCode()
               << " " << error->GetMessage();
    return false;
  }
  for (auto bt_adapter : bt_adapters) {
    BluetoothManagerInterface::BTAdapterWithEnabled adapter{
        .hci_interface = brillo::GetVariantValueOrDefault<int32_t>(
            bt_adapter, "hci_interface"),
        .enabled =
            brillo::GetVariantValueOrDefault<bool>(bt_adapter, "enabled")};
    adapters->push_back(adapter);
  }
  return true;
}

}  // namespace shill
