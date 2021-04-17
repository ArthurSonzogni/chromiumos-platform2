// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/welcome_screen_state_handler.h"

namespace rmad {

WelcomeScreenStateHandler::WelcomeScreenStateHandler(
    scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {
  ResetState();
}

RmadState::StateCase WelcomeScreenStateHandler::GetNextStateCase() const {
  if (state_.welcome().choice() == WelcomeState::RMAD_CHOICE_FINALIZE_REPAIR) {
    return RmadState::StateCase::kSelectNetwork;
  }
  // Not ready to go to next state.
  return GetStateCase();
}

RmadErrorCode WelcomeScreenStateHandler::UpdateState(const RmadState& state) {
  CHECK(state.has_welcome()) << "RmadState missing welcome state";
  const WelcomeState& welcome = state.welcome();
  if (welcome.choice() == WelcomeState::RMAD_CHOICE_UNKNOWN) {
    // TODO(gavindodd): What is correct error for unset/missing fields?
    return RMAD_ERROR_REQUEST_INVALID;
  }
  state_ = state;
  if (welcome.choice() == WelcomeState::RMAD_CHOICE_CANCEL) {
    // TODO(gavindodd): Cancel RMA
  } else {
    CHECK_EQ(welcome.choice(), WelcomeState::RMAD_CHOICE_FINALIZE_REPAIR)
        << "Invalid choice " << welcome.choice();
  }

  return RMAD_ERROR_OK;
}

RmadErrorCode WelcomeScreenStateHandler::ResetState() {
  // TODO(gavindodd): Set state values in the WelcomeState proto and add to
  // json_store.
  WelcomeState* welcome = new WelcomeState;
  state_.set_allocated_welcome(welcome);

  return RMAD_ERROR_OK;
}

}  // namespace rmad
