// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_BLUETOOTH_BLUETOOTH_MANAGER_H_
#define SHILL_BLUETOOTH_BLUETOOTH_MANAGER_H_

#include <memory>

#include "shill/bluetooth/bluetooth_manager_proxy_interface.h"

namespace shill {
class ControlInterface;

class BluetoothManager {
 public:
  explicit BluetoothManager(ControlInterface* control_interface);
  BluetoothManager(const BluetoothManager&) = delete;
  BluetoothManager& operator=(const BluetoothManager&) = delete;

  virtual ~BluetoothManager() = default;

  virtual void Start();

  virtual void Stop();

  BluetoothManagerProxyInterface* proxy() {
    return bluetooth_manager_proxy_.get();
  }

 private:
  ControlInterface* control_interface_;

  std::unique_ptr<BluetoothManagerProxyInterface> bluetooth_manager_proxy_;
};

}  // namespace shill

#endif  // SHILL_BLUETOOTH_BLUETOOTH_MANAGER_H_
