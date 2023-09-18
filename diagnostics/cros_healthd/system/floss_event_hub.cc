// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/system/floss_event_hub.h"

#include <memory>
#include <string>
#include <utility>

#include <base/strings/string_number_conversions.h>

#include "diagnostics/cros_healthd/utils/dbus_utils.h"

namespace diagnostics {

namespace {

constexpr char kExportedBluetoothCallbackPath[] =
    "/org/chromium/bluetooth/healthd/adapterclient";
constexpr char kExportedBluetoothManagerCallbackPath[] =
    "/org/chromium/bluetooth/healthd/managerclient";

}  // namespace

FlossEventHub::FlossEventHub(
    const scoped_refptr<dbus::Bus>& bus,
    org::chromium::bluetooth::Manager::ObjectManagerProxy*
        bluetooth_manager_proxy,
    org::chromium::bluetooth::ObjectManagerProxy* bluetooth_proxy)
    : bus_(bus) {
  if (bluetooth_manager_proxy) {
    bluetooth_manager_proxy->SetManagerAddedCallback(base::BindRepeating(
        &FlossEventHub::OnManagerAdded, weak_ptr_factory_.GetWeakPtr()));
    bluetooth_manager_proxy->SetManagerRemovedCallback(base::BindRepeating(
        &FlossEventHub::OnManagerRemoved, weak_ptr_factory_.GetWeakPtr()));
  }

  if (bluetooth_proxy) {
    bluetooth_proxy->SetBluetoothAddedCallback(base::BindRepeating(
        &FlossEventHub::OnAdapterAdded, weak_ptr_factory_.GetWeakPtr()));
    bluetooth_proxy->SetBluetoothRemovedCallback(base::BindRepeating(
        &FlossEventHub::OnAdapterRemoved, weak_ptr_factory_.GetWeakPtr()));
  }
}

base::CallbackListSubscription FlossEventHub::SubscribeAdapterAdded(
    OnFlossAdapterAddedCallback callback) {
  return adapter_added_observers_.Add(callback);
}

base::CallbackListSubscription FlossEventHub::SubscribeAdapterRemoved(
    OnFlossAdapterRemovedCallback callback) {
  return adapter_removed_observers_.Add(callback);
}

base::CallbackListSubscription FlossEventHub::SubscribeAdapterPoweredChanged(
    OnFlossAdapterPoweredChangedCallback callback) {
  return adapter_powered_changed_observers_.Add(callback);
}

base::CallbackListSubscription
FlossEventHub::SubscribeAdapterDiscoveringChanged(
    OnFlossAdapterDiscoveringChangedCallback callback) {
  return adapter_discovering_changed_observers_.Add(callback);
}

base::CallbackListSubscription FlossEventHub::SubscribeDeviceAdded(
    OnFlossDeviceAddedCallback callback) {
  return device_added_observers_.Add(callback);
}

base::CallbackListSubscription FlossEventHub::SubscribeDeviceRemoved(
    OnFlossDeviceRemovedCallback callback) {
  return device_removed_observers_.Add(callback);
}

base::CallbackListSubscription FlossEventHub::SubscribeManagerRemoved(
    OnFlossManagerRemovedCallback callback) {
  return manager_removed_observers_.Add(callback);
}

void FlossEventHub::OnAdapterAdded(
    org::chromium::bluetooth::BluetoothProxyInterface* adapter) {
  if (adapter) {
    auto exported_path =
        dbus::ObjectPath(kExportedBluetoothCallbackPath +
                         base::NumberToString(callback_path_index_));
    callback_path_index_ += 1;
    auto [on_success, on_error] = SplitDbusCallback(
        base::BindOnce(&FlossEventHub::HandleRegisterBluetoothCallbackResponse,
                       weak_ptr_factory_.GetWeakPtr(), adapter->GetObjectPath(),
                       exported_path));
    adapter->RegisterCallbackAsync(exported_path, std::move(on_success),
                                   std::move(on_error));
  }
  adapter_added_observers_.Notify(adapter);
}

void FlossEventHub::OnAdapterRemoved(const dbus::ObjectPath& adapter_path) {
  adapter_callbacks_[adapter_path].reset();
  adapter_removed_observers_.Notify(adapter_path);
}

void FlossEventHub::OnManagerAdded(
    org::chromium::bluetooth::ManagerProxyInterface* manager) {
  if (manager) {
    auto exported_path =
        dbus::ObjectPath(kExportedBluetoothManagerCallbackPath);
    auto [on_success, on_error] = SplitDbusCallback(
        base::BindOnce(&FlossEventHub::HandleRegisterManagerCallbackResponse,
                       weak_ptr_factory_.GetWeakPtr(), exported_path));
    manager->RegisterCallbackAsync(exported_path, std::move(on_success),
                                   std::move(on_error));
  }
}

void FlossEventHub::OnManagerRemoved(const dbus::ObjectPath& manager_path) {
  manager_callback_.reset();
  manager_removed_observers_.Notify(manager_path);
}

void FlossEventHub::OnAdapterPoweredChanged(int32_t hci_interface,
                                            bool powered) {
  adapter_powered_changed_observers_.Notify(hci_interface, powered);
}

void FlossEventHub::OnAdapterDiscoveringChanged(
    const dbus::ObjectPath& adapter_path, bool discovering) {
  adapter_discovering_changed_observers_.Notify(adapter_path, discovering);
}

void FlossEventHub::OnDeviceAdded(const brillo::VariantDictionary& device) {
  device_added_observers_.Notify(device);
}

void FlossEventHub::OnDeviceRemoved(const brillo::VariantDictionary& device) {
  device_removed_observers_.Notify(device);
}

void FlossEventHub::HandleRegisterBluetoothCallbackResponse(
    const dbus::ObjectPath& adapter_path,
    const dbus::ObjectPath& callback_path,
    brillo::Error* error,
    uint32_t register_id) {
  if (error) {
    LOG(ERROR) << "Failed to register org.chromium.bluetooth.BluetoothCallback";
    return;
  }

  if (bus_) {
    adapter_callbacks_[adapter_path].reset();
    adapter_callbacks_[adapter_path] =
        std::make_unique<BluetoothCallbackService>(this, bus_, callback_path);
  }
}

void FlossEventHub::HandleRegisterManagerCallbackResponse(
    const dbus::ObjectPath& callback_path, brillo::Error* error) {
  if (error) {
    LOG(ERROR) << "Failed to register org.chromium.bluetooth.ManagerCallback";
    return;
  }

  if (bus_) {
    manager_callback_.reset();
    manager_callback_ =
        std::make_unique<ManagerCallbackService>(this, bus_, callback_path);
  }
}

}  // namespace diagnostics
