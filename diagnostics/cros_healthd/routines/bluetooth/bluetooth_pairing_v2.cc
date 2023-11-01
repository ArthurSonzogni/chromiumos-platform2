// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/bluetooth/bluetooth_pairing_v2.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/functional/callback.h>
#include <base/hash/hash.h>
#include <base/memory/ptr_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/task/single_thread_task_runner.h>
#include <base/time/time.h>
#include <base/types/expected.h>
#include <base/uuid.h>
#include <brillo/errors/error.h>
#include <brillo/variant_dictionary.h>

#include "diagnostics/cros_healthd/routines/bluetooth/address_utils.h"
#include "diagnostics/cros_healthd/routines/bluetooth/bluetooth_constants.h"
#include "diagnostics/cros_healthd/system/context.h"
#include "diagnostics/cros_healthd/system/floss_controller.h"
#include "diagnostics/cros_healthd/system/floss_event_hub.h"
#include "diagnostics/cros_healthd/utils/callback_barrier.h"
#include "diagnostics/cros_healthd/utils/dbus_utils.h"
#include "diagnostics/cros_healthd/utils/floss_utils.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

// Raw value of address type, which is referenced from |BtAddrType| enum in the
// Android codebase:
// packages/modules/Bluetooth/system/gd/rust/topshim/src/btif.rs
namespace address_type {
constexpr uint32_t kPublic = 0;
constexpr uint32_t kRandom = 1;
}  // namespace address_type

void RemoveTargetPeripheral(FlossController* floss_controller,
                            int32_t hci_interface,
                            brillo::VariantDictionary device) {
  auto adapter_path =
      dbus::ObjectPath("/org/chromium/bluetooth/hci" +
                       base::NumberToString(hci_interface) + "/adapter");
  for (const auto& adapter : floss_controller->GetAdapters()) {
    if (adapter && adapter->GetObjectPath() == adapter_path) {
      adapter->RemoveBondAsync(device, base::DoNothing(), base::DoNothing());
      return;
    }
  }
}

// Convert address type raw value from Floss to mojom enum.
mojom::BluetoothPairingPeripheralInfo_AddressType GetAddressTypeEnum(
    uint32_t address_type) {
  if (address_type == address_type::kPublic) {
    return mojom::BluetoothPairingPeripheralInfo_AddressType::kPublic;
  } else if (address_type == address_type::kRandom) {
    return mojom::BluetoothPairingPeripheralInfo_AddressType::kRandom;
  } else {
    LOG(WARNING) << "Get unknown address type value: " << address_type;
    return mojom::BluetoothPairingPeripheralInfo_AddressType::kUnknown;
  }
}

// Convert address type raw value from Floss to string for address validation.
std::string GetAddressTypeString(uint32_t address_type) {
  if (address_type == address_type::kPublic) {
    return "public";
  } else if (address_type == address_type::kRandom) {
    return "random";
  } else {
    return "unknown";
  }
}

}  // namespace

BluetoothPairingRoutineV2::BluetoothPairingRoutineV2(
    Context* context, const mojom::BluetoothPairingRoutineArgumentPtr& arg)
    : BluetoothRoutineBaseV2(context), peripheral_id_(arg->peripheral_id) {
  CHECK(context_);

  routine_output_ = mojom::BluetoothPairingRoutineDetail::New();
}

BluetoothPairingRoutineV2::~BluetoothPairingRoutineV2() = default;

