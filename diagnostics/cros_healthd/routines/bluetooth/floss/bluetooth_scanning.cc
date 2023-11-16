// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/bluetooth/floss/bluetooth_scanning.h"

#include <algorithm>
#include <memory>
#include <numeric>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/functional/callback.h>
#include <base/hash/hash.h>
#include <base/strings/string_number_conversions.h>
#include <base/task/single_thread_task_runner.h>
#include <base/time/time.h>
#include <base/types/expected.h>
#include <brillo/variant_dictionary.h>

#include "diagnostics/cros_healthd/routines/bluetooth/bluetooth_constants.h"
#include "diagnostics/cros_healthd/system/context.h"
#include "diagnostics/cros_healthd/system/floss_event_hub.h"
#include "diagnostics/cros_healthd/system/ground_truth.h"
#include "diagnostics/cros_healthd/utils/dbus_utils.h"
#include "diagnostics/cros_healthd/utils/floss_utils.h"
#include "diagnostics/mojom/public/cros_healthd_exception.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics::floss {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

// Frequency to update the routine percentage.
constexpr base::TimeDelta kScanningRoutineUpdatePeriod =
    base::Milliseconds(500);

// Invalid RSSI, which is copied from |INVALID_RSSI| in the Android codebase:
// packages/modules/Bluetooth/system/gd/rust/topshim/src/btif.rs
constexpr int16_t kInvalidRssi = 127;

// Check if the peripheral is nearby via RSSI.
bool IsNearbyPeripheral(const std::vector<int16_t>& rssi_history) {
  if (rssi_history.empty()) {
    return false;
  }
  auto average_rssi =
      std::accumulate(rssi_history.begin(), rssi_history.end(), 0.0) /
      static_cast<double>(rssi_history.size());
  return average_rssi >= kNearbyPeripheralMinimumAverageRssi;
}

BluetoothScanningRoutine::CreateResult ReturnIfSupported(
    std::unique_ptr<BaseRoutineControl> routine,
    mojom::SupportStatusPtr status) {
  if (status->is_supported()) {
    return base::ok(std::move(routine));
  }
  return base::unexpected(std::move(status));
}

}  // namespace

// static
void BluetoothScanningRoutine::Create(
    Context* context,
    const ash::cros_healthd::mojom::BluetoothScanningRoutineArgumentPtr& arg,
    CreateCallback callback) {
  CHECK(!arg.is_null());
  if (arg->exec_duration && !arg->exec_duration->is_positive()) {
    std::move(callback).Run(base::unexpected(
        mojom::SupportStatus::NewUnsupported(mojom::Unsupported::New(
            "Execution duration should be strictly greater than zero",
            nullptr))));
    return;
  }
  context->ground_truth()->PrepareRoutineBluetoothFloss(
      base::BindOnce(
          &ReturnIfSupported,
          base::WrapUnique(new BluetoothScanningRoutine(context, arg)))
          .Then(std::move(callback)));
}

BluetoothScanningRoutine::BluetoothScanningRoutine(
    Context* context, const mojom::BluetoothScanningRoutineArgumentPtr& arg)
    : BluetoothRoutineBase(context),
      exec_duration_(
          arg->exec_duration.value_or(kScanningRoutineDefaultRuntime)) {
  CHECK(context_);
}

BluetoothScanningRoutine::~BluetoothScanningRoutine() = default;

void BluetoothScanningRoutine::OnStart() {
  CHECK(step_ == TestStep::kInitialize);
  SetRunningState();

  start_ticks_ = base::TimeTicks::Now();

  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&BluetoothScanningRoutine::OnTimeoutOccurred,
                     weak_ptr_factory_.GetWeakPtr()),
      exec_duration_ + kScanningRoutineTimeout);

  event_subscriptions_.push_back(
      context_->floss_event_hub()->SubscribeDeviceAdded(
          base::BindRepeating(&BluetoothScanningRoutine::OnDeviceAdded,
                              weak_ptr_factory_.GetWeakPtr())));
  event_subscriptions_.push_back(
      context_->floss_event_hub()->SubscribeDevicePropertyChanged(
          base::BindRepeating(
              &BluetoothScanningRoutine::OnDevicePropertyChanged,
              weak_ptr_factory_.GetWeakPtr())));

  Initialize(base::BindOnce(&BluetoothScanningRoutine::HandleInitializeResult,
                            weak_ptr_factory_.GetWeakPtr()));
}

