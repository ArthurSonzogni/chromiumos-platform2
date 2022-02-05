// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/write_protect_disable_physical_state_handler.h"

#include <memory>
#include <utility>

#include <base/logging.h>

#include "rmad/constants.h"
#include "rmad/metrics/metrics_constants.h"
#include "rmad/system/fake_power_manager_client.h"
#include "rmad/system/power_manager_client_impl.h"
#include "rmad/utils/cr50_utils_impl.h"
#include "rmad/utils/crossystem_utils_impl.h"
#include "rmad/utils/dbus_utils.h"
#include "rmad/utils/fake_cr50_utils.h"
#include "rmad/utils/fake_crossystem_utils.h"

namespace {

// crossystem HWWP property name.
constexpr char kHwwpProperty[] = "wpsw_cur";

}  // namespace

namespace rmad {

namespace fake {

FakeWriteProtectDisablePhysicalStateHandler::
    FakeWriteProtectDisablePhysicalStateHandler(
        scoped_refptr<JsonStore> json_store,
        const base::FilePath& working_dir_path)
    : WriteProtectDisablePhysicalStateHandler(
          json_store,
          std::make_unique<FakeCr50Utils>(working_dir_path),
          std::make_unique<FakeCrosSystemUtils>(working_dir_path),
          std::make_unique<FakePowerManagerClient>(working_dir_path)) {}

}  // namespace fake

WriteProtectDisablePhysicalStateHandler::
    WriteProtectDisablePhysicalStateHandler(scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store),
      write_protect_signal_sender_(base::DoNothing()) {
  cr50_utils_ = std::make_unique<Cr50UtilsImpl>();
  crossystem_utils_ = std::make_unique<CrosSystemUtilsImpl>();
  power_manager_client_ =
      std::make_unique<PowerManagerClientImpl>(GetSystemBus());
}

WriteProtectDisablePhysicalStateHandler::
    WriteProtectDisablePhysicalStateHandler(
        scoped_refptr<JsonStore> json_store,
        std::unique_ptr<Cr50Utils> cr50_utils,
        std::unique_ptr<CrosSystemUtils> crossystem_utils,
        std::unique_ptr<PowerManagerClient> power_manager_client)
    : BaseStateHandler(json_store),
      write_protect_signal_sender_(base::DoNothing()),
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

  PollUntilWriteProtectOff();
  return RMAD_ERROR_OK;
}

void WriteProtectDisablePhysicalStateHandler::CleanUpState() {
  // Stop the polling loop.
  if (timer_.IsRunning()) {
    timer_.Stop();
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
    json_store_->SetValue(
        kWriteProtectDisableMethod,
        static_cast<int>(
            cr50_utils_->IsFactoryModeEnabled()
                ? WriteProtectDisableMethod::PHYSICAL_ASSEMBLE_DEVICE
                : WriteProtectDisableMethod::PHYSICAL_KEEP_DEVICE_OPEN));
    return NextStateCaseWrapper(RmadState::StateCase::kWpDisableComplete);
  }
  // Wait for HWWP being disabled, or the follow-up preparations are done.
  return NextStateCaseWrapper(RMAD_ERROR_WAIT);
}

bool WriteProtectDisablePhysicalStateHandler::IsHwwpDisabled() const {
  int hwwp_status;
  return (crossystem_utils_->GetInt(kHwwpProperty, &hwwp_status) &&
          hwwp_status == 0);
}

bool WriteProtectDisablePhysicalStateHandler::CanSkipEnablingFactoryMode()
    const {
  return cr50_utils_->IsFactoryModeEnabled() ||
         state_.wp_disable_physical().keep_device_open();
}

void WriteProtectDisablePhysicalStateHandler::PollUntilWriteProtectOff() {
  VLOG(1) << "Start polling write protection";
  if (timer_.IsRunning()) {
    timer_.Stop();
  }
  timer_.Start(
      FROM_HERE, kPollInterval, this,
      &WriteProtectDisablePhysicalStateHandler::CheckWriteProtectOffTask);
}

void WriteProtectDisablePhysicalStateHandler::CheckWriteProtectOffTask() {
  VLOG(1) << "Check write protection";

  if (IsHwwpDisabled()) {
    timer_.Stop();
    if (CanSkipEnablingFactoryMode()) {
      write_protect_signal_sender_.Run(false);
    } else {
      // Enable cr50 factory mode.
      if (cr50_utils_->EnableFactoryMode()) {
        // cr50 triggers a reboot shortly after enabling factory mode.
        return;
      }
      LOG(WARNING) << "WpDisablePhysical: Failed to enable factory mode.";
      // Still do a reboot when failed to enable factory mode, just for
      // consistent behavior.
      power_manager_client_->Restart();
    }
  }
}

}  // namespace rmad
