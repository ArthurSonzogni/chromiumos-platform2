// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/components_repair_state_handler.h"

namespace rmad {

ComponentsRepairStateHandler::ComponentsRepairStateHandler(
    scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {
  ResetState();
}

RmadState::StateCase ComponentsRepairStateHandler::GetNextStateCase() const {
  if (VerifyComponents()) {
    return RmadState::StateCase::kDeviceDestination;
  }
  // Not ready to go to next state.
  return GetStateCase();
}

RmadErrorCode ComponentsRepairStateHandler::UpdateState(
    const RmadState& state) {
  CHECK(state.has_select_components())
      << "RmadState missing select component state.";
  const ComponentsRepairState& components_repair = state.select_components();
  for (int i = 0; i < components_repair.components_size(); ++i) {
    if (const ComponentRepairState& component = components_repair.components(i);
        component.name() == ComponentRepairState::RMAD_COMPONENT_UNKNOWN ||
        component.repair_state() == ComponentRepairState::RMAD_REPAIR_UNKNOWN) {
      return RMAD_ERROR_REQUEST_INVALID;
    }
  }
  // TODO(chenghan): Check |repair_state| for all the components.
  state_ = state;

  return RMAD_ERROR_OK;
}

RmadErrorCode ComponentsRepairStateHandler::ResetState() {
  state_.set_allocated_select_components(new ComponentsRepairState);

  return RMAD_ERROR_OK;
}

bool ComponentsRepairStateHandler::VerifyComponents() const {
  // TODO(chenghan): Verify if the selected components are probeable.
  return true;
}

}  // namespace rmad
