// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetchers/bluetooth_fetcher_floss.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/functional/bind.h>
#include <base/functional/callback.h>
#include <base/strings/string_number_conversions.h>
#include <brillo/variant_dictionary.h>
#include <dbus/object_path.h>

#include "diagnostics/cros_healthd/system/context.h"
#include "diagnostics/cros_healthd/system/floss_controller.h"
#include "diagnostics/cros_healthd/utils/callback_barrier.h"
#include "diagnostics/cros_healthd/utils/dbus_utils.h"
#include "diagnostics/cros_healthd/utils/error_utils.h"
#include "diagnostics/cros_healthd/utils/floss_utils.h"
#include "diagnostics/dbus_bindings/bluetooth_manager/dbus-proxies.h"
#include "diagnostics/dbus_bindings/floss/dbus-proxies.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

org::chromium::bluetooth::BluetoothProxyInterface* GetTargetAdapter(
    FlossController* floss_controller, int32_t hci_interface) {
  const auto target_adapter_path =
      dbus::ObjectPath("/org/chromium/bluetooth/hci" +
                       base::NumberToString(hci_interface) + "/adapter");
  for (const auto& adapter : floss_controller->GetAdapters()) {
    if (adapter && adapter->GetObjectPath() == target_adapter_path)
      return adapter;
  }
  LOG(ERROR) << "Failed to get target adapter for hci" << hci_interface;
  return nullptr;
}

org::chromium::bluetooth::BluetoothQAProxyInterface* GetTargetAdapterQA(
    FlossController* floss_controller, int32_t hci_interface) {
  const auto target_adapter_qa_path =
      dbus::ObjectPath("/org/chromium/bluetooth/hci" +
                       base::NumberToString(hci_interface) + "/qa");
  for (const auto& adapter_qa : floss_controller->GetAdapterQAs()) {
    if (adapter_qa && adapter_qa->GetObjectPath() == target_adapter_qa_path)
      return adapter_qa;
  }
  LOG(WARNING) << "Failed to get target adapter QA for hci" << hci_interface;
  return nullptr;
}

org::chromium::bluetooth::BluetoothAdminProxyInterface* GetTargetAdmin(
    FlossController* floss_controller, int32_t hci_interface) {
  const auto target_admin_path =
      dbus::ObjectPath("/org/chromium/bluetooth/hci" +
                       base::NumberToString(hci_interface) + "/admin");
  for (const auto& admin : floss_controller->GetAdmins()) {
    if (admin && admin->GetObjectPath() == target_admin_path)
      return admin;
  }
  LOG(WARNING) << "Failed to get target admin for hci" << hci_interface;
  return nullptr;
}

class State {
 public:
  explicit State(FlossController* floss_controller);
  State(const State&) = delete;
  State& operator=(const State&) = delete;
  ~State();

  void AddAdapterInfo(mojom::BluetoothAdapterInfoPtr adapter_info);

  void FetchEnabledAdapterInfo(
      mojom::BluetoothAdapterInfo* const adapter_info_ptr,
      CallbackBarrier& barrier,
      int32_t hci_interface);

  void HandleAdapterAddressResponse(
      mojom::BluetoothAdapterInfo* const adapter_info_ptr,
      brillo::Error* err,
      const std::string& address);
  void HandleAdapterNameResponse(
      mojom::BluetoothAdapterInfo* const adapter_info_ptr,
      brillo::Error* err,
      const std::string& name);
  void HandleAdapterDiscoveringResponse(
      mojom::BluetoothAdapterInfo* const adapter_info_ptr,
      brillo::Error* err,
      bool discovering);
  void HandleAdapterDiscoverableResponse(
      mojom::BluetoothAdapterInfo* const adapter_info_ptr,
      brillo::Error* err,
      bool discoverable);
  void HandleAdapterUuidsResponse(
      mojom::BluetoothAdapterInfo* const adapter_info_ptr,
      brillo::Error* err,
      const std::vector<std::vector<uint8_t>>& uuids);
  void HandleAdapterModaliasResponse(
      mojom::BluetoothAdapterInfo* const adapter_info_ptr,
      brillo::Error* err,
      const std::string& modalias);
  void HandleAdapterAllowedServicesResponse(
      mojom::BluetoothAdapterInfo* const adapter_info_ptr,
      brillo::Error* err,
      const std::vector<std::vector<uint8_t>>& services);
  void FetchConnectedDevicesInfo(
      mojom::BluetoothAdapterInfo* const adapter_info_ptr,
      brillo::Error* err,
      const std::vector<brillo::VariantDictionary>& devices);

