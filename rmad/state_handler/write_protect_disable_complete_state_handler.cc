// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/write_protect_disable_complete_state_handler.h"

namespace rmad {

WriteProtectDisableCompleteStateHandler::
    WriteProtectDisableCompleteStateHandler(scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {
  ResetState();
}

RmadState::StateCase WriteProtectDisableCompleteStateHandler::GetNextStateCase()
    const {
  // TODO(chenghan): Implement the logic for different paths.
  return RmadState::StateCase::kUpdateRoFirmware;
}

RmadErrorCode WriteProtectDisableCompleteStateHandler::UpdateState(
    const RmadState& state) {
  CHECK(state.has_wp_disable_complete())
      << "RmadState missing WP disable complete state.";

  // There's nothing in |WriteProtectDisableCompleteState|.
  state_ = state;

  return RMAD_ERROR_OK;
}

RmadErrorCode WriteProtectDisableCompleteStateHandler::ResetState() {
  state_.set_allocated_wp_disable_complete(
      new WriteProtectDisableCompleteState);

  return RMAD_ERROR_OK;
}

}  // namespace rmad