void BluetoothPairingRoutineV2::OnStart() {
  CHECK(step_ == TestStep::kInitialize);
  SetRunningState();

  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&BluetoothPairingRoutineV2::OnTimeoutOccurred,
                     weak_ptr_factory_.GetWeakPtr()),
      kPairingRoutineTimeout);

  // Used to scan the target peripheral.
  event_subscriptions_.push_back(
      context_->floss_event_hub()->SubscribeDeviceAdded(
          base::BindRepeating(&BluetoothPairingRoutineV2::OnDeviceAdded,
                              weak_ptr_factory_.GetWeakPtr())));
  event_subscriptions_.push_back(
      context_->floss_event_hub()->SubscribeDevicePropertyChanged(
          base::BindRepeating(
              &BluetoothPairingRoutineV2::OnDevicePropertyChanged,
              weak_ptr_factory_.GetWeakPtr())));
  // Used to observe device connection and bonded status.
  event_subscriptions_.push_back(
      context_->floss_event_hub()->SubscribeDeviceConnectedChanged(
          base::BindRepeating(
              &BluetoothPairingRoutineV2::OnDeviceConnectedChanged,
              weak_ptr_factory_.GetWeakPtr())));
  event_subscriptions_.push_back(
      context_->floss_event_hub()->SubscribeDeviceBondChanged(
          base::BindRepeating(&BluetoothPairingRoutineV2::OnDeviceBondChanged,
                              weak_ptr_factory_.GetWeakPtr())));
  event_subscriptions_.push_back(
      context_->floss_event_hub()->SubscribeDeviceSspRequest(
          base::BindRepeating(&BluetoothPairingRoutineV2::OnDeviceSspRequest,
                              weak_ptr_factory_.GetWeakPtr())));

  Initialize(base::BindOnce(&BluetoothPairingRoutineV2::HandleInitializeResult,
                            weak_ptr_factory_.GetWeakPtr()));
}

void BluetoothPairingRoutineV2::HandleInitializeResult(bool success) {
  if (!success) {
    SetResultAndStop(
        base::unexpected("Failed to initialize Bluetooth routine."));
    return;
  }
  RunNextStep();
}

org::chromium::bluetooth::BluetoothProxyInterface*
BluetoothPairingRoutineV2::GetDefaultAdapterOrStop() {
  auto adapter = GetDefaultAdapter();
  if (!adapter) {
    SetResultAndStop(base::unexpected("Failed to get default adapter."));
    return nullptr;
  }
  return adapter;
}

