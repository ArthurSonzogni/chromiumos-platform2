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

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

void SendBluetoothEvent(
    const mojo::RemoteSet<mojom::EventObserver>& event_observers,
    mojom::BluetoothEventInfo::State event_state) {
  mojom::BluetoothEventInfo info;
  info.state = event_state;
  for (auto& observer : event_observers)
    observer->OnEvent(mojom::EventInfo::NewBluetoothEventInfo(info.Clone()));
}

}  // namespace

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
  event_subscriptions_.push_back(
      context->floss_event_hub()->SubscribeAdapterPropertyChanged(
          base::BindRepeating(
              &BluetoothEventsImpl::OnFlossAdapterPropertyChanged,
              weak_ptr_factory_.GetWeakPtr())));
  // Since `discovering` is not included in `BtPropertyType` enum, we need to
  // subscribe this event separately.
  event_subscriptions_.push_back(
      context->floss_event_hub()->SubscribeAdapterDiscoveringChanged(
          base::BindRepeating(
              &BluetoothEventsImpl::OnFlossAdapterDiscoveringChanged,
              weak_ptr_factory_.GetWeakPtr())));
  event_subscriptions_.push_back(
      context->floss_event_hub()->SubscribeDeviceAdded(
          base::BindRepeating(&BluetoothEventsImpl::OnFlossDeviceAdded,
                              weak_ptr_factory_.GetWeakPtr())));
  event_subscriptions_.push_back(
      context->floss_event_hub()->SubscribeDeviceRemoved(
          base::BindRepeating(&BluetoothEventsImpl::OnFlossDeviceRemoved,
                              weak_ptr_factory_.GetWeakPtr())));
  event_subscriptions_.push_back(
      context->floss_event_hub()->SubscribeDevicePropertyChanged(
          base::BindRepeating(
              &BluetoothEventsImpl::OnFlossDevicePropertyChanged,
              weak_ptr_factory_.GetWeakPtr())));
  // Since `connected` is not included in `BtPropertyType` enum, we need to
  // subscribe this event separately.
  event_subscriptions_.push_back(
      context->floss_event_hub()->SubscribeDeviceConnectedChanged(
          base::BindRepeating(
              &BluetoothEventsImpl::OnFlossDeviceConnectedChanged,
              weak_ptr_factory_.GetWeakPtr())));
  // Since bond state is not included in `BtPropertyType` enum, we need to
  // subscribe this event separately.
  event_subscriptions_.push_back(
      context->floss_event_hub()->SubscribeDeviceBondChanged(
          base::BindRepeating(&BluetoothEventsImpl::OnFlossDeviceBondChanged,
                              weak_ptr_factory_.GetWeakPtr())));
}

BluetoothEventsImpl::~BluetoothEventsImpl() {}

void BluetoothEventsImpl::AddObserver(
    mojo::PendingRemote<mojom::EventObserver> observer) {
  observers_.Add(std::move(observer));
}

void BluetoothEventsImpl::OnBluezAdapterAdded(
    org::bluez::Adapter1ProxyInterface* adapter) {
  SendBluetoothEvent(observers_,
                     mojom::BluetoothEventInfo::State::kAdapterAdded);
}

void BluetoothEventsImpl::OnBluezAdapterRemoved(
    const dbus::ObjectPath& adapter_path) {
  SendBluetoothEvent(observers_,
                     mojom::BluetoothEventInfo::State::kAdapterRemoved);
}

void BluetoothEventsImpl::OnBluezAdapterPropertyChanged(
    org::bluez::Adapter1ProxyInterface* adapter,
    const std::string& property_name) {
  SendBluetoothEvent(observers_,
                     mojom::BluetoothEventInfo::State::kAdapterPropertyChanged);
}

void BluetoothEventsImpl::OnBluezDeviceAdded(
    org::bluez::Device1ProxyInterface* device) {
  SendBluetoothEvent(observers_,
                     mojom::BluetoothEventInfo::State::kDeviceAdded);
}

void BluetoothEventsImpl::OnBluezDeviceRemoved(
    const dbus::ObjectPath& device_path) {
  SendBluetoothEvent(observers_,
                     mojom::BluetoothEventInfo::State::kDeviceRemoved);
}

void BluetoothEventsImpl::OnBluezDevicePropertyChanged(
    org::bluez::Device1ProxyInterface* device,
    const std::string& property_name) {
  SendBluetoothEvent(observers_,
                     mojom::BluetoothEventInfo::State::kDevicePropertyChanged);
}

void BluetoothEventsImpl::OnFlossAdapterAdded(
    org::chromium::bluetooth::BluetoothProxyInterface* adapter) {
  SendBluetoothEvent(observers_,
                     mojom::BluetoothEventInfo::State::kAdapterAdded);
}

void BluetoothEventsImpl::OnFlossAdapterRemoved(
    const dbus::ObjectPath& adapter_path) {
  SendBluetoothEvent(observers_,
                     mojom::BluetoothEventInfo::State::kAdapterRemoved);
}

void BluetoothEventsImpl::OnFlossAdapterPropertyChanged(
    const dbus::ObjectPath& adapter_path, BtPropertyType property) {
  SendBluetoothEvent(observers_,
                     mojom::BluetoothEventInfo::State::kAdapterPropertyChanged);
}

void BluetoothEventsImpl::OnFlossAdapterDiscoveringChanged(
    const dbus::ObjectPath& adapter_path, bool discovering) {
  SendBluetoothEvent(observers_,
                     mojom::BluetoothEventInfo::State::kAdapterPropertyChanged);
}

void BluetoothEventsImpl::OnFlossDeviceAdded(
    const brillo::VariantDictionary& device) {
  SendBluetoothEvent(observers_,
                     mojom::BluetoothEventInfo::State::kDeviceAdded);
}

void BluetoothEventsImpl::OnFlossDeviceRemoved(
    const brillo::VariantDictionary& device) {
  SendBluetoothEvent(observers_,
                     mojom::BluetoothEventInfo::State::kDeviceRemoved);
}

void BluetoothEventsImpl::OnFlossDevicePropertyChanged(
    const brillo::VariantDictionary& device, BtPropertyType property) {
  SendBluetoothEvent(observers_,
                     mojom::BluetoothEventInfo::State::kDevicePropertyChanged);
}

void BluetoothEventsImpl::OnFlossDeviceConnectedChanged(
    const brillo::VariantDictionary& device, bool connected) {
  SendBluetoothEvent(observers_,
                     mojom::BluetoothEventInfo::State::kDevicePropertyChanged);
}

void BluetoothEventsImpl::OnFlossDeviceBondChanged(uint32_t bt_status,
                                                   const std::string& address,
                                                   BondState bond_state) {
  SendBluetoothEvent(observers_,
                     mojom::BluetoothEventInfo::State::kDevicePropertyChanged);
}

}  // namespace diagnostics
