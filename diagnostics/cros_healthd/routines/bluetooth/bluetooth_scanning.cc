// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/bluetooth/bluetooth_scanning.h"

#include <algorithm>
#include <numeric>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/functional/bind.h>
#include <base/hash/hash.h>
#include <base/json/json_writer.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/task/single_thread_task_runner.h>

#include "diagnostics/base/mojo_utils.h"
#include "diagnostics/cros_healthd/routines/bluetooth/bluetooth_constants.h"
#include "diagnostics/cros_healthd/system/bluez_event_hub.h"

namespace diagnostics {
namespace {

namespace mojom = ::ash::cros_healthd::mojom;

base::Value::Dict ConstructPeripheralDict(
    const ScannedPeripheralDevice& device) {
  base::Value::Dict peripheral;
  const auto& rssi_history = device.rssi_history;

  // RSSI history.
  base::Value::List out_rssi_history;
  for (const auto& rssi : rssi_history)
    out_rssi_history.Append(rssi);
  peripheral.Set("rssi_history", std::move(out_rssi_history));

  if (rssi_history.empty() ||
      std::accumulate(rssi_history.begin(), rssi_history.end(), 0.0) /
              static_cast<double>(rssi_history.size()) <
          kNearbyPeripheralMinimumAverageRssi) {
    return peripheral;
  }

  // Peripheral ID.
  peripheral.Set("peripheral_id", device.peripheral_id);
  // Name.
  if (device.name.has_value())
    peripheral.Set("name", device.name.value());

  return peripheral;
}

}  // namespace

BluetoothScanningRoutine::BluetoothScanningRoutine(
    Context* context, const std::optional<base::TimeDelta>& exec_duration)
    : BluetoothRoutineBase(context),
      exec_duration_(exec_duration.value_or(kScanningRoutineDefaultRuntime)) {}

BluetoothScanningRoutine::~BluetoothScanningRoutine() = default;

void BluetoothScanningRoutine::Start() {
  DCHECK_EQ(GetStatus(), mojom::DiagnosticRoutineStatusEnum::kReady);

  if (exec_duration_.is_zero()) {
    SetResultAndStop(
        mojom::DiagnosticRoutineStatusEnum::kError,
        "Routine execution time should be strictly greater than zero.");
    return;
  }

  UpdateStatus(mojom::DiagnosticRoutineStatusEnum::kRunning,
               kBluetoothRoutineRunningMessage);
  start_ticks_ = base::TimeTicks::Now();

  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&BluetoothScanningRoutine::OnTimeoutOccurred,
                     weak_ptr_factory_.GetWeakPtr()),
      exec_duration_ + kScanningRoutineTimeout);

  event_subscriptions_.push_back(
      context_->bluez_event_hub()->SubscribeDeviceAdded(
          base::BindRepeating(&BluetoothScanningRoutine::OnDeviceAdded,
                              weak_ptr_factory_.GetWeakPtr())));
  event_subscriptions_.push_back(
      context_->bluez_event_hub()->SubscribeDevicePropertyChanged(
          base::BindRepeating(
              &BluetoothScanningRoutine::OnDevicePropertyChanged,
              weak_ptr_factory_.GetWeakPtr())));

  RunPreCheck(
      /*on_passed=*/base::BindOnce(&BluetoothScanningRoutine::RunNextStep,
                                   weak_ptr_factory_.GetWeakPtr()),
      /*on_failed=*/base::BindOnce(&BluetoothScanningRoutine::SetResultAndStop,
                                   weak_ptr_factory_.GetWeakPtr()));
}

void BluetoothScanningRoutine::Resume() {
  LOG(ERROR) << "Bluetooth scanning routine cannot be resumed";
}

void BluetoothScanningRoutine::Cancel() {
  LOG(ERROR) << "Bluetooth scanning routine cannot be cancelled";
}

void BluetoothScanningRoutine::PopulateStatusUpdate(
    mojom::RoutineUpdate* response, bool include_output) {
  DCHECK(response);
  auto status = GetStatus();

  response->routine_update_union =
      mojom::RoutineUpdateUnion::NewNoninteractiveUpdate(
          mojom::NonInteractiveRoutineUpdate::New(status, GetStatusMessage()));

  if (include_output) {
    base::Value::List peripherals;
    for (const auto& device : scanned_devices_)
      peripherals.Append(ConstructPeripheralDict(device.second));
    base::Value::Dict output_dict;
    output_dict.Set("peripherals", std::move(peripherals));
    std::string json;
    base::JSONWriter::Write(output_dict, &json);
    response->output = CreateReadOnlySharedMemoryRegionMojoHandle(json);
  }

  // The routine is failed.
  if (status == mojom::DiagnosticRoutineStatusEnum::kFailed ||
      status == mojom::DiagnosticRoutineStatusEnum::kError) {
    response->progress_percent = 100;
    return;
  }

  // The routine is not started.
  if (status == mojom::DiagnosticRoutineStatusEnum::kReady) {
    response->progress_percent = 0;
    return;
  }

  double step_percent = step_ * 100 / TestStep::kComplete;
  double running_time_ratio =
      (base::TimeTicks::Now() - start_ticks_) / exec_duration_;
  response->progress_percent =
      step_percent + (100 - step_percent) * std::min(1.0, running_time_ratio);
}

