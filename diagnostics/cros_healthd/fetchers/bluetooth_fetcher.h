// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCHERS_BLUETOOTH_FETCHER_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCHERS_BLUETOOTH_FETCHER_H_

#include <map>
#include <vector>

#include "diagnostics/cros_healthd/fetchers/base_fetcher.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {

// The BluetoothFetcher class is responsible for gathering a device's Bluetooth
// information.
class BluetoothFetcher final : public BaseFetcher {
 public:
  using BaseFetcher::BaseFetcher;

  // Returns the device's Bluetooth information.
  chromeos::cros_healthd::mojom::BluetoothResultPtr FetchBluetoothInfo();
  chromeos::cros_healthd::mojom::BluetoothResultPtr FetchBluetoothInfo(
      std::vector<org::bluez::Adapter1ProxyInterface*> adapters,
      std::vector<org::bluez::Device1ProxyInterface*> devices);

 private:
  std::vector<org::bluez::Adapter1ProxyInterface*> GetAdapterInstances();
  std::vector<org::bluez::Device1ProxyInterface*> GetDeviceInstances();
  void FetchDevicesInfo(
      std::vector<org::bluez::Device1ProxyInterface*> devices,
      std::map<dbus::ObjectPath, uint32_t>& num_connected_devices);
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCHERS_BLUETOOTH_FETCHER_H_
