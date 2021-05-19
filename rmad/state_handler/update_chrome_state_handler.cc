// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/update_chrome_state_handler.h"

namespace rmad {

UpdateChromeStateHandler::UpdateChromeStateHandler(
    scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {}

RmadErrorCode UpdateChromeStateHandler::InitializeState() {
  if (!state_.has_update_chrome() && !RetrieveState()) {
    state_.set_allocated_update_chrome(new UpdateChromeState);
  }
  return RMAD_ERROR_OK;
}

BaseStateHandler::GetNextStateCaseReply
UpdateChromeStateHandler::GetNextStateCase(const RmadState& state) {
  if (!state.has_update_chrome()) {
    LOG(ERROR) << "RmadState missing |update Chrome| state.";
    return {.error = RMAD_ERROR_REQUEST_INVALID, .state_case = GetStateCase()};
  }
  const UpdateChromeState& update_chrome = state.update_chrome();
  if (update_chrome.update() == UpdateChromeState::RMAD_UPDATE_STATE_UNKNOWN) {
    return {.error = RMAD_ERROR_REQUEST_ARGS_MISSING,
            .state_case = GetStateCase()};
  }
  if (update_chrome.update() == UpdateChromeState::RMAD_UPDATE_STATE_UPDATE) {
    LOG(INFO) << "Chrome needs update. Blocking state transition.";
    return {.error = RMAD_ERROR_TRANSITION_FAILED,
            .state_case = GetStateCase()};
  }

  state_ = state;
  StoreState();

  return {.error = RMAD_ERROR_OK,
          .state_case = RmadState::StateCase::kComponentsRepair};
}

}  // namespace rmad
