// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/system/floss_callback_services.h"

#include <string>

#include <brillo/variant_dictionary.h>

#include "diagnostics/cros_healthd/system/floss_event_hub.h"

namespace diagnostics {

BluetoothCallbackService::BluetoothCallbackService(
    FlossEventHub* event_hub,
    const scoped_refptr<dbus::Bus>& bus,
    const dbus::ObjectPath& object_path)
    : org::chromium::bluetooth::BluetoothCallbackAdaptor(this),
      event_hub_(event_hub),
      object_path_(object_path),
      dbus_object_(nullptr, bus, object_path) {
  RegisterWithDBusObject(&dbus_object_);
  dbus_object_.RegisterAndBlock();
}

void BluetoothCallbackService::OnAdapterPropertyChanged(uint32_t property) {}

void BluetoothCallbackService::OnAddressChanged(const std::string& address) {}

void BluetoothCallbackService::OnNameChanged(const std::string& name) {}

void BluetoothCallbackService::OnDiscoverableChanged(bool discoverable) {}

void BluetoothCallbackService::OnDiscoveringChanged(bool discovering) {
  event_hub_->OnAdapterDiscoveringChanged(object_path_, discovering);
}

void BluetoothCallbackService::OnDeviceFound(
    const brillo::VariantDictionary& device) {
  // The |device| dictionary should contain the following keys:
  // * "name": string
  // * "address": string
  event_hub_->OnDeviceAdded(device);
}

void BluetoothCallbackService::OnDeviceCleared(
    const brillo::VariantDictionary& device) {
  // The |device| dictionary should contain the following keys:
  // * "name": string
  // * "address": string
  event_hub_->OnDeviceRemoved(device);
}

void BluetoothCallbackService::OnDevicePropertiesChanged(
    const brillo::VariantDictionary& device,
    const std::vector<uint32_t>& properties) {}

ManagerCallbackService::ManagerCallbackService(
    FlossEventHub* event_hub,
    const scoped_refptr<dbus::Bus>& bus,
    const dbus::ObjectPath& object_path)
    : org::chromium::bluetooth::ManagerCallbackAdaptor(this),
      event_hub_(event_hub),
      dbus_object_(nullptr, bus, object_path) {
  RegisterWithDBusObject(&dbus_object_);
  dbus_object_.RegisterAndBlock();
}

void ManagerCallbackService::OnHciEnabledChanged(int32_t hci_interface,
                                                 bool enabled) {
  event_hub_->OnAdapterPoweredChanged(hci_interface, enabled);
}

void ManagerCallbackService::OnHciDeviceChanged(int32_t hci_interface,
                                                bool present) {}

void ManagerCallbackService::OnDefaultAdapterChanged(int32_t hci_interface) {}

}  // namespace diagnostics
