// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/finalize_state_handler.h"

namespace rmad {

FinalizeStateHandler::FinalizeStateHandler(scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {
  ResetState();
}

BaseStateHandler::GetNextStateCaseReply FinalizeStateHandler::GetNextStateCase(
    const RmadState& state) {
  if (!state.has_finalize()) {
    LOG(ERROR) << "RmadState missing |finalize| state.";
    return {.error = RMAD_ERROR_REQUEST_INVALID, .state_case = GetStateCase()};
  }
  const FinalizeState& finalize = state.finalize();
  if (finalize.shutdown() == FinalizeState::RMAD_FINALIZE_UNKNOWN) {
    LOG(ERROR) << "RmadState missing |shutdown| argument.";
    return {.error = RMAD_ERROR_REQUEST_ARGS_MISSING,
            .state_case = GetStateCase()};
  }

  // TODO(chenghan): This is currently fake.
  state_ = state;
  return {.error = RMAD_ERROR_OK,
          .state_case = RmadState::StateCase::STATE_NOT_SET};
}

RmadErrorCode FinalizeStateHandler::ResetState() {
  state_.set_allocated_finalize(new FinalizeState);

  return RMAD_ERROR_OK;
}

}  // namespace rmad
