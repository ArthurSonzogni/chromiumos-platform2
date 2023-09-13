// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/system/bluez_event_hub.h"

#include <string>

namespace diagnostics {

BluezEventHub::BluezEventHub(org::bluezProxy* bluez_proxy) {
  if (!bluez_proxy)
    return;
  bluez_proxy->SetAdapter1AddedCallback(base::BindRepeating(
      &BluezEventHub::OnAdapterAdded, weak_ptr_factory_.GetWeakPtr()));
  bluez_proxy->SetAdapter1RemovedCallback(base::BindRepeating(
      &BluezEventHub::OnAdapterRemoved, weak_ptr_factory_.GetWeakPtr()));
  bluez_proxy->SetDevice1AddedCallback(base::BindRepeating(
      &BluezEventHub::OnDeviceAdded, weak_ptr_factory_.GetWeakPtr()));
  bluez_proxy->SetDevice1RemovedCallback(base::BindRepeating(
      &BluezEventHub::OnDeviceRemoved, weak_ptr_factory_.GetWeakPtr()));
}

base::CallbackListSubscription BluezEventHub::SubscribeAdapterAdded(
    OnBluetoothAdapterAddedCallback callback) {
  return adapter_added_observers_.Add(callback);
}

base::CallbackListSubscription BluezEventHub::SubscribeAdapterRemoved(
    OnBluetoothAdapterRemovedCallback callback) {
  return adapter_removed_observers_.Add(callback);
}

base::CallbackListSubscription BluezEventHub::SubscribeAdapterPropertyChanged(
    OnBluetoothAdapterPropertyChangedCallback callback) {
  return adapter_property_changed_observers_.Add(callback);
}

base::CallbackListSubscription BluezEventHub::SubscribeDeviceAdded(
    OnBluetoothDeviceAddedCallback callback) {
  return device_added_observers_.Add(callback);
}

base::CallbackListSubscription BluezEventHub::SubscribeDeviceRemoved(
    OnBluetoothDeviceRemovedCallback callback) {
  return device_removed_observers_.Add(callback);
}

base::CallbackListSubscription BluezEventHub::SubscribeDevicePropertyChanged(
    OnBluetoothDevicePropertyChangedCallback callback) {
  return device_property_changed_observers_.Add(callback);
}

void BluezEventHub::OnAdapterAdded(
    org::bluez::Adapter1ProxyInterface* adapter) {
  if (adapter) {
    adapter->SetPropertyChangedCallback(
        base::BindRepeating(&BluezEventHub::OnAdapterPropertyChanged,
                            weak_ptr_factory_.GetWeakPtr()));
  }
  adapter_added_observers_.Notify(adapter);
}

void BluezEventHub::OnAdapterRemoved(const dbus::ObjectPath& adapter_path) {
  adapter_removed_observers_.Notify(adapter_path);
}

void BluezEventHub::OnAdapterPropertyChanged(
    org::bluez::Adapter1ProxyInterface* adapter,
    const std::string& property_name) {
  adapter_property_changed_observers_.Notify(adapter, property_name);
}

void BluezEventHub::OnDeviceAdded(org::bluez::Device1ProxyInterface* device) {
  if (device) {
    device->SetPropertyChangedCallback(
        base::BindRepeating(&BluezEventHub::OnDevicePropertyChanged,
                            weak_ptr_factory_.GetWeakPtr()));
  }
  device_added_observers_.Notify(device);
}

void BluezEventHub::OnDeviceRemoved(const dbus::ObjectPath& device_path) {
  device_removed_observers_.Notify(device_path);
}

void BluezEventHub::OnDevicePropertyChanged(
    org::bluez::Device1ProxyInterface* device,
    const std::string& property_name) {
  device_property_changed_observers_.Notify(device, property_name);
}

}  // namespace diagnostics
