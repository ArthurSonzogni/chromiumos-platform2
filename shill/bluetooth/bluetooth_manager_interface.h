// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_BLUETOOTH_BLUETOOTH_MANAGER_INTERFACE_H_
#define SHILL_BLUETOOTH_BLUETOOTH_MANAGER_INTERFACE_H_

#include <cstdint>
#include <vector>

namespace shill {

class BluetoothManagerInterface {
 public:
  virtual ~BluetoothManagerInterface() = default;

  virtual void Start() = 0;

  virtual void Stop() = 0;

  struct BTAdapterWithEnabled {
    int32_t hci_interface;
    bool enabled;
  };

  // Query the BT stack to get the list of adapters present on the system.
  // Returns true if the query was successful, false otherwise.
  // If the query was successful, |is_floss| is set to true if the device is
  // using Floss, false if the device is using BlueZ. After a successful call
  // |adapters| will contain the list of BT adapters available.
  virtual bool GetAvailableAdapters(
      bool* is_floss, std::vector<BTAdapterWithEnabled>* adapters) const = 0;
};

}  // namespace shill

#endif  //  SHILL_BLUETOOTH_BLUETOOTH_MANAGER_INTERFACE_H_
