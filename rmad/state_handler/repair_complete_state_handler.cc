// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/repair_complete_state_handler.h"

#include <memory>
#include <utility>

#include <base/logging.h>
#include <base/notreached.h>

#include "rmad/system/power_manager_client_impl.h"
#include "rmad/utils/crossystem_utils_impl.h"
#include "rmad/utils/dbus_utils.h"

namespace {

constexpr char kBatteryCutoffRequest[] = "battery_cutoff_request";

}  // namespace

namespace rmad {

RepairCompleteStateHandler::RepairCompleteStateHandler(
    scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {
  power_manager_client_ =
      std::make_unique<PowerManagerClientImpl>(GetSystemBus());
  crossystem_utils_ = std::make_unique<CrosSystemUtilsImpl>();
}

RepairCompleteStateHandler::RepairCompleteStateHandler(
    scoped_refptr<JsonStore> json_store,
    std::unique_ptr<PowerManagerClient> power_manager_client,
    std::unique_ptr<CrosSystemUtils> crossystem_utils)
    : BaseStateHandler(json_store),
      power_manager_client_(std::move(power_manager_client)),
      crossystem_utils_(std::move(crossystem_utils)) {}

RmadErrorCode RepairCompleteStateHandler::InitializeState() {
  if (!state_.has_repair_complete()) {
    state_.set_allocated_repair_complete(new RepairCompleteState);
  }
  return RMAD_ERROR_OK;
}

BaseStateHandler::GetNextStateCaseReply
RepairCompleteStateHandler::GetNextStateCase(const RmadState& state) {
  if (!state.has_repair_complete()) {
    LOG(ERROR) << "RmadState missing |repair_complete| state.";
    return {.error = RMAD_ERROR_REQUEST_INVALID, .state_case = GetStateCase()};
  }

  switch (state.repair_complete().shutdown()) {
    case RepairCompleteState::RMAD_REPAIR_COMPLETE_UNKNOWN:
      return {.error = RMAD_ERROR_REQUEST_ARGS_MISSING,
              .state_case = GetStateCase()};
    case RepairCompleteState::RMAD_REPAIR_COMPLETE_REBOOT:
      // Wait for a while before reboot.
      timer_.Start(FROM_HERE, kShutdownDelay, this,
                   &RepairCompleteStateHandler::Reboot);
      return {.error = RMAD_ERROR_EXPECT_REBOOT, .state_case = GetStateCase()};
    case RepairCompleteState::RMAD_REPAIR_COMPLETE_SHUTDOWN:
      // Wait for a while before shutdown.
      timer_.Start(FROM_HERE, kShutdownDelay, this,
                   &RepairCompleteStateHandler::Shutdown);
      return {.error = RMAD_ERROR_EXPECT_SHUTDOWN,
              .state_case = GetStateCase()};
    case RepairCompleteState::RMAD_REPAIR_COMPLETE_BATTERY_CUTOFF:
      // Wait for a while before cutoff.
      timer_.Start(FROM_HERE, kShutdownDelay, this,
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
  // TODO(chenghan): This currently doesn't work, because we have no permission
  //                 to access /dev/nvram in minijail. Try another approach or
  //                 discuss with security team if we can open up permissions.
  if (!crossystem_utils_->SetInt(kBatteryCutoffRequest, 1)) {
    LOG(ERROR) << "Failed to request battery cutoff";
    return;
  }
  // Battery cutoff requires a reboot (not shutdown) after the request.
  if (!power_manager_client_->Restart()) {
    LOG(ERROR) << "Failed to reboot";
  }
}

}  // namespace rmad
