// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/select_network_state_handler.h"

namespace rmad {

SelectNetworkStateHandler::SelectNetworkStateHandler(
    scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {
  ResetState();
}

RmadState::StateCase SelectNetworkStateHandler::GetNextStateCase() const {
  if (state_.select_network().connection_state() !=
      SelectNetworkState::RMAD_NETWORK_UNKNOWN) {
    return RmadState::StateCase::kUpdateChrome;
  }
  // Not ready to go to next state.
  return GetStateCase();
}

RmadErrorCode SelectNetworkStateHandler::UpdateState(const RmadState& state) {
  CHECK(state.has_select_network())
      << "RmadState missing network selection state.";
  const SelectNetworkState& select_network = state.select_network();
  if (select_network.connection_state() ==
      SelectNetworkState::RMAD_NETWORK_UNKNOWN) {
    // TODO(gavindodd): What is correct error for unset/missing fields?
    return RMAD_ERROR_REQUEST_INVALID;
  }
  state_ = state;

  return RMAD_ERROR_OK;
}

RmadErrorCode SelectNetworkStateHandler::ResetState() {
  state_.set_allocated_select_network(new SelectNetworkState);

  return RMAD_ERROR_OK;
}

}  // namespace rmad
