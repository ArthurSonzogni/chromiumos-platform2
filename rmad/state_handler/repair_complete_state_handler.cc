// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/repair_complete_state_handler.h"

#include <memory>
#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/notreached.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <brillo/file_utils.h>

#include "rmad/constants.h"
#include "rmad/metrics/fake_metrics_utils.h"
#include "rmad/metrics/metrics_utils_impl.h"
#include "rmad/system/fake_power_manager_client.h"
#include "rmad/system/power_manager_client_impl.h"
#include "rmad/utils/dbus_utils.h"
#include "rmad/utils/fake_sys_utils.h"
#include "rmad/utils/sys_utils_impl.h"

namespace {

constexpr char kPowerwashCountPath[] = "powerwash_count";

bool ReadFileToInt(const base::FilePath& path, int* value) {
  std::string str;
  if (!base::ReadFileToString(path, &str)) {
    LOG(ERROR) << "Failed to read from path " << path;
    return false;
  }
  base::TrimWhitespaceASCII(str, base::TRIM_ALL, &str);
  return base::StringToInt(str, value);
}

}  // namespace

namespace rmad {

namespace fake {

FakeRepairCompleteStateHandler::FakeRepairCompleteStateHandler(
    scoped_refptr<JsonStore> json_store, const base::FilePath& working_dir_path)
    : RepairCompleteStateHandler(
          json_store,
          working_dir_path,
          working_dir_path,
          std::make_unique<FakePowerManagerClient>(working_dir_path),
          std::make_unique<FakeSysUtils>(working_dir_path),
          std::make_unique<FakeMetricsUtils>(working_dir_path)) {}

}  // namespace fake

RepairCompleteStateHandler::RepairCompleteStateHandler(
    scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store),
      working_dir_path_(kDefaultWorkingDirPath),
      unencrypted_preserve_path_(kDefaultUnencryptedPreservePath),
      power_cable_signal_sender_(base::DoNothing()) {
  power_manager_client_ =
      std::make_unique<PowerManagerClientImpl>(GetSystemBus());
  sys_utils_ = std::make_unique<SysUtilsImpl>();
  metrics_utils_ = std::make_unique<MetricsUtilsImpl>();
}

RepairCompleteStateHandler::RepairCompleteStateHandler(
    scoped_refptr<JsonStore> json_store,
    const base::FilePath& working_dir_path,
    const base::FilePath& unencrypted_preserve_path,
    std::unique_ptr<PowerManagerClient> power_manager_client,
    std::unique_ptr<SysUtils> sys_utils,
    std::unique_ptr<MetricsUtils> metrics_utils)
    : BaseStateHandler(json_store),
      working_dir_path_(working_dir_path),
      unencrypted_preserve_path_(unencrypted_preserve_path),
      power_cable_signal_sender_(base::DoNothing()),
      power_manager_client_(std::move(power_manager_client)),
      sys_utils_(std::move(sys_utils)),
      metrics_utils_(std::move(metrics_utils)) {}

RmadErrorCode RepairCompleteStateHandler::InitializeState() {
  if (!state_.has_repair_complete() && !RetrieveState()) {
    state_.set_allocated_repair_complete(new RepairCompleteState);
    // Record the current powerwash count during initialization. If the file
    // doesn't exist, set the value to 0. This file counter is incremented by
    // one after every powerwash. See platform2/init/clobber_state.cc for more
    // detail.
    int powerwash_count = 0;
    ReadFileToInt(unencrypted_preserve_path_.AppendASCII(kPowerwashCountPath),
                  &powerwash_count);
    json_store_->SetValue(kPowerwashCount, powerwash_count);
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

  // kWipeDevice should be set by previous states.
  bool wipe_device;
  if (!json_store_->GetValue(kWipeDevice, &wipe_device)) {
    LOG(ERROR) << "Variable " << kWipeDevice << " not found";
    return NextStateCaseWrapper(RMAD_ERROR_TRANSITION_FAILED);
  }

  if (wipe_device && !IsPowerwashComplete() &&
      !base::PathExists(
          working_dir_path_.AppendASCII(kDisablePowerwashFilePath)) &&
      !base::PathExists(working_dir_path_.AppendASCII(kTestDirPath))) {
    // Request a powerwash if we want to wipe the device, and powerwash is not
    // done yet. The pre-stop script picks up the |kPowerwashRequestFilePath|
    // file before reboot and requests a rma-mode powerwash.
    // |kDisablePowerwashFilePath| is a file for testing convenience. Manually
    // touch this file if we want to avoid powerwash during testing. Powerwash
    // is also disabled when the test mode directory exists.
    if (!brillo::TouchFile(
            working_dir_path_.AppendASCII(kPowerwashRequestFilePath))) {
      LOG(ERROR) << "Failed to request powerwash";
      return NextStateCaseWrapper(RMAD_ERROR_POWERWASH_FAILED);
    }
    action_timer_.Start(FROM_HERE, kShutdownDelay, this,
                        &RepairCompleteStateHandler::Reboot);
    return NextStateCaseWrapper(GetStateCase(), RMAD_ERROR_EXPECT_REBOOT,
                                AdditionalActivity::REBOOT);
  } else {
    // Clear the state file and shutdown/reboot/cutoff if the device doesn't
    // need to do a powerwash, or a powerwash is already done.
    if (!metrics_utils_->Record(json_store_, true)) {
      LOG(ERROR) << "RepairCompleteState: Failed to record metrics to the file";
      // TODO(genechang): We should block here if the metrics library is ready.
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
  power_cable_signal_sender_.Run(sys_utils_->IsPowerSourcePresent());
}

bool RepairCompleteStateHandler::IsPowerwashComplete() const {
  int stored_powerwash_count, current_powerwash_count;
  if (!json_store_->GetValue(kPowerwashCount, &stored_powerwash_count)) {
    LOG(ERROR) << "Key " << kPowerwashCount << " should exist in |json_store|";
    return false;
  }
  if (!ReadFileToInt(
          unencrypted_preserve_path_.AppendASCII(kPowerwashCountPath),
          &current_powerwash_count)) {
    return false;
  }
  return current_powerwash_count > stored_powerwash_count;
}

}  // namespace rmad
