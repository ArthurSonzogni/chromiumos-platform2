// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/write_protect_disable_physical_state_handler.h"

namespace rmad {

WriteProtectDisablePhysicalStateHandler::
    WriteProtectDisablePhysicalStateHandler(scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {
  ResetState();
}

RmadState::StateCase WriteProtectDisablePhysicalStateHandler::GetNextStateCase()
    const {
  if (CheckWriteProtectionOn()) {
    return RmadState::StateCase::kWpDisableComplete;
  }
  // Not ready to go to next state.
  return GetStateCase();
}

RmadErrorCode WriteProtectDisablePhysicalStateHandler::UpdateState(
    const RmadState& state) {
  CHECK(state.has_wp_disable_physical())
      << "RmadState missing physical write protection state.";

  // There's nothing in |WriteProtectionDisablePhysicalState|.
  return RMAD_ERROR_OK;
}

RmadErrorCode WriteProtectDisablePhysicalStateHandler::ResetState() {
  state_.set_allocated_wp_disable_physical(
      new WriteProtectDisablePhysicalState);

  return RMAD_ERROR_OK;
}

bool WriteProtectDisablePhysicalStateHandler::CheckWriteProtectionOn() const {
  // TODO(chenghan): Get the info from crossystem.
  return false;
}

}  // namespace rmad