void BluetoothPairingRoutineV2::RunNextStep() {
  step_ = static_cast<TestStep>(static_cast<int32_t>(step_) + 1);
  UpdatePercentage();

  switch (step_) {
    case TestStep::kInitialize:
      SetResultAndStop(base::unexpected(kBluetoothRoutineUnexpectedFlow));
      break;
    case TestStep::kPreCheckDiscovery:
      RunPreCheck(
          base::BindOnce(&BluetoothPairingRoutineV2::HandlePreCheckResponse,
                         weak_ptr_factory_.GetWeakPtr()));
      break;
    case TestStep::kEnsurePoweredOn:
      if (GetAdapterInitialPoweredState()) {
        RunNextStep();
        return;
      }
      ChangeAdapterPoweredState(
          /*powered=*/true,
          base::BindOnce(
              &BluetoothPairingRoutineV2::HandleEnsurePoweredOnResponse,
              weak_ptr_factory_.GetWeakPtr()));
      break;
    case TestStep::kCheckBondedDevices:
      if (auto adapter = GetDefaultAdapterOrStop(); adapter != nullptr) {
        auto [on_success, on_error] = SplitDbusCallback(base::BindOnce(
            &BluetoothPairingRoutineV2::CheckTargetPeripheralBonded,
            weak_ptr_factory_.GetWeakPtr()));
        adapter->GetBondedDevicesAsync(std::move(on_success),
                                       std::move(on_error));
      }
      break;
    case TestStep::kStartDiscovery:
      if (auto adapter = GetDefaultAdapterOrStop(); adapter != nullptr) {
        SetupStopDiscoveryJob();
        auto [on_success, on_error] = SplitDbusCallback(base::BindOnce(
            &BluetoothPairingRoutineV2::HandleUpdateDiscoveryResponse,
            weak_ptr_factory_.GetWeakPtr()));
        adapter->StartDiscoveryAsync(std::move(on_success),
                                     std::move(on_error));
      }
      break;
    case TestStep::kScanTargetDevice:
      // Wait for the target peripheral to be scanned in |OnDeviceAdded|.
      break;
    case TestStep::kTagTargetDevice:
      if (auto adapter = GetDefaultAdapterOrStop(); adapter != nullptr) {
        auto [on_success, on_error] = SplitDbusCallback(base::BindOnce(
            &BluetoothPairingRoutineV2::HandleUpdateAliasResponse,
            weak_ptr_factory_.GetWeakPtr()));
        adapter->SetRemoteAliasAsync(
            target_device_, kHealthdBluetoothDiagnosticsTag,
            std::move(on_success), std::move(on_error));
      }
      break;
    case TestStep::kCollectDeviceInfo:
      GetDeviceProperties();
      break;
    case TestStep::kBondTargetDevice:
      if (auto adapter = GetDefaultAdapterOrStop(); adapter != nullptr) {
        remove_target_peripheral_ = base::ScopedClosureRunner(base::BindOnce(
            &RemoveTargetPeripheral, context_->floss_controller(),
            default_adapter_hci_, target_device_));
        // Waiting for the device connected event.
        routine_output_->pairing_peripheral->connect_error = mojom::
            BluetoothPairingPeripheralInfo_ConnectError::kNoConnectedEvent;

        auto [on_success, on_error] = SplitDbusCallback(
            base::BindOnce(&BluetoothPairingRoutineV2::HandleBondDeviceResponse,
                           weak_ptr_factory_.GetWeakPtr()));
        // `in_transport` is 0 for Auto.
        adapter->CreateBondAsync(target_device_, /*in_transport=*/0,
                                 std::move(on_success), std::move(on_error));
      }
      break;
    case TestStep::kResetDeviceTag:
      if (auto adapter = GetDefaultAdapterOrStop(); adapter != nullptr) {
        auto [on_success, on_error] = SplitDbusCallback(base::BindOnce(
            &BluetoothPairingRoutineV2::HandleUpdateAliasResponse,
            weak_ptr_factory_.GetWeakPtr()));
        adapter->SetRemoteAliasAsync(target_device_, /*in_alias=*/"",
                                     std::move(on_success),
                                     std::move(on_error));
      }
      break;
    case TestStep::kRemoveTargetDevice:
      if (auto adapter = GetDefaultAdapterOrStop(); adapter != nullptr) {
        remove_target_peripheral_.ReplaceClosure(base::DoNothing());
        auto [on_success, on_error] = SplitDbusCallback(
            base::BindOnce(&BluetoothPairingRoutineV2::HandleRemoveBondResponse,
                           weak_ptr_factory_.GetWeakPtr()));
        adapter->RemoveBondAsync(target_device_, std::move(on_success),
                                 std::move(on_error));
      }
      break;
    case TestStep::kCancelDiscovery:
      if (auto adapter = GetDefaultAdapterOrStop(); adapter != nullptr) {
        adapter_stop_discovery_.ReplaceClosure(base::DoNothing());
        auto [on_success, on_error] = SplitDbusCallback(base::BindOnce(
            &BluetoothPairingRoutineV2::HandleUpdateDiscoveryResponse,
            weak_ptr_factory_.GetWeakPtr()));
        adapter->CancelDiscoveryAsync(std::move(on_success),
                                      std::move(on_error));
      }
      break;
    case TestStep::kComplete:
      SetResultAndStop(/*result=*/base::ok(true));
      break;
  }
}

void BluetoothPairingRoutineV2::HandlePreCheckResponse(
    std::optional<std::string> error) {
  if (error.has_value()) {
    SetResultAndStop(base::unexpected(error.value()));
    return;
  }
  RunNextStep();
}

void BluetoothPairingRoutineV2::HandleEnsurePoweredOnResponse(
    const base::expected<bool, std::string>& result) {
  if (!result.has_value() || !result.value()) {
    SetResultAndStop(
        base::unexpected("Failed to ensure default adapter is powered on."));
    return;
  }
  RunNextStep();
}

