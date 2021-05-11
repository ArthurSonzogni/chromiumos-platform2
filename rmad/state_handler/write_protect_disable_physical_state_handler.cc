// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/write_protect_disable_physical_state_handler.h"

namespace rmad {

WriteProtectDisablePhysicalStateHandler::
    WriteProtectDisablePhysicalStateHandler(scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {
  ResetState();
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
  return {.error = RMAD_ERROR_OK,
          .state_case = RmadState::StateCase::kWpDisableComplete};
}

RmadErrorCode WriteProtectDisablePhysicalStateHandler::ResetState() {
  state_.set_allocated_wp_disable_physical(
      new WriteProtectDisablePhysicalState);

  return RMAD_ERROR_OK;
}

bool WriteProtectDisablePhysicalStateHandler::CheckWriteProtectionOn() const {
  // TODO(chenghan): Get the info from crossystem.
  return false;
}

}  // namespace rmad
