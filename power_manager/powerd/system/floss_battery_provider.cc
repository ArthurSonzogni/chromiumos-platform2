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
  dbus_wrapper_->RegisterForServiceAvailability(
      bluetooth_manager_object_proxy_,
      base::BindOnce(&FlossBatteryProvider::RegisterBluetoothManagerCallback,
                     weak_ptr_factory_.GetWeakPtr()));
}

void FlossBatteryProvider::Reset() {}

void FlossBatteryProvider::UpdateDeviceBattery(const std::string& address,
                                               int level) {}

bool FlossBatteryProvider::IsRegistered() {
  return is_registered_with_bluetooth_manager_;
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
}

void FlossBatteryProvider::OnHciEnabledChanged(
    dbus::MethodCall* method_call,
    dbus::ExportedObject::ResponseSender response_sender) {
  LOG(INFO) << __func__ << ": HCI was enabled/disabled. Re-registering.";
  Reset();  // no-op
}

}  // namespace power_manager::system
