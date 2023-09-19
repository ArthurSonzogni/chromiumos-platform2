// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetchers/bluetooth_fetcher.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

#include <base/check.h>
#include <dbus/object_path.h>

#include "diagnostics/cros_healthd/system/bluez_controller.h"
#include "diagnostics/cros_healthd/system/floss_controller.h"
#include "diagnostics/cros_healthd/utils/dbus_utils.h"
#include "diagnostics/cros_healthd/utils/error_utils.h"
#include "diagnostics/dbus_bindings/bluetooth_manager/dbus-proxies.h"
#include "diagnostics/dbus_bindings/bluez/dbus-proxies.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

constexpr char kBluetoothTypeBrEdrName[] = "BR/EDR";
constexpr char kBluetoothTypeLeName[] = "LE";
constexpr char kBluetoothTypeDualName[] = "DUAL";

// Convert std::string to |BluetoothDeviceType| enum.
mojom::BluetoothDeviceType GetDeviceType(const std::string& type) {
  if (type == kBluetoothTypeBrEdrName)
    return mojom::BluetoothDeviceType::kBrEdr;
  else if (type == kBluetoothTypeLeName)
    return mojom::BluetoothDeviceType::kLe;
  else if (type == kBluetoothTypeDualName)
    return mojom::BluetoothDeviceType::kDual;
  return mojom::BluetoothDeviceType::kUnfound;
}

// Parse Bluetooth info from AdminPolicyStatus1 interface and store in the map.
void ParseServiceAllowList(
    std::vector<org::bluez::AdminPolicyStatus1ProxyInterface*> admin_policies,
    std::map<dbus::ObjectPath, std::vector<std::string>>&
        out_service_allow_list) {
  for (const auto& policy : admin_policies) {
    out_service_allow_list[policy->GetObjectPath()] =
        policy->service_allow_list();
  }
}

// Parse Bluetooth info from Battery1 interface and store in the map.
void ParseBatteryPercentage(
    std::vector<org::bluez::Battery1ProxyInterface*> batteries,
    std::map<dbus::ObjectPath, uint8_t>& out_battery_percentage) {
  for (const auto& battery : batteries) {
    out_battery_percentage[battery->GetObjectPath()] = battery->percentage();
  }
}

// Parse Bluetooth info from Device1 interface and store in the map.
void ParseDevicesInfo(
    std::vector<org::bluez::Device1ProxyInterface*> devices,
    std::vector<org::bluez::Battery1ProxyInterface*> batteries,
    std::map<dbus::ObjectPath, std::vector<mojom::BluetoothDeviceInfoPtr>>&
        out_connected_devices) {
  // Map from the device's ObjectPath to the battery percentage.
  std::map<dbus::ObjectPath, uint8_t> battery_percentage;
  ParseBatteryPercentage(batteries, battery_percentage);

  for (const auto& device : devices) {
    if (!device || !device->connected())
      continue;

    mojom::BluetoothDeviceInfo info;
    info.address = device->address();

    // The following are optional device properties.
    if (device->is_name_valid())
      info.name = device->name();
    if (device->is_type_valid())
      info.type = GetDeviceType(device->type());
    else
      info.type = mojom::BluetoothDeviceType::kUnfound;
    if (device->is_appearance_valid())
      info.appearance = mojom::NullableUint16::New(device->appearance());
    if (device->is_modalias_valid())
      info.modalias = device->modalias();
    if (device->is_rssi_valid())
      info.rssi = mojom::NullableInt16::New(device->rssi());
    if (device->is_mtu_valid())
      info.mtu = mojom::NullableUint16::New(device->mtu());
    if (device->is_uuids_valid())
      info.uuids = device->uuids();
    if (device->is_bluetooth_class_valid())
      info.bluetooth_class =
          mojom::NullableUint32::New(device->bluetooth_class());

    const auto it = battery_percentage.find(device->GetObjectPath());
    if (it != battery_percentage.end()) {
      info.battery_percentage = mojom::NullableUint8::New(it->second);
    }

    out_connected_devices[device->adapter()].push_back(info.Clone());
  }
}

void FetchBluetoothInfoFromBluez(Context* context,
                                 FetchBluetoothInfoCallback callback) {
  const auto bluez_controller = context->bluez_controller();
  if (!bluez_controller) {
    std::move(callback).Run(mojom::BluetoothResult::NewError(
        CreateAndLogProbeError(mojom::ErrorType::kServiceUnavailable,
                               "Bluez proxy is not ready")));
    return;
  }
  std::vector<mojom::BluetoothAdapterInfoPtr> adapter_infos;

  // Map from the adapter's ObjectPath to the service allow list.
  std::map<dbus::ObjectPath, std::vector<std::string>> service_allow_list;
  ParseServiceAllowList(bluez_controller->GetAdminPolicies(),
                        service_allow_list);

  // Map from the adapter's ObjectPath to the connected devices.
  std::map<dbus::ObjectPath, std::vector<mojom::BluetoothDeviceInfoPtr>>
      connected_devices;
  ParseDevicesInfo(bluez_controller->GetDevices(),
                   bluez_controller->GetBatteries(), connected_devices);

  // Fetch adapters' info.
  for (const auto& adapter : bluez_controller->GetAdapters()) {
    if (!adapter)
      continue;
    mojom::BluetoothAdapterInfo info;

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
    adapter_infos.push_back(info.Clone());
  }

  std::move(callback).Run(mojom::BluetoothResult::NewBluetoothAdapterInfo(
      std::move(adapter_infos)));
}

void CheckBluetoothStack(Context* context,
                         FetchBluetoothInfoCallback callback,
                         brillo::Error* err,
                         bool floss_enabled) {
  if (err) {
    std::move(callback).Run(mojom::BluetoothResult::NewError(
        CreateAndLogProbeError(mojom::ErrorType::kSystemUtilityError,
                               "Failed to get floss enabled state")));
    return;
  }

  if (floss_enabled) {
    // TODO(b/300007763): Support Bluetooth telemetry via Floss.
    std::move(callback).Run(
        mojom::BluetoothResult::NewBluetoothAdapterInfo({}));
    return;
  }

  FetchBluetoothInfoFromBluez(context, std::move(callback));
}

}  // namespace

void FetchBluetoothInfo(Context* context, FetchBluetoothInfoCallback callback) {
  const auto floss_controller = context->floss_controller();
  if (!floss_controller) {
    std::move(callback).Run(mojom::BluetoothResult::NewError(
        CreateAndLogProbeError(mojom::ErrorType::kServiceUnavailable,
                               "Floss proxy is not ready")));
    return;
  }

  const auto manager = floss_controller->GetManager();
  if (!manager) {
    // Floss is not installed on device with 2GiB rootfs, which will always use
    // Bluez as Bluetooth stack.
    FetchBluetoothInfoFromBluez(context, std::move(callback));
    return;
  }

  auto [on_success, on_error] = SplitDbusCallback(
      base::BindOnce(&CheckBluetoothStack, context, std::move(callback)));
  manager->GetFlossEnabledAsync(std::move(on_success), std::move(on_error));
}

}  // namespace diagnostics
