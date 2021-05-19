// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/write_protect_disable_physical_state_handler.h"

namespace rmad {

WriteProtectDisablePhysicalStateHandler::
    WriteProtectDisablePhysicalStateHandler(scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {}

RmadErrorCode WriteProtectDisablePhysicalStateHandler::InitializeState() {
  if (!state_.has_wp_disable_physical() && !RetrieveState()) {
    state_.set_allocated_wp_disable_physical(
        new WriteProtectDisablePhysicalState);
  }
  return RMAD_ERROR_OK;
}

BaseStateHandler::GetNextStateCaseReply
WriteProtectDisablePhysicalStateHandler::GetNextStateCase(
    const RmadState& state) {
  if (!state.has_wp_disable_physical()) {
    LOG(ERROR) << "RmadState missing |physical write protection| state.";
    return {.error = RMAD_ERROR_REQUEST_INVALID, .state_case = GetStateCase()};
  }
  if (CheckWriteProtectionOn()) {
    LOG(ERROR) << "Write protection still enabled.";
    return {.error = RMAD_ERROR_TRANSITION_FAILED,
            .state_case = GetStateCase()};
  }

  // There's nothing in |WriteProtectDisablePhysicalState|.
  state_ = state;
  StoreState();

  // TODO(chenghan): This is currently fake. Poll the write protect signal and
  //                 emit a signal when it's disabled.
  return {.error = RMAD_ERROR_OK,
          .state_case = RmadState::StateCase::kWpDisableComplete};
}

bool WriteProtectDisablePhysicalStateHandler::CheckWriteProtectionOn() const {
  // TODO(chenghan): Get the info from crossystem.
  return false;
}

}  // namespace rmad
