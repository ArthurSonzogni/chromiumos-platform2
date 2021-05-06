// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/update_chrome_state_handler.h"

namespace rmad {

UpdateChromeStateHandler::UpdateChromeStateHandler(
    scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {
  ResetState();
}

RmadState::StateCase UpdateChromeStateHandler::GetNextStateCase() const {
  if (state_.update_chrome().update() ==
          UpdateChromeState::RMAD_UPDATE_STATE_COMPLETE ||
      state_.update_chrome().update() ==
          UpdateChromeState::RMAD_UPDATE_STATE_SKIP) {
    return RmadState::StateCase::kComponentsRepair;
  }
  // Not ready to go to next state.
  return GetStateCase();
}

RmadErrorCode UpdateChromeStateHandler::UpdateState(const RmadState& state) {
  CHECK(state.has_update_chrome()) << "RmadState missing update Chrome state.";
  const UpdateChromeState& update_chrome = state.update_chrome();
  if (update_chrome.update() == UpdateChromeState::RMAD_UPDATE_STATE_UNKNOWN) {
    // TODO(gavindodd): What is correct error for unset/missing fields?
    return RMAD_ERROR_REQUEST_INVALID;
  }
  state_ = state;

  return RMAD_ERROR_OK;
}

RmadErrorCode UpdateChromeStateHandler::ResetState() {
  state_.set_allocated_update_chrome(new UpdateChromeState);

  return RMAD_ERROR_OK;
}

}  // namespace rmad
