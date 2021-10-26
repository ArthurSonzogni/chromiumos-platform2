// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/write_protect_disable_physical_state_handler.h"

#include <memory>
#include <utility>

#include <base/logging.h>

#include "rmad/constants.h"
#include "rmad/system/power_manager_client_impl.h"
#include "rmad/utils/cr50_utils_impl.h"
#include "rmad/utils/crossystem_utils_impl.h"
#include "rmad/utils/dbus_utils.h"

namespace rmad {

namespace {

// crossystem HWWP property name.
constexpr char kHwwpProperty[] = "wpsw_cur";

}  // namespace

WriteProtectDisablePhysicalStateHandler::
    WriteProtectDisablePhysicalStateHandler(scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {
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
      cr50_utils_(std::move(cr50_utils)),
      crossystem_utils_(std::move(crossystem_utils)),
      power_manager_client_(std::move(power_manager_client)) {}

RmadErrorCode WriteProtectDisablePhysicalStateHandler::InitializeState() {
  if (!state_.has_wp_disable_physical()) {
    state_.set_allocated_wp_disable_physical(
        new WriteProtectDisablePhysicalState);
  }
  if (!write_protect_signal_sender_) {
    return RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED;
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
    return {.error = RMAD_ERROR_REQUEST_INVALID, .state_case = GetStateCase()};
  }

  // To transition to next state, either factory mode is enabled, or we've set a
  // flag indicating that the device should stay open.
  if (IsFactoryModeTried() && IsHwwpDisabled()) {
    return {.error = RMAD_ERROR_OK,
            .state_case = RmadState::StateCase::kWpDisableComplete};
  }
  // Wait for HWWP being disabled, or the follow-up preparations are done.
  return {.error = RMAD_ERROR_WAIT, .state_case = GetStateCase()};
}

bool WriteProtectDisablePhysicalStateHandler::IsHwwpDisabled() const {
  int hwwp_status;
  return (crossystem_utils_->GetInt(kHwwpProperty, &hwwp_status) &&
          hwwp_status == 0);
}

bool WriteProtectDisablePhysicalStateHandler::IsFactoryModeTried() const {
  if (cr50_utils_->IsFactoryModeEnabled()) {
    return true;
  }
  if (bool keep_device_open;
      json_store_->GetValue(kKeepDeviceOpen, &keep_device_open) &&
      keep_device_open) {
    return true;
  }
  return false;
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
  DCHECK(write_protect_signal_sender_);
  VLOG(1) << "Check write protection";

  if (IsHwwpDisabled()) {
    timer_.Stop();
    if (IsFactoryModeTried()) {
      write_protect_signal_sender_->Run(false);
    } else {
      // Enable cr50 factory mode if possible.
      if (!cr50_utils_->EnableFactoryMode()) {
        LOG(WARNING) << "WpDisablePhysical: Failed to enable factory mode";
        json_store_->SetValue(kKeepDeviceOpen, true);
        power_manager_client_->Restart();
      }
    }
  }
}

}  // namespace rmad
