// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/events/bluetooth_events_impl.h"

#include <string>
#include <utility>

#include <base/check.h>

namespace diagnostics {

BluetoothEventsImpl::BluetoothEventsImpl(Context* context)
    : context_(context), weak_ptr_factory_(this) {
  DCHECK(context_);
  SetProxyCallback();
}

BluetoothEventsImpl::~BluetoothEventsImpl() {}

void BluetoothEventsImpl::AddObserver(
    mojo::PendingRemote<
        chromeos::cros_healthd::mojom::CrosHealthdBluetoothObserver> observer) {
  observers_.Add(std::move(observer));
}

void BluetoothEventsImpl::SetProxyCallback() {
  org::bluezProxy* bluetooth_proxy = context_->bluetooth_proxy();
  DCHECK(bluetooth_proxy);

  bluetooth_proxy->SetAdapter1AddedCallback(base::BindRepeating(
      &BluetoothEventsImpl::AdapterAdded, weak_ptr_factory_.GetWeakPtr()));
  bluetooth_proxy->SetAdapter1RemovedCallback(base::BindRepeating(
      &BluetoothEventsImpl::AdapterRemoved, weak_ptr_factory_.GetWeakPtr()));
  bluetooth_proxy->SetDevice1AddedCallback(base::BindRepeating(
      &BluetoothEventsImpl::DeviceAdded, weak_ptr_factory_.GetWeakPtr()));
  bluetooth_proxy->SetDevice1RemovedCallback(base::BindRepeating(
      &BluetoothEventsImpl::DeviceRemoved, weak_ptr_factory_.GetWeakPtr()));
}

void BluetoothEventsImpl::AdapterAdded(
    org::bluez::Adapter1ProxyInterface* adapter) {
  for (auto& observer : observers_)
    observer->OnAdapterAdded();
  adapter->SetPropertyChangedCallback(
      base::BindRepeating(&BluetoothEventsImpl::AdapterPropertyChanged,
                          weak_ptr_factory_.GetWeakPtr()));
}

void BluetoothEventsImpl::AdapterRemoved(const dbus::ObjectPath& adapter_path) {
  for (auto& observer : observers_)
    observer->OnAdapterRemoved();
}

void BluetoothEventsImpl::AdapterPropertyChanged(
    org::bluez::Adapter1ProxyInterface* adapter,
    const std::string& property_name) {
  for (auto& observer : observers_)
    observer->OnAdapterPropertyChanged();
}

void BluetoothEventsImpl::DeviceAdded(
    org::bluez::Device1ProxyInterface* device) {
  for (auto& observer : observers_)
    observer->OnDeviceAdded();
  device->SetPropertyChangedCallback(
      base::BindRepeating(&BluetoothEventsImpl::DevicePropertyChanged,
                          weak_ptr_factory_.GetWeakPtr()));
}

void BluetoothEventsImpl::DeviceRemoved(const dbus::ObjectPath& device_path) {
  for (auto& observer : observers_)
    observer->OnDeviceRemoved();
}

void BluetoothEventsImpl::DevicePropertyChanged(
    org::bluez::Device1ProxyInterface* device,
    const std::string& property_name) {
  for (auto& observer : observers_)
    observer->OnDevicePropertyChanged();
}

}  // namespace diagnostics
