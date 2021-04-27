// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/write_protect_enable_physical_state_handler.h"

namespace rmad {

WriteProtectEnablePhysicalStateHandler::WriteProtectEnablePhysicalStateHandler(
    scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {
  ResetState();
}

RmadState::StateCase WriteProtectEnablePhysicalStateHandler::GetNextStateCase()
    const {
  // TODO(chenghan): This is currently fake.
  return RmadState::StateCase::kFinalize;
}

RmadErrorCode WriteProtectEnablePhysicalStateHandler::UpdateState(
    const RmadState& state) {
  CHECK(state.has_wp_enable_physical()) << "RmadState missing provision state.";

  // Nothing to store.
  return RMAD_ERROR_OK;
}

RmadErrorCode WriteProtectEnablePhysicalStateHandler::ResetState() {
  state_.set_allocated_wp_enable_physical(new WriteProtectEnablePhysicalState);

  return RMAD_ERROR_OK;
}

}  // namespace rmad
