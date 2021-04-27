// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/restock_state_handler.h"

namespace rmad {

RestockStateHandler::RestockStateHandler(scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {
  ResetState();
}

RmadState::StateCase RestockStateHandler::GetNextStateCase() const {
  // TODO(chenghan): This is currently fake.
  if (state_.restock_state().choice() != RestockState::RMAD_RESTOCK_UNKNOWN) {
    return RmadState::StateCase::kUpdateDeviceInfo;
  }
  // Not ready to go to next state.
  return GetStateCase();
}

RmadErrorCode RestockStateHandler::UpdateState(const RmadState& state) {
  CHECK(state.has_restock_state()) << "RmadState missing restock state.";
  const RestockState& restock = state.restock_state();
  if (restock.choice() == RestockState::RMAD_RESTOCK_UNKNOWN) {
    // TODO(gavindodd): What is correct error for unset/missing fields?
    return RMAD_ERROR_REQUEST_INVALID;
  }
  state_ = state;

  return RMAD_ERROR_OK;
}

RmadErrorCode RestockStateHandler::ResetState() {
  state_.set_allocated_restock_state(new RestockState);

  return RMAD_ERROR_OK;
}

}  // namespace rmad
