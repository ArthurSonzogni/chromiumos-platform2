// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/write_protect_disable_rsu_state_handler.h"

namespace rmad {

WriteProtectDisableRsuStateHandler::WriteProtectDisableRsuStateHandler(
    scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {
  ResetState();
}

RmadState::StateCase WriteProtectDisableRsuStateHandler::GetNextStateCase()
    const {
  if (state_.wp_disable_rsu().unlock_code().size()) {
    // TODO(chenghan): Try the code to unlock the device. Need a reboot to take
    //                 effect, so we should carefully design the flow here.
    return RmadState::StateCase::kWpDisableComplete;
  }
  // Not ready to go to next state.
  return GetStateCase();
}

RmadErrorCode WriteProtectDisableRsuStateHandler::UpdateState(
    const RmadState& state) {
  CHECK(state.has_wp_disable_rsu()) << "RmadState missing RSU state.";
  const WriteProtectDisableRsuState& wp_disable_rsu = state.wp_disable_rsu();
  if (wp_disable_rsu.unlock_code().empty()) {
    return RMAD_ERROR_REQUEST_INVALID;
  }
  state_ = state;

  return RMAD_ERROR_OK;
}

RmadErrorCode WriteProtectDisableRsuStateHandler::ResetState() {
  state_.set_allocated_wp_disable_rsu(new WriteProtectDisableRsuState);

  return RMAD_ERROR_OK;
}

}  // namespace rmad
