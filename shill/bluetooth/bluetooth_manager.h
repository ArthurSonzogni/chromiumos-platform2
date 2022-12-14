// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_BLUETOOTH_BLUETOOTH_MANAGER_H_
#define SHILL_BLUETOOTH_BLUETOOTH_MANAGER_H_

#include <memory>
#include <vector>

#include "shill/bluetooth/bluetooth_bluez_proxy_interface.h"
#include "shill/bluetooth/bluetooth_manager_interface.h"
#include "shill/bluetooth/bluetooth_manager_proxy_interface.h"

namespace shill {
class ControlInterface;

class BluetoothManager : public BluetoothManagerInterface {
 public:
  explicit BluetoothManager(ControlInterface* control_interface);
  BluetoothManager(const BluetoothManager&) = delete;
  BluetoothManager& operator=(const BluetoothManager&) = delete;

  ~BluetoothManager() override = default;

  void Start() override;

  void Stop() override;

  bool GetAvailableAdapters(
      bool* is_floss,
      std::vector<BluetoothManagerInterface::BTAdapterWithEnabled>* adapters)
      const override;

 private:
  ControlInterface* control_interface_;

  std::unique_ptr<BluetoothManagerProxyInterface> bluetooth_manager_proxy_;

  std::unique_ptr<BluetoothBlueZProxyInterface> bluez_proxy_;
};

}  // namespace shill

#endif  // SHILL_BLUETOOTH_BLUETOOTH_MANAGER_H_