  void HandleResult(FetchBluetoothInfoFromFlossCallback callback, bool success);

 private:
  FlossController* const floss_controller_;
  std::vector<mojom::BluetoothAdapterInfoPtr> adapter_infos_;
  mojom::ProbeErrorPtr error_ = nullptr;
};

State::State(FlossController* floss_controller)
    : floss_controller_(floss_controller) {
  CHECK(floss_controller_);
}

State::~State() = default;

void State::AddAdapterInfo(mojom::BluetoothAdapterInfoPtr adapter_info) {
  adapter_infos_.push_back(std::move(adapter_info));
}

void State::FetchEnabledAdapterInfo(
    mojom::BluetoothAdapterInfo* const adapter_info_ptr,
    CallbackBarrier& barrier,
    int32_t hci_interface) {
  auto target_adapter = GetTargetAdapter(floss_controller_, hci_interface);
  if (!target_adapter) {
    error_ = CreateAndLogProbeError(mojom::ErrorType::kServiceUnavailable,
                                    "Failed to get target adapter");
    return;
  }

  // Address.
  auto address_cb = SplitDbusCallback(
      barrier.Depend(base::BindOnce(&State::HandleAdapterAddressResponse,
                                    base::Unretained(this), adapter_info_ptr)));
  target_adapter->GetAddressAsync(std::move(address_cb.first),
                                  std::move(address_cb.second));
  // Name.
  auto name_cb = SplitDbusCallback(
      barrier.Depend(base::BindOnce(&State::HandleAdapterNameResponse,
                                    base::Unretained(this), adapter_info_ptr)));
  target_adapter->GetNameAsync(std::move(name_cb.first),
                               std::move(name_cb.second));
  // Discovering.
  auto discovering_cb = SplitDbusCallback(
      barrier.Depend(base::BindOnce(&State::HandleAdapterDiscoveringResponse,
                                    base::Unretained(this), adapter_info_ptr)));
  target_adapter->IsDiscoveringAsync(std::move(discovering_cb.first),
                                     std::move(discovering_cb.second));
  // Discoverable.
  auto discoverable_cb = SplitDbusCallback(
      barrier.Depend(base::BindOnce(&State::HandleAdapterDiscoverableResponse,
                                    base::Unretained(this), adapter_info_ptr)));
  target_adapter->GetDiscoverableAsync(std::move(discoverable_cb.first),
                                       std::move(discoverable_cb.second));
  // UUIDs.
  auto uuids_cb = SplitDbusCallback(
      barrier.Depend(base::BindOnce(&State::HandleAdapterUuidsResponse,
                                    base::Unretained(this), adapter_info_ptr)));
  target_adapter->GetUuidsAsync(std::move(uuids_cb.first),
                                std::move(uuids_cb.second));
  // Connected devices.
  auto devices_cb = SplitDbusCallback(
      barrier.Depend(base::BindOnce(&State::FetchConnectedDevicesInfo,
                                    base::Unretained(this), adapter_info_ptr)));
  target_adapter->GetConnectedDevicesAsync(std::move(devices_cb.first),
                                           std::move(devices_cb.second));
  // Modalias.
  auto target_adapter_qa = GetTargetAdapterQA(floss_controller_, hci_interface);
  if (target_adapter_qa) {
    auto modalias_cb = SplitDbusCallback(barrier.Depend(
        base::BindOnce(&State::HandleAdapterModaliasResponse,
                       base::Unretained(this), adapter_info_ptr)));
    target_adapter_qa->GetModaliasAsync(std::move(modalias_cb.first),
                                        std::move(modalias_cb.second));
  }
  // Service allow list.
  auto target_admin = GetTargetAdmin(floss_controller_, hci_interface);
  if (target_admin) {
    auto allowed_services_cb = SplitDbusCallback(barrier.Depend(
        base::BindOnce(&State::HandleAdapterAllowedServicesResponse,
                       base::Unretained(this), adapter_info_ptr)));
    target_admin->GetAllowedServicesAsync(
        std::move(allowed_services_cb.first),
        std::move(allowed_services_cb.second));
  }
}

