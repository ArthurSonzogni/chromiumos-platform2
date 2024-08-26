// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/bluetooth/floss/bluetooth_power.h"

#include <utility>

#include <base/functional/callback.h>
#include <base/task/single_thread_task_runner.h>
#include <base/types/expected.h>

#include "diagnostics/cros_healthd/mojom/executor.mojom.h"
#include "diagnostics/cros_healthd/routines/bluetooth/bluetooth_constants.h"
#include "diagnostics/cros_healthd/system/context.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics::floss {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

}  // namespace

BluetoothPowerRoutine::BluetoothPowerRoutine(
    Context* context, const mojom::BluetoothPowerRoutineArgumentPtr& arg)
    : BluetoothRoutineBase(context) {
  CHECK(context_);

  routine_output_ = mojom::BluetoothPowerRoutineDetail::New();
}

BluetoothPowerRoutine::~BluetoothPowerRoutine() = default;

void BluetoothPowerRoutine::OnStart() {
  CHECK(step_ == TestStep::kInitialize);
  SetRunningState();

  start_ticks_ = base::TimeTicks::Now();

  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&BluetoothPowerRoutine::OnTimeoutOccurred,
                     weak_ptr_factory_.GetWeakPtr()),
      kPowerRoutineTimeout);

  Initialize(base::BindOnce(&BluetoothPowerRoutine::HandleInitializeResult,
                            weak_ptr_factory_.GetWeakPtr()));
}

void BluetoothPowerRoutine::HandleInitializeResult(bool success) {
  if (!success) {
    SetResultAndStop(
        base::unexpected("Failed to initialize Bluetooth routine"));
    return;
  }
  RunNextStep();
}

void BluetoothPowerRoutine::RunNextStep() {
  step_ = static_cast<TestStep>(static_cast<int>(step_) + 1);
  UpdatePercentage();

  switch (step_) {
    case TestStep::kInitialize:
      SetResultAndStop(base::unexpected(kBluetoothRoutineUnexpectedFlow));
      break;
    case TestStep::kPreCheckDiscovery:
      RunPreCheck(base::BindOnce(&BluetoothPowerRoutine::HandlePreCheckResponse,
                                 weak_ptr_factory_.GetWeakPtr()));
      break;
    case TestStep::kCheckPoweredStatusOff: {
      // We can't get the power off event when the power is already off.
      // Create another flow to skip event observation.
      if (!GetAdapterInitialPoweredState()) {
        // Validate the powered status in HCI level directly.
        context_->executor()->GetHciDeviceConfig(
            /*hci_interface=*/default_adapter_hci_,
            base::BindOnce(&BluetoothPowerRoutine::HandleHciConfigResponse,
                           weak_ptr_factory_.GetWeakPtr(),
                           /*dbus_powered=*/false));
        return;
      }

      SetAdapterPoweredState(
          /*powered=*/false,
          base::BindOnce(&BluetoothPowerRoutine::HandleSetPoweredResponse,
                         weak_ptr_factory_.GetWeakPtr()));
      break;
    }
    case TestStep::kCheckPoweredStatusOn:
      SetAdapterPoweredState(
          /*powered=*/true,
          base::BindOnce(&BluetoothPowerRoutine::HandleSetPoweredResponse,
                         weak_ptr_factory_.GetWeakPtr()));
      break;
    case TestStep::kComplete:
      SetResultAndStop(/*result=*/base::ok(true));
      break;
  }
}

void BluetoothPowerRoutine::HandlePreCheckResponse(
    std::optional<std::string> error) {
  if (error.has_value()) {
    SetResultAndStop(base::unexpected(error.value()));
    return;
  }
  RunNextStep();
}

void BluetoothPowerRoutine::HandleSetPoweredResponse(
    std::optional<bool> dbus_powered) {
  if (!dbus_powered.has_value()) {
    SetResultAndStop(
        base::unexpected("Got unexpected error when setting adapter powered"));
    return;
  }

  // Validate the powered status in HCI level.
  context_->executor()->GetHciDeviceConfig(
      /*hci_interface=*/default_adapter_hci_,
      base::BindOnce(&BluetoothPowerRoutine::HandleHciConfigResponse,
                     weak_ptr_factory_.GetWeakPtr(), dbus_powered.value()));
}

void BluetoothPowerRoutine::HandleHciConfigResponse(
    bool dbus_powered, mojom::ExecutedProcessResultPtr result) {
  std::string err = result->err;
  int32_t return_code = result->return_code;

  if (!err.empty() || return_code != EXIT_SUCCESS) {
    LOG(ERROR) << "Failed to get HCI config for hci" << default_adapter_hci_
               << " with return code: " << return_code
               << " and error: " << err.c_str();
    SetResultAndStop(base::unexpected(
        "Failed to parse powered status from HCI device config."));
    return;
  }

  bool check_powered_off = result->out.find("DOWN") != std::string::npos;
  bool check_powered_on = result->out.find("UP RUNNING") != std::string::npos;
  if (check_powered_off && !check_powered_on) {
    ValidateAdapterPowered(dbus_powered, /*hci_powered=*/false);
  } else if (!check_powered_off && check_powered_on) {
    ValidateAdapterPowered(dbus_powered, /*hci_powered=*/true);
  } else {
    LOG(ERROR) << "Failed to parse hciconfig, out: " << result->out;
    SetResultAndStop(base::unexpected(
        "Failed to parse powered status from HCI device config."));
  }
}

void BluetoothPowerRoutine::ValidateAdapterPowered(bool dbus_powered,
                                                   bool hci_powered) {
  bool is_passed;
  auto powered_state = mojom::BluetoothPoweredDetail::New();
  powered_state->dbus_powered = dbus_powered;
  powered_state->hci_powered = hci_powered;

  if (step_ == TestStep::kCheckPoweredStatusOff) {
    // The powered status should be false.
    is_passed = !hci_powered && !dbus_powered;
    routine_output_->power_off_result = std::move(powered_state);
  } else if (step_ == TestStep::kCheckPoweredStatusOn) {
    // The powered status should be true.
    is_passed = hci_powered && dbus_powered;
    routine_output_->power_on_result = std::move(powered_state);
  } else {
    SetResultAndStop(base::unexpected(kBluetoothRoutineUnexpectedFlow));
    return;
  }

  // Stop routine if validation is failed.
  if (!is_passed) {
    SetResultAndStop(/*result=*/base::ok(false));
    return;
  }
  RunNextStep();
}

void BluetoothPowerRoutine::UpdatePercentage() {
  double new_percentage = static_cast<int32_t>(step_) * 100.0 /
                          static_cast<int32_t>(TestStep::kComplete);
  // Update the percentage.
  if (new_percentage > state()->percentage && new_percentage < 100)
    SetPercentage(new_percentage);
}

void BluetoothPowerRoutine::OnTimeoutOccurred() {
  SetResultAndStop(
      base::unexpected("Bluetooth routine failed to complete before timeout."));
}

void BluetoothPowerRoutine::SetResultAndStop(
    const base::expected<bool, std::string>& result) {
  // Cancel all pending callbacks.
  weak_ptr_factory_.InvalidateWeakPtrs();
  reset_bluetooth_powered_.RunAndReset();

  if (!result.has_value()) {
    RaiseException(result.error());
    return;
  }
  SetFinishedState(result.value(), mojom::RoutineDetail::NewBluetoothPower(
                                       std::move(routine_output_)));
}

}  // namespace diagnostics::floss
