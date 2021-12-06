// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/repair_complete_state_handler.h"

#include <memory>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/notreached.h>
#include <brillo/file_utils.h>

#include "rmad/constants.h"
#include "rmad/metrics/fake_metrics_utils.h"
#include "rmad/metrics/metrics_utils_impl.h"
#include "rmad/system/fake_power_manager_client.h"
#include "rmad/system/power_manager_client_impl.h"
#include "rmad/utils/dbus_utils.h"

namespace rmad {

namespace fake {

FakeRepairCompleteStateHandler::FakeRepairCompleteStateHandler(
    scoped_refptr<JsonStore> json_store, const base::FilePath& working_dir_path)
    : RepairCompleteStateHandler(
          json_store,
          working_dir_path,
          std::make_unique<FakePowerManagerClient>(working_dir_path),
          std::make_unique<FakeMetricsUtils>(working_dir_path)) {}

}  // namespace fake

RepairCompleteStateHandler::RepairCompleteStateHandler(
    scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store), working_dir_path_(kDefaultWorkingDirPath) {
  power_manager_client_ =
      std::make_unique<PowerManagerClientImpl>(GetSystemBus());
  metrics_utils_ = std::make_unique<MetricsUtilsImpl>();
}

RepairCompleteStateHandler::RepairCompleteStateHandler(
    scoped_refptr<JsonStore> json_store,
    const base::FilePath& working_dir_path,
    std::unique_ptr<PowerManagerClient> power_manager_client,
    std::unique_ptr<MetricsUtils> metrics_utils)
    : BaseStateHandler(json_store),
      working_dir_path_(working_dir_path),
      power_manager_client_(std::move(power_manager_client)),
      metrics_utils_(std::move(metrics_utils)) {}

RmadErrorCode RepairCompleteStateHandler::InitializeState() {
  if (!state_.has_repair_complete() && !RetrieveState()) {
    state_.set_allocated_repair_complete(new RepairCompleteState);
  }
  power_cable_timer_.Start(
      FROM_HERE, kReportPowerCableInterval, this,
      &RepairCompleteStateHandler::SendPowerCableStateSignal);
  return RMAD_ERROR_OK;
}

void RepairCompleteStateHandler::CleanUpState() {
  power_cable_timer_.Stop();
}

BaseStateHandler::GetNextStateCaseReply
RepairCompleteStateHandler::GetNextStateCase(const RmadState& state) {
  if (!state.has_repair_complete()) {
    LOG(ERROR) << "RmadState missing |repair_complete| state.";
    return NextStateCaseWrapper(RMAD_ERROR_REQUEST_INVALID);
  }

  if (state.repair_complete().shutdown() ==
      RepairCompleteState::RMAD_REPAIR_COMPLETE_UNKNOWN) {
    return NextStateCaseWrapper(RMAD_ERROR_REQUEST_ARGS_MISSING);
  }

  state_ = state;
  StoreState();

  bool same_owner = false, powerwash_request = false;
  json_store_->GetValue(kSameOwner, &same_owner);
  json_store_->GetValue(kPowerwashRequest, &powerwash_request);
  if (same_owner || powerwash_request ||
      base::PathExists(
          working_dir_path_.AppendASCII(kDisablePowerwashFilePath)) ||
      base::PathExists(working_dir_path_.AppendASCII(kTestDirPath))) {
    // Clear the state file and shutdown/reboot/cutoff if the device is
    // returning to the same user, or it's already done a powerwash. We don't
    // set |kPowerwashRequest| back to false because the state file is getting
    // removed anyway. |kDisablePowerwashFilePath| is a file for testing
    // convenience. Manually touch this file if we want to avoid powerwash
    // during testing. Powerwash is also disabled when the test mode directory
    // exists.
    // TODO(chenghan): Check if powerwash is successful.
    if (!metrics_utils_->Record(json_store_, true)) {
      LOG(ERROR) << "RepairCompleteState: Failed to record metrics to the file";
      return NextStateCaseWrapper(RMAD_ERROR_TRANSITION_FAILED);
    }

    if (!json_store_->ClearAndDeleteFile()) {
      LOG(ERROR) << "RepairCompleteState: Failed to clear RMA state file";
      return NextStateCaseWrapper(RMAD_ERROR_TRANSITION_FAILED);
    }

    switch (state.repair_complete().shutdown()) {
      case RepairCompleteState::RMAD_REPAIR_COMPLETE_REBOOT:
        // Wait for a while before reboot.
        action_timer_.Start(FROM_HERE, kShutdownDelay, this,
                            &RepairCompleteStateHandler::Reboot);
        return {.error = RMAD_ERROR_EXPECT_REBOOT,
                .state_case = GetStateCase()};
      case RepairCompleteState::RMAD_REPAIR_COMPLETE_SHUTDOWN:
        // Wait for a while before shutdown.
        action_timer_.Start(FROM_HERE, kShutdownDelay, this,
                            &RepairCompleteStateHandler::Shutdown);
        return {.error = RMAD_ERROR_EXPECT_SHUTDOWN,
                .state_case = GetStateCase()};
      case RepairCompleteState::RMAD_REPAIR_COMPLETE_BATTERY_CUTOFF:
        // Wait for a while before cutoff.
        action_timer_.Start(FROM_HERE, kShutdownDelay, this,
                            &RepairCompleteStateHandler::Cutoff);
        return {.error = RMAD_ERROR_EXPECT_SHUTDOWN,
                .state_case = GetStateCase()};
      default:
        break;
    }
    NOTREACHED();
    return {.error = RMAD_ERROR_NOT_SET,
            .state_case = RmadState::StateCase::STATE_NOT_SET};
  } else {
    // Request a powerwash if the device is returning to a different user, and
    // powerwash is not done yet. The pre-stop script picks up the file before
    // reboot and requests a rma-mode powerwash.
    if (!json_store_->SetValue(kPowerwashRequest, true) ||
        !brillo::TouchFile(
            working_dir_path_.AppendASCII(kPowerwashRequestFilePath))) {
      LOG(ERROR) << "Failed to request powerwash";
      return NextStateCaseWrapper(RMAD_ERROR_POWERWASH_FAILED);
    }
    action_timer_.Start(FROM_HERE, kShutdownDelay, this,
                        &RepairCompleteStateHandler::Reboot);
    return NextStateCaseWrapper(GetStateCase(), RMAD_ERROR_EXPECT_REBOOT,
                                AdditionalActivity::REBOOT);
  }
}

void RepairCompleteStateHandler::Reboot() {
  LOG(INFO) << "RMA flow complete. Rebooting.";
  if (!power_manager_client_->Restart()) {
    LOG(ERROR) << "Failed to reboot";
  }
}

void RepairCompleteStateHandler::Shutdown() {
  LOG(INFO) << "RMA flow complete. Shutting down.";
  if (!power_manager_client_->Shutdown()) {
    LOG(ERROR) << "Failed to shut down";
  }
}

void RepairCompleteStateHandler::Cutoff() {
  LOG(INFO) << "RMA flow complete. Doing battery cutoff.";
  // The pre-stop script picks up the file before shutdown/reboot, and requests
  // a battery cutoff by crossystem.
  if (!brillo::TouchFile(
          working_dir_path_.AppendASCII(kCutoffRequestFilePath))) {
    LOG(ERROR) << "Failed to request battery cutoff";
    return;
  }
  // Battery cutoff requires a reboot (not shutdown) after the request.
  if (!power_manager_client_->Restart()) {
    LOG(ERROR) << "Failed to reboot";
  }
}

void RepairCompleteStateHandler::SendPowerCableStateSignal() {
  // TODO(chenghan): This is currently fake.
  CHECK(power_cable_signal_sender_);
  power_cable_signal_sender_->Run(true);
}

}  // namespace rmad
