// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/events/bluetooth_events_impl.h"

#include <string>
#include <utility>

#include <base/check.h>
#include <base/functional/callback_helpers.h>

#include "diagnostics/cros_healthd/system/bluez_event_hub.h"
#include "diagnostics/cros_healthd/system/floss_event_hub.h"

namespace diagnostics {

namespace mojom = ::ash::cros_healthd::mojom;

BluetoothEventsImpl::BluetoothEventsImpl(Context* context) {
  CHECK(context);

  // Bluez events.
  event_subscriptions_.push_back(
      context->bluez_event_hub()->SubscribeAdapterAdded(
          base::BindRepeating(&BluetoothEventsImpl::OnBluezAdapterAdded,
                              weak_ptr_factory_.GetWeakPtr())));
  event_subscriptions_.push_back(
      context->bluez_event_hub()->SubscribeAdapterRemoved(
          base::BindRepeating(&BluetoothEventsImpl::OnBluezAdapterRemoved,
                              weak_ptr_factory_.GetWeakPtr())));
  event_subscriptions_.push_back(
      context->bluez_event_hub()->SubscribeAdapterPropertyChanged(
          base::BindRepeating(
              &BluetoothEventsImpl::OnBluezAdapterPropertyChanged,
              weak_ptr_factory_.GetWeakPtr())));
  event_subscriptions_.push_back(
      context->bluez_event_hub()->SubscribeDeviceAdded(
          base::BindRepeating(&BluetoothEventsImpl::OnBluezDeviceAdded,
                              weak_ptr_factory_.GetWeakPtr())));
  event_subscriptions_.push_back(
      context->bluez_event_hub()->SubscribeDeviceRemoved(
          base::BindRepeating(&BluetoothEventsImpl::OnBluezDeviceRemoved,
                              weak_ptr_factory_.GetWeakPtr())));
  event_subscriptions_.push_back(
      context->bluez_event_hub()->SubscribeDevicePropertyChanged(
          base::BindRepeating(
              &BluetoothEventsImpl::OnBluezDevicePropertyChanged,
              weak_ptr_factory_.GetWeakPtr())));

  // Floss events.
  event_subscriptions_.push_back(
      context->floss_event_hub()->SubscribeAdapterAdded(
          base::BindRepeating(&BluetoothEventsImpl::OnFlossAdapterAdded,
                              weak_ptr_factory_.GetWeakPtr())));
  event_subscriptions_.push_back(
      context->floss_event_hub()->SubscribeAdapterRemoved(
          base::BindRepeating(&BluetoothEventsImpl::OnFlossAdapterRemoved,
                              weak_ptr_factory_.GetWeakPtr())));
}

BluetoothEventsImpl::~BluetoothEventsImpl() {}

void BluetoothEventsImpl::AddObserver(
    mojo::PendingRemote<mojom::EventObserver> observer) {
  observers_.Add(std::move(observer));
}

void BluetoothEventsImpl::OnBluezAdapterAdded(
    org::bluez::Adapter1ProxyInterface* adapter) {
  mojom::BluetoothEventInfo info;
  info.state = mojom::BluetoothEventInfo::State::kAdapterAdded;
  for (auto& observer : observers_)
    observer->OnEvent(mojom::EventInfo::NewBluetoothEventInfo(info.Clone()));
}

void BluetoothEventsImpl::OnBluezAdapterRemoved(
    const dbus::ObjectPath& adapter_path) {
  mojom::BluetoothEventInfo info;
  info.state = mojom::BluetoothEventInfo::State::kAdapterRemoved;
  for (auto& observer : observers_)
    observer->OnEvent(mojom::EventInfo::NewBluetoothEventInfo(info.Clone()));
}

void BluetoothEventsImpl::OnBluezAdapterPropertyChanged(
    org::bluez::Adapter1ProxyInterface* adapter,
    const std::string& property_name) {
  mojom::BluetoothEventInfo info;
  info.state = mojom::BluetoothEventInfo::State::kAdapterPropertyChanged;
  for (auto& observer : observers_)
    observer->OnEvent(mojom::EventInfo::NewBluetoothEventInfo(info.Clone()));
}

void BluetoothEventsImpl::OnBluezDeviceAdded(
    org::bluez::Device1ProxyInterface* device) {
  mojom::BluetoothEventInfo info;
  info.state = mojom::BluetoothEventInfo::State::kDeviceAdded;
  for (auto& observer : observers_)
    observer->OnEvent(mojom::EventInfo::NewBluetoothEventInfo(info.Clone()));
}

void BluetoothEventsImpl::OnBluezDeviceRemoved(
    const dbus::ObjectPath& device_path) {
  mojom::BluetoothEventInfo info;
  info.state = mojom::BluetoothEventInfo::State::kDeviceRemoved;
  for (auto& observer : observers_)
    observer->OnEvent(mojom::EventInfo::NewBluetoothEventInfo(info.Clone()));
}

void BluetoothEventsImpl::OnBluezDevicePropertyChanged(
    org::bluez::Device1ProxyInterface* device,
    const std::string& property_name) {
  mojom::BluetoothEventInfo info;
  info.state = mojom::BluetoothEventInfo::State::kDevicePropertyChanged;
  for (auto& observer : observers_)
    observer->OnEvent(mojom::EventInfo::NewBluetoothEventInfo(info.Clone()));
}

void BluetoothEventsImpl::OnFlossAdapterAdded(
    org::chromium::bluetooth::BluetoothProxyInterface* adapter) {
  mojom::BluetoothEventInfo info;
  info.state = mojom::BluetoothEventInfo::State::kAdapterAdded;
  for (auto& observer : observers_)
    observer->OnEvent(mojom::EventInfo::NewBluetoothEventInfo(info.Clone()));
}

void BluetoothEventsImpl::OnFlossAdapterRemoved(
    const dbus::ObjectPath& adapter_path) {
  mojom::BluetoothEventInfo info;
  info.state = mojom::BluetoothEventInfo::State::kAdapterRemoved;
  for (auto& observer : observers_)
    observer->OnEvent(mojom::EventInfo::NewBluetoothEventInfo(info.Clone()));
}

}  // namespace diagnostics