void State::HandleAdapterAddressResponse(
    mojom::BluetoothAdapterInfo* const adapter_info_ptr,
    brillo::Error* err,
    const std::string& address) {
  if (err) {
    error_ = CreateAndLogProbeError(mojom::ErrorType::kSystemUtilityError,
                                    "Failed to get adapter address");
    return;
  }
  adapter_info_ptr->address = address;
}
void State::HandleAdapterNameResponse(
    mojom::BluetoothAdapterInfo* const adapter_info_ptr,
    brillo::Error* err,
    const std::string& name) {
  if (err) {
    error_ = CreateAndLogProbeError(mojom::ErrorType::kSystemUtilityError,
                                    "Failed to get adapter name");
    return;
  }
  adapter_info_ptr->name = name;
}

void State::HandleAdapterDiscoveringResponse(
    mojom::BluetoothAdapterInfo* const adapter_info_ptr,
    brillo::Error* err,
    bool discovering) {
  if (err) {
    error_ = CreateAndLogProbeError(mojom::ErrorType::kSystemUtilityError,
                                    "Failed to get adapter discovering");
    return;
  }
  adapter_info_ptr->discovering = discovering;
}

void State::HandleAdapterDiscoverableResponse(
    mojom::BluetoothAdapterInfo* const adapter_info_ptr,
    brillo::Error* err,
    bool discoverable) {
  if (err) {
    error_ = CreateAndLogProbeError(mojom::ErrorType::kSystemUtilityError,
                                    "Failed to get adapter discoverable");
    return;
  }
  adapter_info_ptr->discoverable = discoverable;
}

void State::HandleAdapterUuidsResponse(
    mojom::BluetoothAdapterInfo* const adapter_info_ptr,
    brillo::Error* err,
    const std::vector<std::vector<uint8_t>>& uuids) {
  if (err) {
    error_ = CreateAndLogProbeError(mojom::ErrorType::kSystemUtilityError,
                                    "Failed to get adapter UUIDs");
    return;
  }
  adapter_info_ptr->uuids = std::vector<std::string>{};
  for (auto uuid : uuids) {
    auto out_uuid = floss_utils::ParseUuidBytes(uuid);
    if (!out_uuid.has_value()) {
      error_ =
          CreateAndLogProbeError(mojom::ErrorType::kParseError,
                                 "Failed to parse UUID from adapter UUIDs");
      return;
    }
    adapter_info_ptr->uuids->push_back(out_uuid.value());
  }
}

void State::HandleAdapterModaliasResponse(
    mojom::BluetoothAdapterInfo* const adapter_info_ptr,
    brillo::Error* err,
    const std::string& modalias) {
  if (err) {
    error_ = CreateAndLogProbeError(mojom::ErrorType::kSystemUtilityError,
                                    "Failed to get adapter modalias");
    return;
  }
  adapter_info_ptr->modalias = modalias;
}

void State::HandleAdapterAllowedServicesResponse(
    mojom::BluetoothAdapterInfo* const adapter_info_ptr,
    brillo::Error* err,
    const std::vector<std::vector<uint8_t>>& services) {
  if (err) {
    error_ = CreateAndLogProbeError(mojom::ErrorType::kSystemUtilityError,
                                    "Failed to get adapter allowed services");
    return;
  }
  adapter_info_ptr->service_allow_list = std::vector<std::string>{};
  for (auto uuid : services) {
    auto out_uuid = floss_utils::ParseUuidBytes(uuid);
    if (!out_uuid.has_value()) {
      error_ =
          CreateAndLogProbeError(mojom::ErrorType::kParseError,
                                 "Failed to parse UUID from allowed services");
      return;
    }
    adapter_info_ptr->service_allow_list->push_back(out_uuid.value());
  }
}