void BluetoothPairingRoutineV2::CheckTargetPeripheralBonded(
    brillo::Error* error,
    const std::vector<brillo::VariantDictionary>& devices) {
  CHECK(step_ == TestStep::kCheckBondedDevices);
  if (error) {
    SetResultAndStop(base::unexpected("Failed to get bonded devices."));
    return;
  }
  for (const auto& device : devices) {
    if (!device.contains("name") || !device.contains("address")) {
      SetResultAndStop(base::unexpected("Failed to parse device info."));
      return;
    }

    auto address =
        brillo::GetVariantValueOrDefault<std::string>(device, "address");
    if (peripheral_id_ == base::NumberToString(base::FastHash(address))) {
      SetResultAndStop(
          base::unexpected("The target peripheral is already paired."));
      return;
    }
  }
  RunNextStep();
}

void BluetoothPairingRoutineV2::HandleUpdateDiscoveryResponse(
    brillo::Error* error, bool is_success) {
  CHECK(step_ == TestStep::kStartDiscovery ||
        step_ == TestStep::kCancelDiscovery);
  if (error || !is_success) {
    SetResultAndStop(base::unexpected("Failed to update discovery mode."));
    return;
  }
  RunNextStep();
}

void BluetoothPairingRoutineV2::OnDeviceAdded(
    const brillo::VariantDictionary& device) {
  if (step_ != TestStep::kScanTargetDevice)
    return;

  if (!device.contains("name") || !device.contains("address")) {
    SetResultAndStop(base::unexpected("Failed to parse device info."));
    return;
  }

  auto address =
      brillo::GetVariantValueOrDefault<std::string>(device, "address");
  if (base::NumberToString(base::FastHash(address)) != peripheral_id_) {
    return;
  }
  // Copy the device dictionary.
  target_device_ = device;

  // Prepare the routine output when the target peripheral is found.
  routine_output_->pairing_peripheral =
      mojom::BluetoothPairingPeripheralInfo::New();
  routine_output_->pairing_peripheral->connect_error =
      mojom::BluetoothPairingPeripheralInfo_ConnectError::kNone;
  routine_output_->pairing_peripheral->pair_error =
      mojom::BluetoothPairingPeripheralInfo_PairError::kNone;
  RunNextStep();
}

void BluetoothPairingRoutineV2::OnDevicePropertyChanged(
    const brillo::VariantDictionary& device, BtPropertyType property) {
  // Check the device property changed event in case that the device is cached
  // and the device added event is missing.
  OnDeviceAdded(device);
}

void BluetoothPairingRoutineV2::HandleUpdateAliasResponse(
    brillo::Error* error) {
  CHECK(step_ == TestStep::kTagTargetDevice ||
        step_ == TestStep::kResetDeviceTag);
  if (error) {
    SetResultAndStop(base::unexpected("Failed to update device alias."));
    return;
  }
  RunNextStep();
}

void BluetoothPairingRoutineV2::GetDeviceProperties() {
  CHECK(step_ == TestStep::kCollectDeviceInfo);
  CHECK(target_device_.contains("name") && target_device_.contains("address"));
  if (auto adapter = GetDefaultAdapterOrStop(); adapter != nullptr) {
    CallbackBarrier barrier{
        base::BindOnce(&BluetoothPairingRoutineV2::RunNextStep,
                       weak_ptr_factory_.GetWeakPtr()),
        base::BindOnce(&BluetoothPairingRoutineV2::SetResultAndStop,
                       weak_ptr_factory_.GetWeakPtr(),
                       base::unexpected("Failed to get device properties."))};

    // UUIDs.
    auto uuids_cb = SplitDbusCallback(barrier.Depend(
        base::BindOnce(&BluetoothPairingRoutineV2::StoreDeviceUuids,
                       weak_ptr_factory_.GetWeakPtr())));
    adapter->GetRemoteUuidsAsync(target_device_, std::move(uuids_cb.first),
                                 std::move(uuids_cb.second));
    // Class of Device (CoD).
    auto class_cb = SplitDbusCallback(barrier.Depend(
        base::BindOnce(&BluetoothPairingRoutineV2::StoreDeviceClass,
                       weak_ptr_factory_.GetWeakPtr())));
    adapter->GetRemoteClassAsync(target_device_, std::move(class_cb.first),
                                 std::move(class_cb.second));
    // Address Type.
    auto address_type_cb = SplitDbusCallback(barrier.Depend(
        base::BindOnce(&BluetoothPairingRoutineV2::StoreDeviceAddressType,
                       weak_ptr_factory_.GetWeakPtr())));
    adapter->GetRemoteAddressTypeAsync(target_device_,
                                       std::move(address_type_cb.first),
                                       std::move(address_type_cb.second));
  }
}