void BluetoothScanningRoutine::RunNextStep() {
  step_ = static_cast<TestStep>(static_cast<int>(step_) + 1);

  switch (step_) {
    case TestStep::kInitialize:
      SetResultAndStop(mojom::DiagnosticRoutineStatusEnum::kError,
                       kBluetoothRoutineUnexpectedFlow);
      break;
    case TestStep::kEnsurePoweredOn:
      EnsureAdapterPoweredState(
          /*powered=*/true,
          base::BindOnce(&BluetoothScanningRoutine::HandleAdapterPoweredOn,
                         weak_ptr_factory_.GetWeakPtr()));
      break;
    case TestStep::kStartDiscovery:
      GetAdapter()->StartDiscoveryAsync(
          base::BindOnce(&BluetoothScanningRoutine::RunNextStep,
                         weak_ptr_factory_.GetWeakPtr()),
          base::BindOnce(&BluetoothScanningRoutine::HandleAdapterDiscoveryError,
                         weak_ptr_factory_.GetWeakPtr()));
      break;
    case TestStep::kScanning:
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(&BluetoothScanningRoutine::OnScanningFinished,
                         weak_ptr_factory_.GetWeakPtr()),
          exec_duration_);
      break;
    case TestStep::kStopDiscovery:
      GetAdapter()->StopDiscoveryAsync(
          base::BindOnce(&BluetoothScanningRoutine::RunNextStep,
                         weak_ptr_factory_.GetWeakPtr()),
          base::BindOnce(&BluetoothScanningRoutine::HandleAdapterDiscoveryError,
                         weak_ptr_factory_.GetWeakPtr()));
      break;
    case TestStep::kComplete:
      SetResultAndStop(mojom::DiagnosticRoutineStatusEnum::kPassed,
                       kBluetoothRoutinePassedMessage);
      break;
  }
}

void BluetoothScanningRoutine::HandleAdapterPoweredOn(bool is_success) {
  if (!is_success) {
    SetResultAndStop(mojom::DiagnosticRoutineStatusEnum::kError,
                     kBluetoothRoutineFailedChangePowered);
    return;
  }
  RunNextStep();
}

void BluetoothScanningRoutine::HandleAdapterDiscoveryError(
    brillo::Error* error) {
  if (error) {
    LOG(ERROR) << "Failed to change discovering status, error message: "
               << error->GetMessage();
  }
  SetResultAndStop(mojom::DiagnosticRoutineStatusEnum::kError,
                   kBluetoothRoutineFailedSwitchDiscovery);
}

void BluetoothScanningRoutine::OnDeviceAdded(
    org::bluez::Device1ProxyInterface* device) {
  if (!device || step_ != TestStep::kScanning)
    return;

  const auto& path = device->GetObjectPath();
  scanned_devices_[path].peripheral_id =
      base::NumberToString(base::FastHash(device->address()));
  if (device->is_name_valid())
    scanned_devices_[path].name = device->name();
  if (device->is_rssi_valid())
    scanned_devices_[path].rssi_history.push_back(device->rssi());
}

void BluetoothScanningRoutine::OnDevicePropertyChanged(
    org::bluez::Device1ProxyInterface* device,
    const std::string& property_name) {
  if (!device || step_ != TestStep::kScanning)
    return;

  const auto& path = device->GetObjectPath();
  // The device is cached before routine starts.
  if (scanned_devices_.find(path) == scanned_devices_.end()) {
    OnDeviceAdded(device);
    return;
  }

  if (property_name == device->NameName()) {
    if (device->is_name_valid())
      scanned_devices_[path].name = device->name();
  } else if (property_name == device->RSSIName()) {
    if (device->is_rssi_valid())
      scanned_devices_[path].rssi_history.push_back(device->rssi());
  }
}

void BluetoothScanningRoutine::OnScanningFinished() {
  if (step_ != TestStep::kScanning) {
    SetResultAndStop(mojom::DiagnosticRoutineStatusEnum::kError,
                     kBluetoothRoutineUnexpectedFlow);
    return;
  }
  // Successfully stop scanning.
  RunNextStep();
}

void BluetoothScanningRoutine::OnTimeoutOccurred() {
  SetResultAndStop(mojom::DiagnosticRoutineStatusEnum::kError,
                   "Bluetooth routine failed to complete before timeout.");
}

void BluetoothScanningRoutine::SetResultAndStop(
    mojom::DiagnosticRoutineStatusEnum status,
    const std::string& status_message) {
  // Cancel all pending callbacks.
  weak_ptr_factory_.InvalidateWeakPtrs();
  ResetPoweredState();
  UpdateStatus(status, status_message);
}

}  // namespace diagnostics
