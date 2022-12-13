// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/dbus/bluetooth_manager_proxy.h"

#include <vector>

#include <brillo/variant_dictionary.h>

#include "bluetooth/dbus-proxies.h"

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
    std::vector<BTAdapterWithEnabled>* adapters) const {
  std::vector<brillo::VariantDictionary> bt_adapters;
  brillo::ErrorPtr error;
  if (!manager_proxy_->GetAvailableAdapters(&bt_adapters, &error)) {
    LOG(ERROR) << "Failed to query available BT adapters: " << error->GetCode()
               << " " << error->GetMessage();
    return false;
  }
  for (auto bt_adapter : bt_adapters) {
    BTAdapterWithEnabled adapter{
        .hci_interface = brillo::GetVariantValueOrDefault<int32_t>(
            bt_adapter, "hci_interface"),
        .enabled =
            brillo::GetVariantValueOrDefault<bool>(bt_adapter, "enabled")};
    adapters->push_back(adapter);
  }
  return true;
}
}  // namespace shill
