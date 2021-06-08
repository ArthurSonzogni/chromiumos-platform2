// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/update_device_info_state_handler.h"

namespace rmad {

UpdateDeviceInfoStateHandler::UpdateDeviceInfoStateHandler(
    scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {}

RmadErrorCode UpdateDeviceInfoStateHandler::InitializeState() {
  if (!state_.has_update_device_info() && !RetrieveState()) {
    state_.set_allocated_update_device_info(new UpdateDeviceInfoState);
  }
  return RMAD_ERROR_OK;
}

BaseStateHandler::GetNextStateCaseReply
UpdateDeviceInfoStateHandler::GetNextStateCase(const RmadState& state) {
  if (!state.has_update_device_info()) {
    LOG(ERROR) << "RmadState missing |update device info| state.";
    return {.error = RMAD_ERROR_REQUEST_INVALID, .state_case = GetStateCase()};
  }
  // TODO(chenghan): Validate the values.

  state_ = state;
  StoreState();

  // TODO(chenghan): This is currently fake.
  return {.error = RMAD_ERROR_OK,
          .state_case = RmadState::StateCase::kCheckCalibration};
}

}  // namespace rmad
