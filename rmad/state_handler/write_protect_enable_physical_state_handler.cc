// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/write_protect_enable_physical_state_handler.h"

namespace rmad {

WriteProtectEnablePhysicalStateHandler::WriteProtectEnablePhysicalStateHandler(
    scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {}

RmadErrorCode WriteProtectEnablePhysicalStateHandler::InitializeState() {
  if (!state_.has_wp_enable_physical() && !RetrieveState()) {
    state_.set_allocated_wp_enable_physical(
        new WriteProtectEnablePhysicalState);
  }
  return RMAD_ERROR_OK;
}

BaseStateHandler::GetNextStateCaseReply
WriteProtectEnablePhysicalStateHandler::GetNextStateCase(
    const RmadState& state) {
  if (!state.has_wp_enable_physical()) {
    LOG(ERROR) << "RmadState missing |write protection enable| state.";
    return {.error = RMAD_ERROR_REQUEST_INVALID, .state_case = GetStateCase()};
  }

  // There's nothing in |WriteProtectEnablePhysicalState|.
  state_ = state;
  StoreState();

  // TODO(chenghan): This is currently fake. Poll the write protect signal and
  //                 emit a signal when it's enabled.
  return {.error = RMAD_ERROR_OK,
          .state_case = RmadState::StateCase::kFinalize};
}

}  // namespace rmad