void BluetoothPairingRoutineV2::StoreDeviceUuids(
    brillo::Error* error, const std::vector<std::vector<uint8_t>>& uuids) {
  CHECK(step_ == TestStep::kCollectDeviceInfo);
  if (error) {
    SetResultAndStop(base::unexpected("Failed to get device UUIDs."));
    return;
  }
  for (const auto& uuid : uuids) {
    auto out_uuid = floss_utils::ParseUuidBytes(uuid);
    if (!out_uuid.is_valid()) {
      SetResultAndStop(
          base::unexpected("Failed to parse UUID from device UUIDs."));
      return;
    }
    routine_output_->pairing_peripheral->uuids.push_back(out_uuid);
  }
}

void BluetoothPairingRoutineV2::StoreDeviceClass(brillo::Error* error,
                                                 uint32_t bluetooth_class) {
  CHECK(step_ == TestStep::kCollectDeviceInfo);
  if (error) {
    SetResultAndStop(base::unexpected("Failed to get device class."));
    return;
  }
  routine_output_->pairing_peripheral->bluetooth_class = bluetooth_class;
}

void BluetoothPairingRoutineV2::StoreDeviceAddressType(brillo::Error* error,
                                                       uint32_t address_type) {
  CHECK(step_ == TestStep::kCollectDeviceInfo);
  CHECK(target_device_.contains("address"));
  if (error) {
    SetResultAndStop(base::unexpected("Failed to get device address type."));
    return;
  }

  auto [is_address_valid, failed_manufacturer_id] = ValidatePeripheralAddress(
      brillo::GetVariantValueOrDefault<std::string>(target_device_, "address"),
      /*address_type=*/GetAddressTypeString(address_type));
  routine_output_->pairing_peripheral->address_type =
      GetAddressTypeEnum(address_type);
  routine_output_->pairing_peripheral->is_address_valid = is_address_valid;
  routine_output_->pairing_peripheral->failed_manufacturer_id =
      failed_manufacturer_id;
}

void BluetoothPairingRoutineV2::HandleBondDeviceResponse(brillo::Error* error,
                                                         bool is_success) {
  CHECK(step_ == TestStep::kBondTargetDevice);
  if (error || !is_success) {
    routine_output_->pairing_peripheral->pair_error =
        mojom::BluetoothPairingPeripheralInfo_PairError::kBondFailed;
    SetResultAndStop(base::ok(false));
    return;
  }
}

void BluetoothPairingRoutineV2::OnDeviceConnectedChanged(
    const brillo::VariantDictionary& device, bool connected) {
  if (step_ != TestStep::kBondTargetDevice || device != target_device_ ||
      !connected) {
    return;
  }

  // Wait for the response of connection state to reset this error in
  // |HandleConnectionStateResponse|.
  routine_output_->pairing_peripheral->connect_error =
      mojom::BluetoothPairingPeripheralInfo_ConnectError::kNotConnected;

  // Check if baseband connection is established by checking connection state.
  if (auto adapter = GetDefaultAdapterOrStop(); adapter != nullptr) {
    auto [on_success, on_error] = SplitDbusCallback(base::BindOnce(
        &BluetoothPairingRoutineV2::HandleConnectionStateResponse,
        weak_ptr_factory_.GetWeakPtr()));
    adapter->GetConnectionStateAsync(target_device_, std::move(on_success),
                                     std::move(on_error));
  }
}

void BluetoothPairingRoutineV2::HandleConnectionStateResponse(
    brillo::Error* error, uint32_t state) {
  if (error) {
    SetResultAndStop(
        base::unexpected("Failed to get device connection state."));
    return;
  }
  if (state == 0 /*not_connected*/) {
    SetResultAndStop(base::ok(false));
    return;
  }
  routine_output_->pairing_peripheral->connect_error =
      mojom::BluetoothPairingPeripheralInfo_ConnectError::kNone;
}

