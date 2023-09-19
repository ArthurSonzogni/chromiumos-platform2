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

    bluetooth_proxy->SetBluetoothGattAddedCallback(base::BindRepeating(
        &FlossEventHub::OnAdapterGattAdded, weak_ptr_factory_.GetWeakPtr()));
    bluetooth_proxy->SetBluetoothGattRemovedCallback(base::BindRepeating(
        &FlossEventHub::OnAdapterGattRemoved, weak_ptr_factory_.GetWeakPtr()));
  }
}

FlossEventHub::~FlossEventHub() = default;

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

base::CallbackListSubscription FlossEventHub::SubscribeScanResultReceived(
    OnFlossScanResultReceivedCallback callback) {
  return scan_result_received_observers_.Add(callback);
}

void FlossEventHub::OnAdapterAdded(
    org::chromium::bluetooth::BluetoothProxyInterface* adapter) {
  if (adapter) {
    const auto adapter_path = adapter->GetObjectPath();
    auto exported_path = GetNextBluetoothCallbackPath();
    auto [on_success, on_error] = SplitDbusCallback(base::BindOnce(
        &FlossEventHub::HandleRegisterBluetoothCallbackResponse,
        weak_ptr_factory_.GetWeakPtr(), adapter_path, exported_path));
    adapter->RegisterCallbackAsync(exported_path, std::move(on_success),
                                   std::move(on_error));

    auto exported_path_conn = GetNextBluetoothCallbackPath();
    auto [on_success_conn, on_error_conn] = SplitDbusCallback(base::BindOnce(
        &FlossEventHub::HandleRegisterConnectionCallbackResponse,
        weak_ptr_factory_.GetWeakPtr(), adapter_path, exported_path_conn));
    adapter->RegisterConnectionCallbackAsync(exported_path_conn,
                                             std::move(on_success_conn),
                                             std::move(on_error_conn));
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

void FlossEventHub::OnAdapterGattAdded(
    org::chromium::bluetooth::BluetoothGattProxyInterface* adapter) {
  if (adapter) {
    auto exported_path = GetNextBluetoothCallbackPath();
    auto [on_success, on_error] = SplitDbusCallback(
        base::BindOnce(&FlossEventHub::HandleRegisterScannerCallbackResponse,
                       weak_ptr_factory_.GetWeakPtr(), adapter->GetObjectPath(),
                       exported_path));
    adapter->RegisterScannerCallbackAsync(exported_path, std::move(on_success),
                                          std::move(on_error));
  }
}

void FlossEventHub::OnAdapterGattRemoved(const dbus::ObjectPath& adapter_path) {
  scanner_callbacks_[adapter_path].reset();
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

void FlossEventHub::OnScanResultReceived(
    const brillo::VariantDictionary& scan_result) {
  scan_result_received_observers_.Notify(scan_result);
}

dbus::ObjectPath FlossEventHub::GetNextBluetoothCallbackPath() {
  auto exported_path =
      dbus::ObjectPath(kExportedBluetoothCallbackPath +
                       base::NumberToString(callback_path_index_));
  callback_path_index_ += 1;
  return exported_path;
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

void FlossEventHub::HandleRegisterConnectionCallbackResponse(
    const dbus::ObjectPath& adapter_path,
    const dbus::ObjectPath& callback_path,
    brillo::Error* error,
    uint32_t register_id) {
  if (error) {
    LOG(ERROR) << "Failed to register "
                  "org.chromium.bluetooth.BluetoothConnectionCallback";
    return;
  }

  if (bus_) {
    connection_callbacks_[adapter_path].reset();
    connection_callbacks_[adapter_path] =
        std::make_unique<BluetoothConnectionCallbackService>(bus_,
                                                             callback_path);
  }
}

void FlossEventHub::HandleRegisterScannerCallbackResponse(
    const dbus::ObjectPath& adapter_path,
    const dbus::ObjectPath& callback_path,
    brillo::Error* error,
    uint32_t register_id) {
  if (error) {
    LOG(ERROR) << "Failed to register org.chromium.bluetooth.ScannerCallback";
    return;
  }

  if (bus_) {
    scanner_callbacks_[adapter_path].reset();
    scanner_callbacks_[adapter_path] =
        std::make_unique<ScannerCallbackService>(this, bus_, callback_path);
  }
}

}  // namespace diagnostics
