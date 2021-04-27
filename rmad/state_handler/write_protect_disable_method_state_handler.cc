// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/write_protect_disable_method_state_handler.h"

namespace rmad {

WriteProtectDisableMethodStateHandler::WriteProtectDisableMethodStateHandler(
    scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {
  ResetState();
}

RmadState::StateCase WriteProtectDisableMethodStateHandler::GetNextStateCase()
    const {
  if (state_.wp_disable_method().disable_method() ==
      WriteProtectDisableMethodState::RMAD_WP_DISABLE_RSU) {
    return RmadState::StateCase::kWpDisableRsu;
  }
  if (state_.wp_disable_method().disable_method() ==
      WriteProtectDisableMethodState::RMAD_WP_DISABLE_PHYSICAL) {
    return RmadState::StateCase::kWpDisablePhysical;
  }
  // Not ready to go to next state.
  return GetStateCase();
}

RmadErrorCode WriteProtectDisableMethodStateHandler::UpdateState(
    const RmadState& state) {
  CHECK(state.has_wp_disable_method())
      << "RmadState missing write protection disable method state.";
  const WriteProtectDisableMethodState& wp_disable_method =
      state.wp_disable_method();
  if (wp_disable_method.disable_method() ==
      WriteProtectDisableMethodState::RMAD_WP_DISABLE_UNKNOWN) {
    // TODO(gavindodd): What is correct error for unset/missing fields?
    return RMAD_ERROR_REQUEST_INVALID;
  }
  state_ = state;

  return RMAD_ERROR_OK;
}

RmadErrorCode WriteProtectDisableMethodStateHandler::ResetState() {
  state_.set_allocated_wp_disable_method(new WriteProtectDisableMethodState);

  return RMAD_ERROR_OK;
}

}  // namespace rmad
