// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/write_protect_disable_physical_state_handler.h"

#include <unistd.h>

#include <memory>
#include <string>
#include <utility>

#include <base/functional/bind.h>
#include <base/logging.h>

#include "rmad/constants.h"
#include "rmad/metrics/metrics_utils.h"
#include "rmad/system/power_manager_client_impl.h"
#include "rmad/utils/crossystem_utils_impl.h"
#include "rmad/utils/gsc_utils_impl.h"
#include "rmad/utils/write_protect_utils_impl.h"

namespace rmad {

WriteProtectDisablePhysicalStateHandler::
    WriteProtectDisablePhysicalStateHandler(
        scoped_refptr<JsonStore> json_store,
        scoped_refptr<DaemonCallback> daemon_callback)
    : BaseStateHandler(json_store, daemon_callback),
      working_dir_path_(kDefaultWorkingDirPath) {
  gsc_utils_ = std::make_unique<GscUtilsImpl>();
  crossystem_utils_ = std::make_unique<CrosSystemUtilsImpl>();
  write_protect_utils_ = std::make_unique<WriteProtectUtilsImpl>();
  power_manager_client_ = std::make_unique<PowerManagerClientImpl>();
}

WriteProtectDisablePhysicalStateHandler::
    WriteProtectDisablePhysicalStateHandler(
        scoped_refptr<JsonStore> json_store,
        scoped_refptr<DaemonCallback> daemon_callback,
        const base::FilePath& working_dir_path,
        std::unique_ptr<GscUtils> gsc_utils,
        std::unique_ptr<CrosSystemUtils> crossystem_utils,
        std::unique_ptr<WriteProtectUtils> write_protect_utils,
        std::unique_ptr<PowerManagerClient> power_manager_client)
    : BaseStateHandler(json_store, daemon_callback),
      working_dir_path_(working_dir_path),
      gsc_utils_(std::move(gsc_utils)),
      crossystem_utils_(std::move(crossystem_utils)),
      write_protect_utils_(std::move(write_protect_utils)),
      power_manager_client_(std::move(power_manager_client)) {}

RmadErrorCode WriteProtectDisablePhysicalStateHandler::InitializeState() {
  if (!state_.has_wp_disable_physical()) {
    auto wp_disable_physical =
        std::make_unique<WriteProtectDisablePhysicalState>();
    // Keep device open if we don't want to wipe the device.
    bool wipe_device;
    if (!json_store_->GetValue(kWipeDevice, &wipe_device)) {
      LOG(ERROR) << "Variable " << kWipeDevice << " not found";
      return RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED;
    }
    wp_disable_physical->set_keep_device_open(!wipe_device);
    state_.set_allocated_wp_disable_physical(wp_disable_physical.release());
  }

  return RMAD_ERROR_OK;
}

void WriteProtectDisablePhysicalStateHandler::RunState() {
  VLOG(1) << "Start polling write protection";
  if (signal_timer_.IsRunning()) {
    signal_timer_.Stop();
  }
  signal_timer_.Start(
      FROM_HERE, kPollInterval, this,
      &WriteProtectDisablePhysicalStateHandler::CheckWriteProtectOffTask);
}

void WriteProtectDisablePhysicalStateHandler::CleanUpState() {
  // Stop the polling loop.
  if (signal_timer_.IsRunning()) {
    signal_timer_.Stop();
  }
}

BaseStateHandler::GetNextStateCaseReply
WriteProtectDisablePhysicalStateHandler::GetNextStateCase(
    const RmadState& state) {
  if (!state.has_wp_disable_physical()) {
    LOG(ERROR) << "RmadState missing |physical write protection| state.";
    return NextStateCaseWrapper(RMAD_ERROR_REQUEST_INVALID);
  }

  // The state will reboot automatically when write protect is disabled. Before
  // that, always return RMAD_ERROR_WAIT.
  return NextStateCaseWrapper(RMAD_ERROR_WAIT);
}

BaseStateHandler::GetNextStateCaseReply
WriteProtectDisablePhysicalStateHandler::TryGetNextStateCaseAtBoot() {
  // If conditions are met, we can transition to the next state.
  if (IsReadyForTransition()) {
    if (gsc_utils_->IsFactoryModeEnabled()) {
      json_store_->SetValue(
          kWpDisableMethod,
          WpDisableMethod_Name(
              RMAD_WP_DISABLE_METHOD_PHYSICAL_ASSEMBLE_DEVICE));
      MetricsUtils::SetMetricsValue(
          json_store_, kMetricsWpDisableMethod,
          WpDisableMethod_Name(
              RMAD_WP_DISABLE_METHOD_PHYSICAL_ASSEMBLE_DEVICE));
    } else {
      json_store_->SetValue(
          kWpDisableMethod,
          WpDisableMethod_Name(
              RMAD_WP_DISABLE_METHOD_PHYSICAL_KEEP_DEVICE_OPEN));
      MetricsUtils::SetMetricsValue(
          json_store_, kMetricsWpDisableMethod,
          WpDisableMethod_Name(
              RMAD_WP_DISABLE_METHOD_PHYSICAL_KEEP_DEVICE_OPEN));
    }
    return NextStateCaseWrapper(RmadState::StateCase::kWpDisableComplete);
  }

  // Otherwise, stay on the same state.
  return NextStateCaseWrapper(GetStateCase());
}

bool WriteProtectDisablePhysicalStateHandler::IsReadyForTransition() const {
  // To transition to next state, all the conditions should meet
  // - It should be HWWP disabled or CHASSIS_OPEN true.
  //   Cr50 devices' HWWP will follow CHASSIS_OPEN, while Ti50 devices won't, so
  //   we check both HWWP and CHASSIS_OPEN here (b/257255419).
  // - We can skip enabling factory mode, either factory mode is already enabled
  //   or we want to keep the device open.
  return CanSkipEnablingFactoryMode() &&
         (IsHwwpDisabled() || IsChassisOpened());
}

bool WriteProtectDisablePhysicalStateHandler::IsHwwpDisabled() const {
  auto hwwp_enabled = write_protect_utils_->GetHardwareWriteProtectionStatus();
  return (hwwp_enabled.has_value() && !hwwp_enabled.value());
}

bool WriteProtectDisablePhysicalStateHandler::IsChassisOpened() const {
  auto chassis_open = gsc_utils_->GetChassisOpenStatus();
  return (chassis_open.has_value() && chassis_open.value());
}

bool WriteProtectDisablePhysicalStateHandler::CanSkipEnablingFactoryMode()
    const {
  return gsc_utils_->IsFactoryModeEnabled() ||
         state_.wp_disable_physical().keep_device_open();
}

void WriteProtectDisablePhysicalStateHandler::CheckWriteProtectOffTask() {
  VLOG(1) << "Check write protection";

  if (IsHwwpDisabled() || IsChassisOpened()) {
    signal_timer_.Stop();
    OnWriteProtectDisabled();
  }
}

void WriteProtectDisablePhysicalStateHandler::OnWriteProtectDisabled() {
  if (!CanSkipEnablingFactoryMode()) {
    // Enable GSC factory mode. This no longer reboots the device, so we need
    // to trigger a reboot ourselves.
    if (!gsc_utils_->EnableFactoryMode()) {
      LOG(ERROR) << "Failed to enable factory mode.";
    }

    // Preseed rmad state file so it can be preserved across TPM reset.
    daemon_callback_->GetExecutePreseedRmaStateCallback().Run(
        base::BindOnce(&WriteProtectDisablePhysicalStateHandler::
                           ExecutePreseedRmaStateCallback,
                       base::Unretained(this)));
    return;
  }

  ExecutePreseedRmaStateCallback(true);
}

void WriteProtectDisablePhysicalStateHandler::RequestRmaPowerwashAndReboot() {
  DLOG(INFO) << "Requesting RMA mode powerwash";
  daemon_callback_->GetExecuteRequestRmaPowerwashCallback().Run(
      base::BindOnce(&WriteProtectDisablePhysicalStateHandler::
                         RequestRmaPowerwashAndRebootCallback,
                     base::Unretained(this)));
}

void WriteProtectDisablePhysicalStateHandler::
    RequestRmaPowerwashAndRebootCallback(bool success) {
  if (!success) {
    LOG(ERROR) << "Failed to request RMA mode powerwash";
  }
  Reboot();
}

void WriteProtectDisablePhysicalStateHandler::ExecutePreseedRmaStateCallback(
    bool success) {
  if (!success) {
    LOG(ERROR) << "Failed to preseed rmad state file.";
  }

  // Chrome picks up the signal and shows the "Preparing to reboot" message.
  daemon_callback_->GetWriteProtectSignalCallback().Run(false);

  // Request RMA mode powerwash if required, then reboot.
  if (!IsPowerwashDisabled(working_dir_path_)) {
    reboot_timer_.Start(
        FROM_HERE, kRebootDelay,
        base::BindOnce(&WriteProtectDisablePhysicalStateHandler::
                           RequestRmaPowerwashAndReboot,
                       base::Unretained(this)));
  } else {
    reboot_timer_.Start(
        FROM_HERE, kRebootDelay,
        base::BindOnce(&WriteProtectDisablePhysicalStateHandler::Reboot,
                       base::Unretained(this)));
  }
}

void WriteProtectDisablePhysicalStateHandler::Reboot() {
  DLOG(INFO) << "Rebooting after physically removing WP";
  // Sync filesystems before doing reboot.
  sync();
  if (!power_manager_client_->Restart()) {
    LOG(ERROR) << "Failed to reboot";
  }
}

}  // namespace rmad