void State::FetchConnectedDevicesInfo(
    mojom::BluetoothAdapterInfo* const adapter_info_ptr,
    brillo::Error* err,
    const std::vector<brillo::VariantDictionary>& devices) {
  if (err) {
    error_ = CreateAndLogProbeError(mojom::ErrorType::kSystemUtilityError,
                                    "Failed to get connected devices");
    return;
  }

  for (const auto& device : devices) {
    if (!device.contains("address") || !device.contains("name")) {
      error_ = CreateAndLogProbeError(mojom::ErrorType::kParseError,
                                      "Failed to parse connected devices");
      return;
    }
  }

  adapter_info_ptr->num_connected_devices = devices.size();
  for (const auto& device : devices) {
    auto address =
        brillo::GetVariantValueOrDefault<std::string>(device, "address");
    auto name = brillo::GetVariantValueOrDefault<std::string>(device, "name");
    auto device_info = mojom::BluetoothDeviceInfo::New();
    device_info->address = address;
    device_info->name = name;
    adapter_info_ptr->connected_devices->push_back(std::move(device_info));
  }

  // TODO(b/300239084): Fetch Bluetooth device info via Floss.
}

void State::HandleResult(FetchBluetoothInfoFromFlossCallback callback,
                         bool success) {
  if (!success) {
    std::move(callback).Run(mojom::BluetoothResult::NewError(
        CreateAndLogProbeError(mojom::ErrorType::kServiceUnavailable,
                               "Failed to finish all callbacks.")));
    return;
  }

  if (!error_.is_null()) {
    std::move(callback).Run(
        mojom::BluetoothResult::NewError(std::move(error_)));
    return;
  }

  std::move(callback).Run(mojom::BluetoothResult::NewBluetoothAdapterInfo(
      std::move(adapter_infos_)));
}

void FetchAvailableAdaptersInfo(
    FlossController* floss_controller,
    FetchBluetoothInfoFromFlossCallback callback,
    brillo::Error* err,
    const std::vector<brillo::VariantDictionary>& adapters) {
  if (err) {
    std::move(callback).Run(mojom::BluetoothResult::NewError(
        CreateAndLogProbeError(mojom::ErrorType::kSystemUtilityError,
                               "Failed to get available adapters")));
    return;
  }

  for (const auto& adapter : adapters) {
    if (!adapter.contains("enabled") || !adapter.contains("hci_interface")) {
      std::move(callback).Run(mojom::BluetoothResult::NewError(
          CreateAndLogProbeError(mojom::ErrorType::kParseError,
                                 "Failed to parse available adapters")));
      return;
    }
  }

  auto state = std::make_unique<State>(floss_controller);
  State* state_ptr = state.get();
  CallbackBarrier barrier{base::BindOnce(&State::HandleResult, std::move(state),
                                         std::move(callback))};

  for (const auto& adapter : adapters) {
    bool enabled = brillo::GetVariantValueOrDefault<bool>(adapter, "enabled");
    int32_t hci_interface =
        brillo::GetVariantValueOrDefault<int32_t>(adapter, "hci_interface");
    auto info = mojom::BluetoothAdapterInfo::New();
    info->powered = enabled;
    info->connected_devices = std::vector<mojom::BluetoothDeviceInfoPtr>{};
    if (enabled) {
      state_ptr->FetchEnabledAdapterInfo(info.get(), barrier, hci_interface);
    } else {
      // Report the default value since we can't access the adapter instance
      // when the powered is off.
      info->address = "";
      info->name = "hci" + base::NumberToString(hci_interface) + " (disabled)";
      info->discovering = false;
      info->discoverable = false;
      info->num_connected_devices = 0;
    }
    state_ptr->AddAdapterInfo(std::move(info));
  }
}

}  // namespace

void FetchBluetoothInfoFromFloss(Context* context,
                                 FetchBluetoothInfoFromFlossCallback callback) {
  CHECK(context);
  const auto floss_controller = context->floss_controller();
  CHECK(floss_controller);

  const auto manager = floss_controller->GetManager();
  if (!manager) {
    std::move(callback).Run(mojom::BluetoothResult::NewError(
        CreateAndLogProbeError(mojom::ErrorType::kServiceUnavailable,
                               "Floss proxy is not ready")));
    return;
  }

  auto [on_success, on_error] = SplitDbusCallback(base::BindOnce(
      &FetchAvailableAdaptersInfo, floss_controller, std::move(callback)));
  manager->GetAvailableAdaptersAsync(std::move(on_success),
                                     std::move(on_error));
}

}  // namespace diagnostics
