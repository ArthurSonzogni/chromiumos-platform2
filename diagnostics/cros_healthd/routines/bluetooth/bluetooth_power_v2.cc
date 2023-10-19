// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/bluetooth/bluetooth_power_v2.h"

#include <utility>

#include <base/functional/callback.h>
#include <base/task/single_thread_task_runner.h>
#include <base/types/expected.h>

#include "diagnostics/cros_healthd/mojom/executor.mojom.h"
#include "diagnostics/cros_healthd/routines/bluetooth/bluetooth_constants.h"
#include "diagnostics/cros_healthd/system/context.h"
#include "diagnostics/cros_healthd/system/floss_event_hub.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

}  // namespace

BluetoothPowerRoutineV2::BluetoothPowerRoutineV2(
    Context* context, const mojom::BluetoothPowerRoutineArgumentPtr& arg)
    : BluetoothRoutineBaseV2(context) {
  CHECK(context_);

  routine_output_ = mojom::BluetoothPowerRoutineDetail::New();
}

BluetoothPowerRoutineV2::~BluetoothPowerRoutineV2() = default;

void BluetoothPowerRoutineV2::OnStart() {
  CHECK(step_ == TestStep::kInitialize);
  SetRunningState();

  start_ticks_ = base::TimeTicks::Now();

  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&BluetoothPowerRoutineV2::OnTimeoutOccurred,
                     weak_ptr_factory_.GetWeakPtr()),
      kPowerRoutineTimeout);

  event_subscriptions_.push_back(
      context_->floss_event_hub()->SubscribeAdapterPoweredChanged(
          base::BindRepeating(&BluetoothPowerRoutineV2::OnAdapterPoweredChanged,
                              weak_ptr_factory_.GetWeakPtr())));

  Initialize(base::BindOnce(&BluetoothPowerRoutineV2::HandleInitializeResult,
                            weak_ptr_factory_.GetWeakPtr()));
}

void BluetoothPowerRoutineV2::HandleInitializeResult(bool success) {
  if (!success) {
    SetResultAndStop(
        base::unexpected("Failed to initialize Bluetooth routine"));
    return;
  }
  RunNextStep();
}

void BluetoothPowerRoutineV2::RunNextStep() {
  step_ = static_cast<TestStep>(static_cast<int>(step_) + 1);
  UpdatePercentage();

  switch (step_) {
    case TestStep::kInitialize:
      SetResultAndStop(base::unexpected(kBluetoothRoutineUnexpectedFlow));
      break;
    case TestStep::kPreCheckDiscovery:
      RunPreCheck(
          base::BindOnce(&BluetoothPowerRoutineV2::HandlePreCheckResponse,
                         weak_ptr_factory_.GetWeakPtr()));
      break;
    case TestStep::kCheckPoweredStatusOff: {
      // We can't get the power off event when the power is already off.
      // Create another flow to skip event observation.
      if (!GetAdapterInitialPoweredState()) {
        // Validate the powered status in HCI level directly.
        context_->executor()->GetHciDeviceConfig(
            /*hci_interface=*/default_adapter_hci_,
            base::BindOnce(&BluetoothPowerRoutineV2::HandleHciConfigResponse,
                           weak_ptr_factory_.GetWeakPtr(),
                           /*dbus_powered=*/false));
        return;
      }

      // Wait for the property changed event in |OnAdapterPoweredChanged|.
      ChangeAdapterPoweredState(
          /*powered=*/false,
          base::BindOnce(&BluetoothPowerRoutineV2::HandleChangePoweredResponse,
                         weak_ptr_factory_.GetWeakPtr()));
      break;
    }
    case TestStep::kCheckPoweredStatusOn:
      // Wait for the property changed event in |OnAdapterPoweredChanged|.
      ChangeAdapterPoweredState(
          /*powered=*/true,
          base::BindOnce(&BluetoothPowerRoutineV2::HandleChangePoweredResponse,
                         weak_ptr_factory_.GetWeakPtr()));
      break;
    case TestStep::kComplete:
      SetResultAndStop(/*result=*/base::ok(true));
      break;
  }
}

void BluetoothPowerRoutineV2::HandlePreCheckResponse(
    std::optional<std::string> error) {
  if (error.has_value()) {
    SetResultAndStop(base::unexpected(error.value()));
    return;
  }
  RunNextStep();
}

void BluetoothPowerRoutineV2::HandleChangePoweredResponse(
    const base::expected<bool, std::string>& result) {
  if (!result.has_value() || !result.value()) {
    SetResultAndStop(result);
  }
}

void BluetoothPowerRoutineV2::OnAdapterPoweredChanged(int32_t hci_interface,
                                                      bool powered) {
  if (hci_interface != default_adapter_hci_ ||
      (step_ != TestStep::kCheckPoweredStatusOff &&
       step_ != TestStep::kCheckPoweredStatusOn))
    return;

  // Validate the powered status in HCI level.
  context_->executor()->GetHciDeviceConfig(
      /*hci_interface=*/default_adapter_hci_,
      base::BindOnce(&BluetoothPowerRoutineV2::HandleHciConfigResponse,
                     weak_ptr_factory_.GetWeakPtr(), /*dbus_powered=*/powered));
}

void BluetoothPowerRoutineV2::HandleHciConfigResponse(
    bool dbus_powered, mojom::ExecutedProcessResultPtr result) {
  std::string err = result->err;
  int32_t return_code = result->return_code;

  if (!err.empty() || return_code != EXIT_SUCCESS) {
    LOG(ERROR) << "GetHciConfig failed with return code: " << return_code
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

void BluetoothPowerRoutineV2::ValidateAdapterPowered(bool dbus_powered,
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

void BluetoothPowerRoutineV2::UpdatePercentage() {
  double new_percentage = static_cast<int32_t>(step_) * 100.0 /
                          static_cast<int32_t>(TestStep::kComplete);
  // Update the percentage.
  if (new_percentage > state()->percentage && new_percentage < 100)
    SetPercentage(new_percentage);
}

void BluetoothPowerRoutineV2::OnTimeoutOccurred() {
  SetResultAndStop(
      base::unexpected("Bluetooth routine failed to complete before timeout."));
}

void BluetoothPowerRoutineV2::SetResultAndStop(
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

}  // namespace diagnostics
