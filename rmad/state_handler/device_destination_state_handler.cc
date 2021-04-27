// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/device_destination_state_handler.h"

namespace rmad {

DeviceDestinationStateHandler::DeviceDestinationStateHandler(
    scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {
  ResetState();
}

RmadState::StateCase DeviceDestinationStateHandler::GetNextStateCase() const {
  if (state_.device_destination().destination() !=
      DeviceDestinationState::RMAD_DESTINATION_UNKNOWN) {
    return RmadState::StateCase::kWpDisableMethod;
  }
  // Not ready to go to next state.
  return GetStateCase();
}

RmadErrorCode DeviceDestinationStateHandler::UpdateState(
    const RmadState& state) {
  CHECK(state.has_device_destination())
      << "RmadState missing device destination state.";
  const DeviceDestinationState& device_destination = state.device_destination();
  if (device_destination.destination() ==
      DeviceDestinationState::RMAD_DESTINATION_UNKNOWN) {
    // TODO(gavindodd): What is correct error for unset/missing fields?
    return RMAD_ERROR_REQUEST_INVALID;
  }
  state_ = state;

  return RMAD_ERROR_OK;
}

RmadErrorCode DeviceDestinationStateHandler::ResetState() {
  state_.set_allocated_device_destination(new DeviceDestinationState);

  return RMAD_ERROR_OK;
}

}  // namespace rmad
