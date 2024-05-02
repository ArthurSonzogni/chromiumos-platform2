// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <brillo/dbus/data_serialization.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/message.h>

#include "power_manager/powerd/system/floss_battery_provider.h"

namespace power_manager::system {

namespace {

// Timeout for DBus requests.
constexpr base::TimeDelta kFlossBatteryProviderDBusTimeout = base::Seconds(2);

// The source of this battery provider (HID profile).
constexpr char kSourceInfo[] = "HID";
// Random UUID which acts as a unique tag for this source.
constexpr char kBatteryProviderUuid[] = "6cb01dc5-326f-4e31-b06f-126fce10b3ff";

// Helper function to write nested DBus messages.
template <typename T>
void AppendValueToWriterAsDictEntry(dbus::MessageWriter& dict_writer,
                                    const char* key,
                                    T value) {
  dbus::MessageWriter entry_writer(nullptr);
  dict_writer.OpenDictEntry(&entry_writer);
  entry_writer.AppendString((std::string)key);
  brillo::dbus_utils::AppendValueToWriterAsVariant(&entry_writer, value);
  dict_writer.CloseContainer(&entry_writer);
}

// Create a Battery object.
void CreateBatteryObject(dbus::MessageWriter& battery_set_writer, int level) {
  dbus::MessageWriter dict_writer(nullptr);
  dbus::MessageWriter variant_writer(nullptr);
  dbus::MessageWriter battery_writer(nullptr);
  dbus::MessageWriter array_writer(nullptr);

  battery_set_writer.OpenDictEntry(&dict_writer);
  dict_writer.AppendString((std::string) "batteries");
  dict_writer.OpenVariant("aa{sv}", &variant_writer);
  variant_writer.OpenArray("a{sv}", &array_writer);
  array_writer.OpenArray("{sv}", &battery_writer);
  AppendValueToWriterAsDictEntry(battery_writer, "percentage", (uint32_t)level);
  AppendValueToWriterAsDictEntry(battery_writer, "variant", "");

  // Close objects.
  array_writer.CloseContainer(&battery_writer);
  variant_writer.CloseContainer(&array_writer);
  dict_writer.CloseContainer(&variant_writer);
  battery_set_writer.CloseContainer(&dict_writer);
}

// Create a BatterySet object.
void CreateBatterySet(dbus::MessageWriter& writer,
                      std::string address,
                      int level) {
  dbus::MessageWriter battery_set_writer(nullptr);

  writer.OpenArray("{sv}", &battery_set_writer);
  AppendValueToWriterAsDictEntry(battery_set_writer, "address",
                                 address.c_str());
  AppendValueToWriterAsDictEntry(battery_set_writer, "source_uuid",
                                 kBatteryProviderUuid);
  AppendValueToWriterAsDictEntry(battery_set_writer, "source_info",
                                 kSourceInfo);
  CreateBatteryObject(battery_set_writer, level);
  writer.CloseContainer(&battery_set_writer);
}

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
  UnregisterAsBatteryProvider();
  dbus_wrapper_->RegisterForInterfaceAvailability(
      provider_manager_object_manager_,
      battery_manager::kFlossBatteryProviderManagerInterface,
      base::BindRepeating(&FlossBatteryProvider::RegisterAsBatteryProvider,
                          weak_ptr_factory_.GetWeakPtr()));
}

void FlossBatteryProvider::UpdateDeviceBattery(const std::string& address,
                                               int level) {
  if (!IsRegistered()) {
    return;
  }
  if (level < 0 || level > 100) {
    LOG(WARNING) << __func__ << ": Ignoring invalid battery level '" << level
                 << "' for address '" << address << "'";
    return;
  }
  if (level == 0) {
    // Some peripherals use 0 to indicate full charge (b/336978853)
    LOG(INFO) << __func__ << ": '" << address
              << "' battery level is 0, but sending 100";
    level = 100;
  }

  dbus::MethodCall method_call(
      battery_manager::kFlossBatteryProviderManagerInterface,
      kFlossBatteryProviderManagerUpdateDeviceBattery);
  dbus::MessageWriter writer(&method_call);
  writer.AppendUint32(battery_provider_id_);

  CreateBatterySet(writer, address, level);

  dbus_wrapper_->CallMethodAsync(
      provider_manager_object_proxy_, &method_call,
      kFlossBatteryProviderDBusTimeout,
      base::BindRepeating(&FlossBatteryProvider::OnUpdateDeviceBatteryResponse,
                          weak_ptr_factory_.GetWeakPtr()));
}

void FlossBatteryProvider::OnUpdateDeviceBatteryResponse(
    dbus::Response* response) {
  if (!response) {
    LOG(ERROR) << __func__ << ": Failed to send updated battery info.";
  }
}

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

void FlossBatteryProvider::UnregisterAsBatteryProvider() {
  if (!is_registered_with_provider_manager_) {
    return;
  }
  dbus::MethodCall method_call(
      battery_manager::kFlossBatteryProviderManagerInterface,
      kFlossBatteryProviderManagerUnregisterBatteryProvider);
  dbus::MessageWriter writer(&method_call);
  writer.AppendUint32(battery_provider_id_);
  dbus_wrapper_->CallMethodAsync(
      provider_manager_object_proxy_, &method_call,
      kFlossBatteryProviderDBusTimeout,
      base::BindOnce(&FlossBatteryProvider::OnUnregisteredAsBatteryProvider,
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

void FlossBatteryProvider::OnUnregisteredAsBatteryProvider(
    dbus::Response* response) {
  is_registered_with_provider_manager_ = false;
}

// No-op
void FlossBatteryProvider::RefreshBatteryInfo(
    dbus::MethodCall* method_call,
    dbus::ExportedObject::ResponseSender response_sender) {}

}  // namespace power_manager::system
