// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/write_protect_disable_physical_state_handler.h"

#include <memory>
#include <utility>

#include <base/logging.h>

#include "rmad/constants.h"
#include "rmad/metrics/metrics_utils.h"
#include "rmad/system/fake_power_manager_client.h"
#include "rmad/system/power_manager_client_impl.h"
#include "rmad/utils/cr50_utils_impl.h"
#include "rmad/utils/crossystem_utils_impl.h"
#include "rmad/utils/dbus_utils.h"

namespace rmad {

WriteProtectDisablePhysicalStateHandler::
    WriteProtectDisablePhysicalStateHandler(
        scoped_refptr<JsonStore> json_store,
        scoped_refptr<DaemonCallback> daemon_callback)
    : BaseStateHandler(json_store, daemon_callback),
      working_dir_path_(kDefaultWorkingDirPath) {
  cr50_utils_ = std::make_unique<Cr50UtilsImpl>();
  crossystem_utils_ = std::make_unique<CrosSystemUtilsImpl>();
  power_manager_client_ =
      std::make_unique<PowerManagerClientImpl>(GetSystemBus());
}

WriteProtectDisablePhysicalStateHandler::
    WriteProtectDisablePhysicalStateHandler(
        scoped_refptr<JsonStore> json_store,
        scoped_refptr<DaemonCallback> daemon_callback,
        const base::FilePath& working_dir_path,
        std::unique_ptr<Cr50Utils> cr50_utils,
        std::unique_ptr<CrosSystemUtils> crossystem_utils,
        std::unique_ptr<PowerManagerClient> power_manager_client)
    : BaseStateHandler(json_store, daemon_callback),
      working_dir_path_(working_dir_path),
      cr50_utils_(std::move(cr50_utils)),
      crossystem_utils_(std::move(crossystem_utils)),
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

  // To transition to next state, HWWP should be disabled, and we can skip
  // enabling factory mode (either factory mode is already enabled, or we want
  // to keep the device open).
  if (CanSkipEnablingFactoryMode() && IsHwwpDisabled()) {
    if (cr50_utils_->IsFactoryModeEnabled()) {
      MetricsUtils::SetMetricsValue(
          json_store_, kWpDisableMethod,
          WpDisableMethod_Name(
              RMAD_WP_DISABLE_METHOD_PHYSICAL_ASSEMBLE_DEVICE));
    } else {
      MetricsUtils::SetMetricsValue(
          json_store_, kWpDisableMethod,
          WpDisableMethod_Name(
              RMAD_WP_DISABLE_METHOD_PHYSICAL_KEEP_DEVICE_OPEN));
    }
    return NextStateCaseWrapper(RmadState::StateCase::kWpDisableComplete);
  }
  // Wait for HWWP being disabled, or the follow-up preparations are done.
  return NextStateCaseWrapper(RMAD_ERROR_WAIT);
}

bool WriteProtectDisablePhysicalStateHandler::IsHwwpDisabled() const {
  int hwwp_status;
  return (crossystem_utils_->GetHwwpStatus(&hwwp_status) && hwwp_status == 0);
}

bool WriteProtectDisablePhysicalStateHandler::CanSkipEnablingFactoryMode()
    const {
  return cr50_utils_->IsFactoryModeEnabled() ||
         state_.wp_disable_physical().keep_device_open();
}

void WriteProtectDisablePhysicalStateHandler::CheckWriteProtectOffTask() {
  VLOG(1) << "Check write protection";

  if (IsHwwpDisabled()) {
    signal_timer_.Stop();
    if (CanSkipEnablingFactoryMode()) {
      daemon_callback_->GetWriteProtectSignalCallback().Run(false);
    } else {
      EnableFactoryMode();
    }
  }
}

void WriteProtectDisablePhysicalStateHandler::EnableFactoryMode() {
  // Sync state file.
  // TODO(chenghan): Do we still need this after we can trigger the reboot?
  json_store_->Sync();
  // Enable cr50 factory mode. This no longer reboots the device, so we need to
  // trigger a reboot ourselves.
  if (!cr50_utils_->EnableFactoryMode()) {
    LOG(ERROR) << "Failed to enable factory mode.";
  }
  // Inject rma-mode powerwash if it is not disabled.
  if (!IsPowerwashDisabled(working_dir_path_) &&
      !RequestPowerwash(working_dir_path_)) {
    LOG(ERROR) << "Failed to request powerwash";
  }
  // Reboot.
  reboot_timer_.Start(FROM_HERE, kRebootDelay, this,
                      &WriteProtectDisablePhysicalStateHandler::Reboot);
}

void WriteProtectDisablePhysicalStateHandler::Reboot() {
  power_manager_client_->Restart();
}

}  // namespace rmad
