// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_BLUETOOTH_BLUETOOTH_MANAGER_PROXY_INTERFACE_H_
#define SHILL_BLUETOOTH_BLUETOOTH_MANAGER_PROXY_INTERFACE_H_

#include <cstdint>
#include <vector>

#include "shill/bluetooth/bluetooth_manager_interface.h"

namespace shill {

class BluetoothManagerProxyInterface {
 public:
  virtual ~BluetoothManagerProxyInterface() = default;

  virtual bool GetAvailableAdapters(
      bool* is_floss,
      std::vector<BluetoothManagerInterface::BTAdapterWithEnabled>* adapters)
      const = 0;
};

}  // namespace shill

#endif  // SHILL_BLUETOOTH_BLUETOOTH_MANAGER_PROXY_INTERFACE_H_
