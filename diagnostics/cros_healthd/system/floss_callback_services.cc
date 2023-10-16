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
    const dbus::ObjectPath& object_path,
    const dbus::ObjectPath& adapter_path)
    : org::chromium::bluetooth::BluetoothCallbackAdaptor(this),
      event_hub_(event_hub),
      adapter_path_(adapter_path),
      dbus_object_(nullptr, bus, object_path) {
  RegisterWithDBusObject(&dbus_object_);
  dbus_object_.RegisterAndBlock();
}

BluetoothCallbackService::~BluetoothCallbackService() = default;

void BluetoothCallbackService::OnAdapterPropertyChanged(uint32_t property) {
  event_hub_->OnAdapterPropertyChanged(adapter_path_, property);
}

void BluetoothCallbackService::OnAddressChanged(const std::string& address) {}

void BluetoothCallbackService::OnNameChanged(const std::string& name) {}

void BluetoothCallbackService::OnDiscoverableChanged(bool discoverable) {}

void BluetoothCallbackService::OnDiscoveringChanged(bool discovering) {
  event_hub_->OnAdapterDiscoveringChanged(adapter_path_, discovering);
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
    const std::vector<uint32_t>& properties) {
  event_hub_->OnDevicePropertiesChanged(device, properties);
}

void BluetoothCallbackService::OnBondStateChanged(uint32_t status,
                                                  const std::string& address,
                                                  uint32_t state) {}

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

ManagerCallbackService::~ManagerCallbackService() = default;

void ManagerCallbackService::OnHciEnabledChanged(int32_t hci_interface,
                                                 bool enabled) {
  event_hub_->OnAdapterPoweredChanged(hci_interface, enabled);
}

void ManagerCallbackService::OnHciDeviceChanged(int32_t hci_interface,
                                                bool present) {}

void ManagerCallbackService::OnDefaultAdapterChanged(int32_t hci_interface) {}

BluetoothConnectionCallbackService::BluetoothConnectionCallbackService(
    FlossEventHub* event_hub,
    const scoped_refptr<dbus::Bus>& bus,
    const dbus::ObjectPath& object_path)
    : org::chromium::bluetooth::BluetoothConnectionCallbackAdaptor(this),
      event_hub_(event_hub),
      dbus_object_(nullptr, bus, object_path) {
  RegisterWithDBusObject(&dbus_object_);
  dbus_object_.RegisterAndBlock();
}

BluetoothConnectionCallbackService::~BluetoothConnectionCallbackService() =
    default;

void BluetoothConnectionCallbackService::OnDeviceConnected(
    const brillo::VariantDictionary& device) {
  event_hub_->OnDeviceConnectedChanged(device, /*connected=*/true);
}

void BluetoothConnectionCallbackService::OnDeviceDisconnected(
    const brillo::VariantDictionary& device) {
  event_hub_->OnDeviceConnectedChanged(device, /*connected=*/false);
}

ScannerCallbackService::ScannerCallbackService(
    FlossEventHub* event_hub,
    const scoped_refptr<dbus::Bus>& bus,
    const dbus::ObjectPath& object_path)
    : org::chromium::bluetooth::ScannerCallbackAdaptor(this),
      event_hub_(event_hub),
      dbus_object_(nullptr, bus, object_path) {
  RegisterWithDBusObject(&dbus_object_);
  dbus_object_.RegisterAndBlock();
}

ScannerCallbackService::~ScannerCallbackService() = default;

void ScannerCallbackService::OnScanResult(
    const brillo::VariantDictionary& scan_result) {
  // The |scan_result| dictionary should contain the following keys:
  // * "name": string
  // * "address": string
  // * "rssi": int16_t
  // And others...
  event_hub_->OnScanResultReceived(scan_result);
}

}  // namespace diagnostics
