// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/calibrate_components_state_handler.h"

namespace rmad {

CalibrateComponentsStateHandler::CalibrateComponentsStateHandler(
    scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {
  ResetState();
}

RmadState::StateCase CalibrateComponentsStateHandler::GetNextStateCase() const {
  // TODO(chenghan): This is currently fake.
  return RmadState::StateCase::kProvisionDevice;
}

RmadErrorCode CalibrateComponentsStateHandler::UpdateState(
    const RmadState& state) {
  CHECK(state.has_calibrate_components())
      << "RmadState missing calibrate components state.";

  // Nothing to store.
  return RMAD_ERROR_OK;
}

RmadErrorCode CalibrateComponentsStateHandler::ResetState() {
  state_.set_allocated_calibrate_components(new CalibrateComponentsState);

  return RMAD_ERROR_OK;
}

}  // namespace rmad
