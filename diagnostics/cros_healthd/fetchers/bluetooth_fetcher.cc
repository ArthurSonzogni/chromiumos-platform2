// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetchers/bluetooth_fetcher.h"

#include <cstdint>
#include <map>
#include <utility>
#include <vector>

#include <base/check.h>
#include <dbus/object_path.h>

namespace diagnostics {

namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

}  // namespace

std::vector<org::bluez::Adapter1ProxyInterface*>
BluetoothFetcher::GetAdapterInstances() {
  DCHECK(context_->bluetooth_proxy());
  return context_->bluetooth_proxy()->GetAdapter1Instances();
}

std::vector<org::bluez::Device1ProxyInterface*>
BluetoothFetcher::GetDeviceInstances() {
  DCHECK(context_->bluetooth_proxy());
  return context_->bluetooth_proxy()->GetDevice1Instances();
}

mojo_ipc::BluetoothResultPtr BluetoothFetcher::FetchBluetoothInfo() {
  return FetchBluetoothInfo(GetAdapterInstances(), GetDeviceInstances());
}

mojo_ipc::BluetoothResultPtr BluetoothFetcher::FetchBluetoothInfo(
    std::vector<org::bluez::Adapter1ProxyInterface*> adapters,
    std::vector<org::bluez::Device1ProxyInterface*> devices) {
  std::vector<mojo_ipc::BluetoothAdapterInfoPtr> adapter_infos;

  // Map from the adapter's ObjectPath to the number of connected devices.
  std::map<dbus::ObjectPath, uint32_t> num_connected_devices;
  FetchDevicesInfo(devices, num_connected_devices);

  for (const auto& adapter : adapters) {
    if (!adapter)
      continue;
    mojo_ipc::BluetoothAdapterInfo info;

    info.name = adapter->name();
    info.address = adapter->address();
    info.powered = adapter->powered();
    info.discoverable = adapter->discoverable();
    info.discovering = adapter->discovering();
    info.uuids = adapter->uuids();
    info.modalias = adapter->modalias();

    info.num_connected_devices = 0;
    const auto it = num_connected_devices.find(adapter->GetObjectPath());
    if (it != num_connected_devices.end())
      info.num_connected_devices = it->second;

    adapter_infos.push_back(info.Clone());
  }

  return mojo_ipc::BluetoothResult::NewBluetoothAdapterInfo(
      std::move(adapter_infos));
}

void BluetoothFetcher::FetchDevicesInfo(
    std::vector<org::bluez::Device1ProxyInterface*> devices,
    std::map<dbus::ObjectPath, uint32_t>& num_connected_devices) {
  for (const auto& device : devices) {
    if (!device || !device->connected())
      continue;
    num_connected_devices[device->adapter()]++;
  }
}

}  // namespace diagnostics
