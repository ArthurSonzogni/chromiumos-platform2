// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <chromeos/dbus/service_constants.h>
#include <dbus/message.h>

#include "power_manager/powerd/system/floss_battery_provider.h"

namespace power_manager::system {

namespace {

// Timeout for DBus requests.
constexpr base::TimeDelta kFlossBatteryProviderDBusTimeout = base::Seconds(2);

}  // namespace

FlossBatteryProvider::FlossBatteryProvider() : weak_ptr_factory_(this) {}

void FlossBatteryProvider::Init(DBusWrapperInterface* dbus_wrapper) {
  if (is_registered_with_bluetooth_manager_) {
    LOG(ERROR) << __func__ << ": FlossBatteryProvider is already registered.";
    return;
  }

  dbus_wrapper_ = dbus_wrapper;
  bluetooth_manager_object_proxy_ = dbus_wrapper_->GetObjectProxy(
      bluetooth_manager::kBluetoothManagerServiceName,
      bluetooth_manager::kBluetoothManagerServicePath);
  provider_manager_object_proxy_ = dbus_wrapper_->GetObjectProxy(
      battery_manager::kFlossBatteryProviderManagerServiceName,
      battery_manager::kFlossBatteryProviderManagerServicePath);

  auto bus = dbus_wrapper_->GetBus();
  if (bus)
    provider_manager_object_manager_ = bus->GetObjectManager(
        battery_manager::kFlossBatteryProviderManagerServiceName,
        dbus::ObjectPath("/"));

  dbus_wrapper_->ExportMethod(
      kFlossBatteryProviderManagerRefreshBatteryInfo,
      battery_manager::kFlossBatteryProviderManagerCallbackInterface,
      base::BindRepeating(&FlossBatteryProvider::RefreshBatteryInfo,
                          weak_ptr_factory_.GetWeakPtr()));

  dbus_wrapper_->RegisterForServiceAvailability(
      bluetooth_manager_object_proxy_,
      base::BindOnce(&FlossBatteryProvider::RegisterBluetoothManagerCallback,
                     weak_ptr_factory_.GetWeakPtr()));
}

void FlossBatteryProvider::Reset() {
  is_registered_with_provider_manager_ = false;
  dbus_wrapper_->RegisterForInterfaceAvailability(
      provider_manager_object_manager_,
      battery_manager::kFlossBatteryProviderManagerInterface,
      base::BindRepeating(&FlossBatteryProvider::RegisterAsBatteryProvider,
                          weak_ptr_factory_.GetWeakPtr()));
}

void FlossBatteryProvider::UpdateDeviceBattery(const std::string& address,
                                               int level) {}

bool FlossBatteryProvider::IsRegistered() {
  return is_registered_with_bluetooth_manager_ &&
         is_registered_with_provider_manager_;
}

void FlossBatteryProvider::RegisterBluetoothManagerCallback(bool available) {
  if (!available) {
    LOG(ERROR) << __func__
               << ": Failed waiting for btmanagerd to become available.";
    return;
  }

  dbus_wrapper_->ExportMethod(
      bluetooth_manager::kBluetoothManagerOnHciEnabledChanged,
      bluetooth_manager::kBluetoothManagerCallbackInterface,
      base::BindRepeating(&FlossBatteryProvider::OnHciEnabledChanged,
                          weak_ptr_factory_.GetWeakPtr()));

  dbus::MethodCall method_call(
      bluetooth_manager::kBluetoothManagerInterface,
      bluetooth_manager::kBluetoothManagerRegisterCallback);
  dbus::MessageWriter writer(&method_call);
  writer.AppendObjectPath(dbus::ObjectPath(kPowerManagerServicePath));
  dbus_wrapper_->CallMethodAsync(
      bluetooth_manager_object_proxy_, &method_call,
      kFlossBatteryProviderDBusTimeout,
      base::BindOnce(
          &FlossBatteryProvider::OnRegisteredBluetoothManagerCallback,
          weak_ptr_factory_.GetWeakPtr()));
}

void FlossBatteryProvider::OnRegisteredBluetoothManagerCallback(
    dbus::Response* response) {
  if (!response) {
    LOG(ERROR) << __func__ << ": Failed to register for btmanagerd updates.";
    return;
  }

  is_registered_with_bluetooth_manager_ = true;
  Reset();
}

void FlossBatteryProvider::OnHciEnabledChanged(
    dbus::MethodCall* method_call,
    dbus::ExportedObject::ResponseSender response_sender) {
  dbus::MessageReader reader(method_call);
  int32_t hci_interface;
  bool enabled;

  reader.PopInt32(&hci_interface);
  reader.PopBool(&enabled);

  if (!enabled) {
    LOG(INFO) << __func__ << ": Bluetooth was disabled.";
    is_registered_with_provider_manager_ = false;
    return;
  }

  LOG(INFO) << __func__
            << ": Bluetooth was enabled. Re-registering FlossBatteryProvider.";
  Reset();
}

void FlossBatteryProvider::RegisterAsBatteryProvider(
    const std::string& interface_name, bool available) {
  if (!available) {
    LOG(ERROR) << __func__
               << ": Failed waiting for btadapterd to become available.";
    return;
  }

  dbus::MethodCall method_call(
      battery_manager::kFlossBatteryProviderManagerInterface,
      kFlossBatteryProviderManagerRegisterBatteryProvider);
  dbus::MessageWriter writer(&method_call);
  writer.AppendObjectPath(dbus::ObjectPath(kPowerManagerServicePath));
  dbus_wrapper_->CallMethodAsync(
      provider_manager_object_proxy_, &method_call,
      kFlossBatteryProviderDBusTimeout,
      base::BindOnce(&FlossBatteryProvider::OnRegisteredAsBatteryProvider,
                     weak_ptr_factory_.GetWeakPtr()));
}

void FlossBatteryProvider::OnRegisteredAsBatteryProvider(
    dbus::Response* response) {
  if (!response) {
    LOG(ERROR) << __func__ << ": Failed to register as a battery provider.";
    return;
  }

  dbus::MessageReader reader(response);
  if (!reader.PopUint32(&battery_provider_id_)) {
    LOG(ERROR) << __func__ << ": Failed to receive a battery provider id.";
    return;
  }

  LOG(INFO) << __func__ << ": Registered as a battery provider with id: "
            << battery_provider_id_;
  is_registered_with_provider_manager_ = true;
}

// No-op
void FlossBatteryProvider::RefreshBatteryInfo(
    dbus::MethodCall* method_call,
    dbus::ExportedObject::ResponseSender response_sender) {}

}  // namespace power_manager::system
