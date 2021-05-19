// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/calibrate_components_state_handler.h"

namespace rmad {

CalibrateComponentsStateHandler::CalibrateComponentsStateHandler(
    scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {}

RmadErrorCode CalibrateComponentsStateHandler::InitializeState() {
  if (!state_.has_calibrate_components() && !RetrieveState()) {
    state_.set_allocated_calibrate_components(new CalibrateComponentsState);
  }
  return RMAD_ERROR_OK;
}

BaseStateHandler::GetNextStateCaseReply
CalibrateComponentsStateHandler::GetNextStateCase(const RmadState& state) {
  if (!state.has_calibrate_components()) {
    LOG(ERROR) << "RmadState missing |calibrate components| state.";
    return {.error = RMAD_ERROR_REQUEST_INVALID, .state_case = GetStateCase()};
  }

  // There's nothing in |CalibrateComponentsState|.
  state_ = state;
  StoreState();

  // TODO(chenghan): This is currently fake. Should emit signals for calibration
  //                 progress.
  return {.error = RMAD_ERROR_OK,
          .state_case = RmadState::StateCase::kProvisionDevice};
}

}  // namespace rmad
