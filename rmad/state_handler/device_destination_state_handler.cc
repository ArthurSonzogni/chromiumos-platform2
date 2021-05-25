// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/device_destination_state_handler.h"

#include "rmad/constants.h"

namespace rmad {

DeviceDestinationStateHandler::DeviceDestinationStateHandler(
    scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {}

RmadErrorCode DeviceDestinationStateHandler::InitializeState() {
  if (!state_.has_device_destination() && !RetrieveState()) {
    state_.set_allocated_device_destination(new DeviceDestinationState);
  }
  return RMAD_ERROR_OK;
}

BaseStateHandler::GetNextStateCaseReply
DeviceDestinationStateHandler::GetNextStateCase(const RmadState& state) {
  if (!state.has_device_destination()) {
    LOG(ERROR) << "RmadState missing |device destination| state.";
    return {.error = RMAD_ERROR_REQUEST_INVALID, .state_case = GetStateCase()};
  }
  const DeviceDestinationState& device_destination = state.device_destination();
  if (device_destination.destination() ==
      DeviceDestinationState::RMAD_DESTINATION_UNKNOWN) {
    LOG(ERROR) << "RmadState missing |destination| argument.";
    return {.error = RMAD_ERROR_REQUEST_ARGS_MISSING,
            .state_case = GetStateCase()};
  }

  state_ = state;
  StoreState();
  StoreVars();

  return {.error = RMAD_ERROR_OK,
          .state_case = RmadState::StateCase::kWpDisableMethod};
}

bool DeviceDestinationStateHandler::StoreVars() const {
  return json_store_->SetValue(
      kSameOwner, state_.device_destination().destination() ==
                      DeviceDestinationState::RMAD_DESTINATION_SAME);
}

}  // namespace rmad
