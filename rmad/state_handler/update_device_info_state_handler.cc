// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/update_device_info_state_handler.h"

namespace rmad {

UpdateDeviceInfoStateHandler::UpdateDeviceInfoStateHandler(
    scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {
  ResetState();
}

RmadState::StateCase UpdateDeviceInfoStateHandler::GetNextStateCase() const {
  // TODO(chenghan): This is currently fake.
  return RmadState::StateCase::kCalibrateComponents;
}

RmadErrorCode UpdateDeviceInfoStateHandler::UpdateState(
    const RmadState& state) {
  CHECK(state.has_update_device_info())
      << "RmadState missing update device info state.";
  // TODO(chenghan): Validate the values.
  state_ = state;

  return RMAD_ERROR_OK;
}

RmadErrorCode UpdateDeviceInfoStateHandler::ResetState() {
  state_.set_allocated_update_device_info(new UpdateDeviceInfoState);

  return RMAD_ERROR_OK;
}

}  // namespace rmad
