// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/dbus/bluetooth_manager_proxy.h"

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
}  // namespace shill
