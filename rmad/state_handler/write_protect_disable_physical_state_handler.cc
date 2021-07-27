// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/write_protect_disable_physical_state_handler.h"

#include <memory>
#include <utility>

#include "rmad/system/cryptohome_client_impl.h"
#include "rmad/utils/cr50_utils_impl.h"
#include "rmad/utils/crossystem_utils_impl.h"

#include <base/logging.h>

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
  cryptohome_client_ = std::make_unique<CryptohomeClientImpl>();
}

WriteProtectDisablePhysicalStateHandler::
    WriteProtectDisablePhysicalStateHandler(
        scoped_refptr<JsonStore> json_store,
        std::unique_ptr<Cr50Utils> cr50_utils,
        std::unique_ptr<CrosSystemUtils> crossystem_utils,
        std::unique_ptr<CryptohomeClient> cryptohome_client)
    : BaseStateHandler(json_store),
      cr50_utils_(std::move(cr50_utils)),
      crossystem_utils_(std::move(crossystem_utils)),
      cryptohome_client_(std::move(cryptohome_client)) {}

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

  int hwwp_status;
  if (crossystem_utils_->GetInt(kHwwpProperty, &hwwp_status) &&
      hwwp_status == 0) {
    // Enable cr50 factory mode if possible.
    if (!cr50_utils_->IsFactoryModeEnabled() &&
        !cryptohome_client_->HasFwmp()) {
      cr50_utils_->EnableFactoryMode();
      return {.error = RMAD_ERROR_EXPECT_REBOOT, .state_case = GetStateCase()};
    } else {
      return {.error = RMAD_ERROR_OK,
              .state_case = RmadState::StateCase::kWpDisableComplete};
    }
  }

  return {.error = RMAD_ERROR_WAIT, .state_case = GetStateCase()};
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

  int hwwp_status;
  if (!crossystem_utils_->GetInt(kHwwpProperty, &hwwp_status)) {
    LOG(ERROR) << "Failed to get HWWP status";
    return;
  }
  if (hwwp_status == 0) {
    write_protect_signal_sender_->Run(false);
    timer_.Stop();
  }
}

}  // namespace rmad