void BluetoothPairingRoutineV2::OnDeviceSspRequest(
    const brillo::VariantDictionary& device) {
  if (step_ != TestStep::kBondTargetDevice || device != target_device_) {
    return;
  }
  if (auto adapter = GetDefaultAdapterOrStop(); adapter != nullptr) {
    auto [on_success, on_error] = SplitDbusCallback(base::BindOnce(
        &BluetoothPairingRoutineV2::HandlePairingConfirmationResponse,
        weak_ptr_factory_.GetWeakPtr()));
    adapter->SetPairingConfirmationAsync(target_device_, /*in_accept=*/true,
                                         std::move(on_success),
                                         std::move(on_error));
  }
}

void BluetoothPairingRoutineV2::HandlePairingConfirmationResponse(
    brillo::Error* error, bool is_success) {
  if (error || !is_success) {
    routine_output_->pairing_peripheral->pair_error =
        mojom::BluetoothPairingPeripheralInfo_PairError::kSspFailed;
    SetResultAndStop(base::ok(false));
    return;
  }
}

void BluetoothPairingRoutineV2::OnDeviceBondChanged(uint32_t bt_status,
                                                    const std::string& address,
                                                    BondState bond_state) {
  if (step_ != TestStep::kBondTargetDevice ||
      address != brillo::GetVariantValueOrDefault<std::string>(target_device_,
                                                               "address")) {
    return;
  }
  // |bt_status| is 0 for Success. We can check the meaning of non-zero status
  // values ​​through the |BtStatus| enum in the Android codebase:
  // packages/modules/Bluetooth/system/gd/rust/topshim/src/btif.rs
  if (bt_status != 0) {
    LOG(ERROR) << "Get unexpected Bluetooth status: " << bt_status;
    routine_output_->pairing_peripheral->pair_error =
        mojom::BluetoothPairingPeripheralInfo_PairError::kBadStatus;
    SetResultAndStop(base::ok(false));
    return;
  }
  // Routine will receive this event after SSP process is finished.
  if (bond_state == BondState::kBonded)
    RunNextStep();
}

void BluetoothPairingRoutineV2::HandleRemoveBondResponse(brillo::Error* error,
                                                         bool is_success) {
  if (error || !is_success) {
    SetResultAndStop(base::unexpected("Failed to remove target peripheral."));
    return;
  }
  RunNextStep();
}

void BluetoothPairingRoutineV2::UpdatePercentage() {
  double new_percentage = static_cast<int32_t>(step_) * 100.0 /
                          static_cast<int32_t>(TestStep::kComplete);
  if (new_percentage > state()->percentage && new_percentage < 100)
    SetPercentage(new_percentage);
}

void BluetoothPairingRoutineV2::OnTimeoutOccurred() {
  if (step_ == TestStep::kScanTargetDevice) {
    SetResultAndStop(base::ok(false));
  } else if (step_ == TestStep::kBondTargetDevice) {
    routine_output_->pairing_peripheral->pair_error =
        mojom::BluetoothPairingPeripheralInfo_PairError::kTimeout;
    SetResultAndStop(base::ok(false));
  } else {
    SetResultAndStop(base::unexpected(
        "Bluetooth routine failed to complete before timeout."));
  }
}

void BluetoothPairingRoutineV2::SetResultAndStop(
    const base::expected<bool, std::string>& result) {
  // Cancel all pending callbacks.
  weak_ptr_factory_.InvalidateWeakPtrs();
  remove_target_peripheral_.RunAndReset();
  adapter_stop_discovery_.RunAndReset();
  reset_bluetooth_powered_.RunAndReset();

  if (!result.has_value()) {
    RaiseException(result.error());
    return;
  }

  SetFinishedState(result.value(), mojom::RoutineDetail::NewBluetoothPairing(
                                       std::move(routine_output_)));
}

}  // namespace diagnostics
