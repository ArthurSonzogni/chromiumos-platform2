// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/finalize_state_handler.h"

namespace rmad {

FinalizeStateHandler::FinalizeStateHandler(scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {
  ResetState();
}

RmadState::StateCase FinalizeStateHandler::GetNextStateCase() const {
  // TODO(chenghan): This is currently fake.
  return RmadState::StateCase::STATE_NOT_SET;
}

RmadErrorCode FinalizeStateHandler::UpdateState(const RmadState& state) {
  CHECK(state.has_finalize()) << "RmadState missing finalize state.";
  const FinalizeRmaState& finalize = state.finalize();
  if (finalize.shutdown() == FinalizeRmaState::RMAD_FINALIZE_UNKNOWN) {
    // TODO(gavindodd): What is correct error for unset/missing fields?
    return RMAD_ERROR_REQUEST_INVALID;
  }
  state_ = state;

  return RMAD_ERROR_OK;
}

RmadErrorCode FinalizeStateHandler::ResetState() {
  state_.set_allocated_finalize(new FinalizeRmaState);

  return RMAD_ERROR_OK;
}

}  // namespace rmad
