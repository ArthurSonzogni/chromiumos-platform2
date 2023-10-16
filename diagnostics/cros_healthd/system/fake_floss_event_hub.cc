// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/system/fake_floss_event_hub.h"

#include <vector>

namespace diagnostics {

void FakeFlossEventHub::SendAdapterAdded(
    org::chromium::bluetooth::BluetoothProxyInterface* adapter) {
  OnAdapterAdded(adapter);
}

void FakeFlossEventHub::SendAdapterRemoved(
    const dbus::ObjectPath& adapter_path) {
  OnAdapterRemoved(adapter_path);
}

void FakeFlossEventHub::SendAdapterPropertyChanged(
    const dbus::ObjectPath& adapter_path, uint32_t property) {
  OnAdapterPropertyChanged(adapter_path, property);
}

void FakeFlossEventHub::SendAdapterPoweredChanged(int32_t hci_interface,
                                                  bool powered) {
  OnAdapterPoweredChanged(hci_interface, powered);
}

void FakeFlossEventHub::SendAdapterDiscoveringChanged(
    const dbus::ObjectPath& adapter_path, bool discovering) {
  OnAdapterDiscoveringChanged(adapter_path, discovering);
}

void FakeFlossEventHub::SendDeviceAdded(
    const brillo::VariantDictionary& device) {
  OnDeviceAdded(device);
}

void FakeFlossEventHub::SendDeviceRemoved(
    const brillo::VariantDictionary& device) {
  OnDeviceRemoved(device);
}

void FakeFlossEventHub::SendDevicePropertiesChanged(
    const brillo::VariantDictionary& device,
    const std::vector<uint32_t>& properties) {
  OnDevicePropertiesChanged(device, properties);
}

void FakeFlossEventHub::SendDeviceConnectedChanged(
    const brillo::VariantDictionary& device, bool connected) {
  OnDeviceConnectedChanged(device, connected);
}

void FakeFlossEventHub::SendManagerAdded(
    org::chromium::bluetooth::ManagerProxyInterface* manager) {
  OnManagerAdded(manager);
}

void FakeFlossEventHub::SendManagerRemoved(
    const dbus::ObjectPath& manager_path) {
  OnManagerRemoved(manager_path);
}

void FakeFlossEventHub::SendAdapterGattAdded(
    org::chromium::bluetooth::BluetoothGattProxyInterface* adapter_gatt) {
  OnAdapterGattAdded(adapter_gatt);
}

}  // namespace diagnostics