void BluetoothScanningRoutine::HandleInitializeResult(bool success) {
  if (!success) {
    SetResultAndStop(
        base::unexpected("Failed to initialize Bluetooth routine."));
    return;
  }
  RunNextStep();
}

void BluetoothScanningRoutine::RunNextStep() {
  step_ = static_cast<TestStep>(static_cast<int32_t>(step_) + 1);
  UpdatePercentage();

  switch (step_) {
    case TestStep::kInitialize:
      SetResultAndStop(base::unexpected(kBluetoothRoutineUnexpectedFlow));
      break;
    case TestStep::kPreCheckDiscovery:
      RunPreCheck(
          base::BindOnce(&BluetoothScanningRoutine::HandlePreCheckResponse,
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
              &BluetoothScanningRoutine::HandleEnsurePoweredOnResponse,
              weak_ptr_factory_.GetWeakPtr()));
      break;
    case TestStep::kStartDiscovery:
      UpdateAdapterDiscoveryMode();
      break;
    case TestStep::kScanning:
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(&BluetoothScanningRoutine::OnScanningFinished,
                         weak_ptr_factory_.GetWeakPtr()),
          exec_duration_);
      break;
    case TestStep::kCancelDiscovery:
      UpdateAdapterDiscoveryMode();
      break;
    case TestStep::kComplete:
      SetResultAndStop(/*result=*/base::ok(true));
      break;
  }
}

void BluetoothScanningRoutine::HandlePreCheckResponse(
    std::optional<std::string> error) {
  if (error.has_value()) {
    SetResultAndStop(base::unexpected(error.value()));
    return;
  }
  RunNextStep();
}

void BluetoothScanningRoutine::HandleEnsurePoweredOnResponse(
    const base::expected<bool, std::string>& result) {
  if (!result.has_value() || !result.value()) {
    SetResultAndStop(
        base::unexpected("Failed to ensure default adapter is powered on."));
    return;
  }
  RunNextStep();
}

void BluetoothScanningRoutine::UpdateAdapterDiscoveryMode() {
  auto adapter = GetDefaultAdapter();
  if (!adapter) {
    SetResultAndStop(base::unexpected("Failed to get default adapter."));
    return;
  }

  auto [on_success, on_error] = SplitDbusCallback(
      base::BindOnce(&BluetoothScanningRoutine::HandleUpdateDiscoveryResponse,
                     weak_ptr_factory_.GetWeakPtr()));
  if (step_ == TestStep::kStartDiscovery) {
    SetupStopDiscoveryJob();
    adapter->StartDiscoveryAsync(std::move(on_success), std::move(on_error));
  } else if (step_ == TestStep::kCancelDiscovery) {
    adapter_stop_discovery_.ReplaceClosure(base::DoNothing());
    adapter->CancelDiscoveryAsync(std::move(on_success), std::move(on_error));
  } else {
    SetResultAndStop(base::unexpected(kBluetoothRoutineUnexpectedFlow));
    return;
  }
}

void BluetoothScanningRoutine::HandleUpdateDiscoveryResponse(
    brillo::Error* error, bool is_success) {
  if (error || !is_success) {
    SetResultAndStop(base::unexpected("Failed to update discovery mode."));
    return;
  }
  RunNextStep();
}

void BluetoothScanningRoutine::OnDeviceAdded(
    const brillo::VariantDictionary& device) {
  if (step_ != TestStep::kScanning) {
    return;
  }
  StoreScannedPeripheral(device);
}

void BluetoothScanningRoutine::OnDevicePropertyChanged(
    const brillo::VariantDictionary& device, BtPropertyType property) {
  // TODO(b/300239430): Add the |property == BtPropertyType::kRemoteRssi|
  // condition after RSSI changed event is supported.
  if (step_ != TestStep::kScanning) {
    return;
  }
  StoreScannedPeripheral(device);
}

