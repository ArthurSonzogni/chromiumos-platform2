// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetchers/bluetooth_fetcher.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/check.h>
#include <dbus/object_path.h>

namespace diagnostics {

namespace {

namespace mojo_ipc = ::ash::cros_healthd::mojom;

constexpr char kBluetoothTypeBrEdrName[] = "BR/EDR";
constexpr char kBluetoothTypeLeName[] = "LE";
constexpr char kBluetoothTypeDualName[] = "DUAL";

constexpr char kSupportedCapabilitiesMaxAdvLenKey[] = "MaxAdvLen";
constexpr char kSupportedCapabilitiesMaxScnRspLenKey[] = "MaxScnRspLen";
constexpr char kSupportedCapabilitiesMinTxPowerKey[] = "MinTxPower";
constexpr char kSupportedCapabilitiesMaxTxPowerKey[] = "MaxTxPower";

}  // namespace

mojo_ipc::BluetoothResultPtr BluetoothFetcher::FetchBluetoothInfo(
    std::unique_ptr<BluezInfoManager> bluez_manager) {
  if (!bluez_manager) {
    bluez_manager = BluezInfoManager::Create(context_->bluetooth_proxy());
  }
  return bluez_manager->ParseBluetoothInstance();
}

std::unique_ptr<BluezInfoManager> BluezInfoManager::Create(
    org::bluezProxy* bluetooth_proxy) {
  std::unique_ptr<BluezInfoManager> bluez_manager(new BluezInfoManager());

  bluez_manager->adapters_ = bluetooth_proxy->GetAdapter1Instances();
  bluez_manager->devices_ = bluetooth_proxy->GetDevice1Instances();
  bluez_manager->admin_policies_ =
      bluetooth_proxy->GetAdminPolicyStatus1Instances();
  bluez_manager->advertisings_ =
      bluetooth_proxy->GetLEAdvertisingManager1Instances();
  bluez_manager->batteries_ = bluetooth_proxy->GetBattery1Instances();
  return bluez_manager;
}

std::vector<org::bluez::Adapter1ProxyInterface*> BluezInfoManager::adapters() {
  return adapters_;
}
std::vector<org::bluez::Device1ProxyInterface*> BluezInfoManager::devices() {
  return devices_;
}
std::vector<org::bluez::AdminPolicyStatus1ProxyInterface*>
BluezInfoManager::admin_policies() {
  return admin_policies_;
}
std::vector<org::bluez::LEAdvertisingManager1ProxyInterface*>
BluezInfoManager::advertisings() {
  return advertisings_;
}
std::vector<org::bluez::Battery1ProxyInterface*> BluezInfoManager::batteries() {
  return batteries_;
}

mojo_ipc::BluetoothResultPtr BluezInfoManager::ParseBluetoothInstance() {
  std::vector<mojo_ipc::BluetoothAdapterInfoPtr> adapter_infos;

  // Map from the adapter's ObjectPath to the service allow list.
  std::map<dbus::ObjectPath, std::vector<std::string>> service_allow_list;
  ParseServiceAllowList(service_allow_list);

  // Map from the adapter's ObjectPath to the supported capabilities.
  std::map<dbus::ObjectPath, mojo_ipc::SupportedCapabilitiesPtr>
      supported_capabilities;
  ParseSupportedCapabilities(supported_capabilities);

  // Map from the adapter's ObjectPath to the connected devices.
  std::map<dbus::ObjectPath, std::vector<mojo_ipc::BluetoothDeviceInfoPtr>>
      connected_devices;
  ParseDevicesInfo(connected_devices);

  // Fetch adapters' info.
  for (const auto& adapter : adapters()) {
    if (!adapter)
      continue;
    mojo_ipc::BluetoothAdapterInfo info;

    info.name = adapter->name();
    info.address = adapter->address();
    info.powered = adapter->powered();
    info.discoverable = adapter->discoverable();
    info.discovering = adapter->discovering();
    info.uuids = adapter->uuids();
    info.modalias = adapter->modalias();

    const auto adapter_path = adapter->GetObjectPath();
    const auto it_connected_device = connected_devices.find(adapter_path);
    if (it_connected_device != connected_devices.end()) {
      info.num_connected_devices = it_connected_device->second.size();
      info.connected_devices = std::move(it_connected_device->second);
    }

    const auto it_service_allow_list = service_allow_list.find(adapter_path);
    if (it_service_allow_list != service_allow_list.end()) {
      info.service_allow_list = it_service_allow_list->second;
    }

    const auto it_capabilities = supported_capabilities.find(adapter_path);
    if (it_capabilities != supported_capabilities.end()) {
      info.supported_capabilities = std::move(it_capabilities->second);
    }
    adapter_infos.push_back(info.Clone());
  }

  return mojo_ipc::BluetoothResult::NewBluetoothAdapterInfo(
      std::move(adapter_infos));
}

void BluezInfoManager::ParseDevicesInfo(
    std::map<dbus::ObjectPath, std::vector<mojo_ipc::BluetoothDeviceInfoPtr>>&
        out_connected_devices) {
  // Map from the device's ObjectPath to the battery percentage.
  std::map<dbus::ObjectPath, uint8_t> battery_percentage;
  ParseBatteryPercentage(battery_percentage);

  for (const auto& device : devices()) {
    if (!device || !device->connected())
      continue;

    mojo_ipc::BluetoothDeviceInfo info;
    info.address = device->address();
    info.name = device->name();
    info.type = GetDeviceType(device->type());
    info.appearance = mojo_ipc::NullableUint16::New(device->appearance());
    info.modalias = device->modalias();
    info.rssi = mojo_ipc::NullableInt16::New(device->rssi());
    info.mtu = mojo_ipc::NullableUint16::New(device->mtu());
    info.uuids = device->uuids();

    const auto it = battery_percentage.find(device->GetObjectPath());
    if (it != battery_percentage.end()) {
      info.battery_percentage = mojo_ipc::NullableUint8::New(it->second);
    }

    out_connected_devices[device->adapter()].push_back(info.Clone());
  }
}

mojo_ipc::BluetoothDeviceType BluezInfoManager::GetDeviceType(
    const std::string& type) {
  if (type == kBluetoothTypeBrEdrName)
    return mojo_ipc::BluetoothDeviceType::kBrEdr;
  else if (type == kBluetoothTypeLeName)
    return mojo_ipc::BluetoothDeviceType::kLe;
  else if (type == kBluetoothTypeDualName)
    return mojo_ipc::BluetoothDeviceType::kDual;
  return mojo_ipc::BluetoothDeviceType::kUnfound;
}

void BluezInfoManager::ParseServiceAllowList(
    std::map<dbus::ObjectPath, std::vector<std::string>>&
        out_service_allow_list) {
  for (const auto& policy : admin_policies()) {
    out_service_allow_list[policy->GetObjectPath()] =
        policy->service_allow_list();
  }
}

void BluezInfoManager::ParseSupportedCapabilities(
    std::map<dbus::ObjectPath, mojo_ipc::SupportedCapabilitiesPtr>&
        out_supported_capabilities) {
  for (const auto& advertising : advertisings()) {
    auto data = advertising->supported_capabilities();
    // Drop data if missing any element.
    if (data.find(kSupportedCapabilitiesMaxAdvLenKey) == data.end() ||
        data.find(kSupportedCapabilitiesMaxScnRspLenKey) == data.end() ||
        data.find(kSupportedCapabilitiesMinTxPowerKey) == data.end() ||
        data.find(kSupportedCapabilitiesMaxTxPowerKey) == data.end()) {
      continue;
    }
    mojo_ipc::SupportedCapabilities info;
    info.max_adv_len = brillo::GetVariantValueOrDefault<uint8_t>(
        data, kSupportedCapabilitiesMaxAdvLenKey);
    info.max_scn_rsp_len = brillo::GetVariantValueOrDefault<uint8_t>(
        data, kSupportedCapabilitiesMaxScnRspLenKey);
    info.min_tx_power = brillo::GetVariantValueOrDefault<int16_t>(
        data, kSupportedCapabilitiesMinTxPowerKey);
    info.max_tx_power = brillo::GetVariantValueOrDefault<int16_t>(
        data, kSupportedCapabilitiesMaxTxPowerKey);
    out_supported_capabilities[advertising->GetObjectPath()] = info.Clone();
  }
}

void BluezInfoManager::ParseBatteryPercentage(
    std::map<dbus::ObjectPath, uint8_t>& out_battery_percentage) {
  for (const auto& battery : batteries()) {
    out_battery_percentage[battery->GetObjectPath()] = battery->percentage();
  }
}
}  // namespace diagnostics
