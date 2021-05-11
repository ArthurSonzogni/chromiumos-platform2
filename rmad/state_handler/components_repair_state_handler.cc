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

BaseStateHandler::GetNextStateCaseReply
ComponentsRepairStateHandler::GetNextStateCase(const RmadState& state) {
  if (!state.has_components_repair()) {
    LOG(ERROR) << "RmadState missing |select component| state.";
    return {.error = RMAD_ERROR_REQUEST_INVALID, .state_case = GetStateCase()};
  }
  const ComponentsRepairState& components_repair = state.components_repair();
  for (int i = 0; i < components_repair.components_size(); ++i) {
    const ComponentRepairState& component = components_repair.components(i);
    if (component.name() == ComponentRepairState::RMAD_COMPONENT_UNKNOWN) {
      LOG(ERROR) << "RmadState component missing |name| argument.";
      return {.error = RMAD_ERROR_REQUEST_INVALID,
              .state_case = GetStateCase()};
    }
    if (component.repair_state() == ComponentRepairState::RMAD_REPAIR_UNKNOWN) {
      LOG(ERROR) << "RmadState component missing |repair_state| argument.";
      return {.error = RMAD_ERROR_REQUEST_INVALID,
              .state_case = GetStateCase()};
    }
  }

  state_ = state;
  if (!VerifyComponents()) {
    LOG(ERROR) << "Component verification failed.";
    // TODO(chenghan): Create a more specific error code.
    return {.error = RMAD_ERROR_TRANSITION_FAILED,
            .state_case = GetStateCase()};
  }
  return {.error = RMAD_ERROR_OK,
          .state_case = RmadState::StateCase::kDeviceDestination};
}

RmadErrorCode ComponentsRepairStateHandler::ResetState() {
  state_.set_allocated_components_repair(new ComponentsRepairState);

  return RMAD_ERROR_OK;
}

bool ComponentsRepairStateHandler::VerifyComponents() const {
  // TODO(chenghan): Verify if the selected components are probeable.
  return true;
}

}  // namespace rmad
