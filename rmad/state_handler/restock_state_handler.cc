// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/restock_state_handler.h"

#include <memory>
#include <utility>

#include <base/logging.h>
#include <base/notreached.h>

#include "rmad/system/power_manager_client_impl.h"
#include "rmad/utils/dbus_utils.h"

namespace rmad {

RestockStateHandler::RestockStateHandler(scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {
  power_manager_client_ =
      std::make_unique<PowerManagerClientImpl>(GetSystemBus());
}

RestockStateHandler::RestockStateHandler(
    scoped_refptr<JsonStore> json_store,
    std::unique_ptr<PowerManagerClient> power_manager_client)
    : BaseStateHandler(json_store),
      power_manager_client_(std::move(power_manager_client)) {}

RmadErrorCode RestockStateHandler::InitializeState() {
  if (!state_.has_restock() && !RetrieveState()) {
    state_.set_allocated_restock(new RestockState);
  }
  return RMAD_ERROR_OK;
}

BaseStateHandler::GetNextStateCaseReply RestockStateHandler::GetNextStateCase(
    const RmadState& state) {
  if (!state.has_restock()) {
    LOG(ERROR) << "RmadState missing |restock| state.";
    return {.error = RMAD_ERROR_REQUEST_INVALID, .state_case = GetStateCase()};
  }

  state_ = state;
  StoreState();

  switch (state_.restock().choice()) {
    case RestockState::RMAD_RESTOCK_UNKNOWN:
      return {.error = RMAD_ERROR_REQUEST_ARGS_MISSING,
              .state_case = GetStateCase()};
    case RestockState::RMAD_RESTOCK_SHUTDOWN_AND_RESTOCK:
      // Set the choice to "Continue", so the device can auto-transition to the
      // next state on next boot.
      state_.mutable_restock()->set_choice(
          RestockState::RMAD_RESTOCK_CONTINUE_RMA);
      StoreState();
      // Wait for a while before shutting down.
      timer_.Start(FROM_HERE, kShutdownDelay, this,
                   &RestockStateHandler::Shutdown);
      return {.error = RMAD_ERROR_EXPECT_SHUTDOWN,
              .state_case = GetStateCase()};
    case RestockState::RMAD_RESTOCK_CONTINUE_RMA:
      return {.error = RMAD_ERROR_OK,
              .state_case = RmadState::StateCase::kUpdateDeviceInfo};
    default:
      break;
  }
  NOTREACHED();
  return {.error = RMAD_ERROR_NOT_SET,
          .state_case = RmadState::StateCase::STATE_NOT_SET};
}

void RestockStateHandler::Shutdown() {
  LOG(INFO) << "Shutting down to restock";
  if (!power_manager_client_->Shutdown()) {
    LOG(ERROR) << "Failed to shut down";
  }
}

}  // namespace rmad
