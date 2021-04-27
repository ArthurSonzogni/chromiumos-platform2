// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/provision_device_state_handler.h"

namespace rmad {

ProvisionDeviceStateHandler::ProvisionDeviceStateHandler(
    scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {
  ResetState();
}

RmadState::StateCase ProvisionDeviceStateHandler::GetNextStateCase() const {
  // TODO(chenghan): This is currently fake.
  return RmadState::StateCase::kWpEnablePhysical;
}

RmadErrorCode ProvisionDeviceStateHandler::UpdateState(const RmadState& state) {
  CHECK(state.has_provision_device()) << "RmadState missing provision state.";

  // Nothing to store.
  return RMAD_ERROR_OK;
}

RmadErrorCode ProvisionDeviceStateHandler::ResetState() {
  state_.set_allocated_provision_device(new ProvisionDeviceState);

  return RMAD_ERROR_OK;
}

}  // namespace rmad
