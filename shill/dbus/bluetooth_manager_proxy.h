// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_DBUS_BLUETOOTH_MANAGER_PROXY_H_
#define SHILL_DBUS_BLUETOOTH_MANAGER_PROXY_H_

#include <memory>
#include <vector>

#include "bluetooth/dbus-proxies.h"
#include "shill/bluetooth/bluetooth_manager_proxy_interface.h"

namespace shill {

class BluetoothManagerProxy : public BluetoothManagerProxyInterface {
 public:
  explicit BluetoothManagerProxy(const scoped_refptr<dbus::Bus>& bus);
  BluetoothManagerProxy(const BluetoothManagerProxy&) = delete;
  BluetoothManagerProxy& operator=(const BluetoothManagerProxy&) = delete;

  ~BluetoothManagerProxy() override = default;

  // Query BT manager to know if BT uses Floss or not. Returns true if the query
  // was successful, false otherwise. If the query was successful, |enabled| is
  // set to true if the device is using Floss, false otherwise.
  bool GetFlossEnabled(bool* enabled) const override;

  bool GetAvailableAdapters(
      std::vector<BTAdapterWithEnabled>* adapters) const override;

 private:
  std::unique_ptr<org::chromium::bluetooth::ManagerProxy> manager_proxy_;
};

}  // namespace shill

#endif  // SHILL_DBUS_BLUETOOTH_MANAGER_PROXY_H_
