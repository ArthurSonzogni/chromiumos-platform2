// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/select_network_state_handler.h"

namespace rmad {

SelectNetworkStateHandler::SelectNetworkStateHandler(
    scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {}

RmadErrorCode SelectNetworkStateHandler::InitializeState() {
  if (!state_.has_select_network() && !RetrieveState()) {
    state_.set_allocated_select_network(new SelectNetworkState);
  }
  return RMAD_ERROR_OK;
}

BaseStateHandler::GetNextStateCaseReply
SelectNetworkStateHandler::GetNextStateCase(const RmadState& state) {
  if (!state.has_select_network()) {
    LOG(ERROR) << "RmadState missing |network selection| state.";
    return {.error = RMAD_ERROR_REQUEST_INVALID, .state_case = GetStateCase()};
  }
  const SelectNetworkState& select_network = state.select_network();
  if (select_network.connection_state() ==
      SelectNetworkState::RMAD_NETWORK_UNKNOWN) {
    return {.error = RMAD_ERROR_REQUEST_ARGS_MISSING,
            .state_case = GetStateCase()};
  }

  state_ = state;
  StoreState();

  return {.error = RMAD_ERROR_OK,
          .state_case = RmadState::StateCase::kUpdateChrome};
}

}  // namespace rmad
