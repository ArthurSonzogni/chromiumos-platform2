// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetchers/bluetooth_fetcher.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

#include <base/check.h>
#include <dbus/object_path.h>

namespace diagnostics {

namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;
constexpr char kBluetoothTypeBrEdrName[] = "BR/EDR";
constexpr char kBluetoothTypeLeName[] = "LE";
constexpr char kBluetoothTypeDualName[] = "DUAL";

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

  // Map from the adapter's ObjectPath to the connected devices.
  std::map<dbus::ObjectPath, std::vector<mojo_ipc::BluetoothDeviceInfoPtr>>
      connected_devices;
  FetchDevicesInfo(devices, connected_devices);

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

    const auto it = connected_devices.find(adapter->GetObjectPath());
    if (it != connected_devices.end()) {
      info.num_connected_devices = it->second.size();
      info.connected_devices = std::move(it->second);
    }

    adapter_infos.push_back(info.Clone());
  }

  return mojo_ipc::BluetoothResult::NewBluetoothAdapterInfo(
      std::move(adapter_infos));
}

void BluetoothFetcher::FetchDevicesInfo(
    std::vector<org::bluez::Device1ProxyInterface*> devices,
    std::map<dbus::ObjectPath, std::vector<mojo_ipc::BluetoothDeviceInfoPtr>>&
        connected_devices) {
  for (const auto& device : devices) {
    if (!device || !device->connected())
      continue;

    mojo_ipc::BluetoothDeviceInfo info;
    info.address = device->address();
    info.name = device->name();
    info.type = GetDeviceType(device->type());
    info.appearance = mojo_ipc::NullableUint16::New(device->appearance());
    info.modalias = device->modalias();
    info.rssi = mojo_ipc::NullableInt16::New(device->rssi());
    info.mtu = mojo_ipc::NullableUint16::New(device->mtu());
    info.uuids = device->uuids();

    connected_devices[device->adapter()].push_back(info.Clone());
  }
}

mojo_ipc::BluetoothDeviceType BluetoothFetcher::GetDeviceType(
    const std::string& type) {
  if (type == kBluetoothTypeBrEdrName)
    return mojom::BluetoothDeviceType::kBrEdr;
  else if (type == kBluetoothTypeLeName)
    return mojom::BluetoothDeviceType::kLe;
  else if (type == kBluetoothTypeDualName)
    return mojom::BluetoothDeviceType::kDual;
  return mojom::BluetoothDeviceType::kUnfound;
}

}  // namespace diagnostics