void BluetoothScanningRoutine::StoreScannedPeripheral(
    const brillo::VariantDictionary& device) {
  auto devie_info = floss_utils::ParseDeviceInfo(device);
  if (!devie_info.has_value()) {
    SetResultAndStop(base::unexpected("Failed to parse device info."));
    return;
  }
  if (!scanned_peripherals_.contains(devie_info->address)) {
    scanned_peripherals_[devie_info->address].name = devie_info->name;
  }

  // TODO(b/300239430): Remove polling after RSSI changed event is supported.
  if (!polling_rssi_callbacks_.contains(devie_info->address)) {
    // Start polling for the new found peripheral.
    polling_rssi_callbacks_[devie_info->address] =
        base::BindRepeating(&BluetoothScanningRoutine::GetPeripheralRssi,
                            weak_ptr_factory_.GetWeakPtr(), device);
    polling_rssi_callbacks_[devie_info->address].Run();
  }
}

void BluetoothScanningRoutine::GetPeripheralRssi(
    const brillo::VariantDictionary& device) {
  auto adapter = GetDefaultAdapter();
  if (!adapter) {
    SetResultAndStop(base::unexpected("Failed to get default adapter."));
    return;
  }

  auto devie_info = floss_utils::ParseDeviceInfo(device);
  CHECK(devie_info.has_value());
  auto [on_success, on_error] = SplitDbusCallback(
      base::BindOnce(&BluetoothScanningRoutine::HandleRssiResponse,
                     weak_ptr_factory_.GetWeakPtr(), devie_info->address));
  adapter->GetRemoteRSSIAsync(device, std::move(on_success),
                              std::move(on_error));
}

void BluetoothScanningRoutine::HandleRssiResponse(const std::string& address,
                                                  brillo::Error* error,
                                                  int16_t rssi) {
  if (error) {
    SetResultAndStop(base::unexpected("Failed to get device RSSI"));
    return;
  }

  if (polling_rssi_callbacks_.contains(address)) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, polling_rssi_callbacks_[address],
        kScanningRoutineRssiPollingPeriod);
  }

  // Ignore the invalid RSSI.
  if (rssi == kInvalidRssi) {
    return;
  }
  scanned_peripherals_[address].rssi_history.push_back(rssi);
}

void BluetoothScanningRoutine::UpdatePercentage() {
  double step_percent = static_cast<int32_t>(step_) * 100.0 /
                        static_cast<int32_t>(TestStep::kComplete);
  double running_time_ratio =
      (base::TimeTicks::Now() - start_ticks_) / exec_duration_;
  int new_percentage =
      step_percent + (100.0 - step_percent) * std::min(1.0, running_time_ratio);
  if (new_percentage < 99) {
    percentage_update_task_.Reset(
        base::BindOnce(&BluetoothScanningRoutine::UpdatePercentage,
                       weak_ptr_factory_.GetWeakPtr()));
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, percentage_update_task_.callback(),
        kScanningRoutineUpdatePeriod);
  }

  // Update the percentage.
  if (new_percentage > state()->percentage && new_percentage < 100)
    SetPercentage(new_percentage);
}

void BluetoothScanningRoutine::OnScanningFinished() {
  if (step_ != TestStep::kScanning) {
    SetResultAndStop(base::unexpected(kBluetoothRoutineUnexpectedFlow));
    return;
  }
  // Remove RSSI polling callbacks.
  polling_rssi_callbacks_.clear();
  // Successfully stop scanning.
  RunNextStep();
}

void BluetoothScanningRoutine::OnTimeoutOccurred() {
  SetResultAndStop(
      base::unexpected("Bluetooth routine failed to complete before timeout."));
}

void BluetoothScanningRoutine::SetResultAndStop(
    const base::expected<bool, std::string>& result) {
  // Cancel all pending callbacks.
  weak_ptr_factory_.InvalidateWeakPtrs();
  adapter_stop_discovery_.RunAndReset();
  reset_bluetooth_powered_.RunAndReset();

  if (!result.has_value()) {
    RaiseException(result.error());
    return;
  }

  auto routine_output = mojom::BluetoothScanningRoutineDetail::New();
  for (const auto& [address, info] : scanned_peripherals_) {
    auto peripheral_info = mojom::BluetoothScannedPeripheralInfo::New();
    peripheral_info->rssi_history = info.rssi_history;
    if (IsNearbyPeripheral(info.rssi_history)) {
      peripheral_info->name = info.name;
      peripheral_info->peripheral_id =
          base::NumberToString(base::FastHash((address)));
    }
    routine_output->peripherals.push_back(std::move(peripheral_info));
  }
  SetFinishedState(result.value(), mojom::RoutineDetail::NewBluetoothScanning(
                                       std::move(routine_output)));
}

}  // namespace diagnostics::floss
