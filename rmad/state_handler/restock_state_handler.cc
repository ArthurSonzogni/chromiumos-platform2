// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/restock_state_handler.h"

namespace rmad {

RestockStateHandler::RestockStateHandler(scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {}

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
  const RestockState& restock = state.restock();
  if (restock.choice() == RestockState::RMAD_RESTOCK_UNKNOWN) {
    return {.error = RMAD_ERROR_REQUEST_ARGS_MISSING,
            .state_case = GetStateCase()};
  }

  state_ = state;
  StoreState();

  // TODO(chenghan): This is currently fake.
  return {.error = RMAD_ERROR_OK,
          .state_case = RmadState::StateCase::kUpdateDeviceInfo};
}

}  // namespace rmad
