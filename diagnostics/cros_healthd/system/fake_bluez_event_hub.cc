// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/system/fake_bluez_event_hub.h"

#include <string>

namespace diagnostics {

void FakeBluezEventHub::SendAdapterAdded(
    org::bluez::Adapter1ProxyInterface* adapter) {
  OnAdapterAdded(adapter);
}
void FakeBluezEventHub::SendAdapterRemoved(
    const dbus::ObjectPath& adapter_path) {
  OnAdapterRemoved(adapter_path);
}
void FakeBluezEventHub::SendAdapterPropertyChanged(
    org::bluez::Adapter1ProxyInterface* adapter,
    const std::string& property_name) {
  OnAdapterPropertyChanged(adapter, property_name);
}
void FakeBluezEventHub::SendDeviceAdded(
    org::bluez::Device1ProxyInterface* device) {
  OnDeviceAdded(device);
}
void FakeBluezEventHub::SendDeviceRemoved(const dbus::ObjectPath& device_path) {
  OnDeviceRemoved(device_path);
}
void FakeBluezEventHub::SendDevicePropertyChanged(
    org::bluez::Device1ProxyInterface* device,
    const std::string& property_name) {
  OnDevicePropertyChanged(device, property_name);
}

}  // namespace diagnostics
