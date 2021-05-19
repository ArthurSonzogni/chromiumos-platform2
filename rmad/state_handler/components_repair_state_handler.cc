// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/components_repair_state_handler.h"

#include <memory>
#include <vector>

namespace rmad {

ComponentsRepairStateHandler::ComponentsRepairStateHandler(
    scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {}

RmadErrorCode ComponentsRepairStateHandler::InitializeState() {
  // Do not read from storage. Always probe again and update |state_|.
  auto components_repair = std::make_unique<ComponentsRepairState>();

  // TODO(chenghan): This is currently fake.
  const std::vector<ComponentRepairState::Component> component_name_list{
      ComponentRepairState::RMAD_COMPONENT_KEYBOARD,
      ComponentRepairState::RMAD_COMPONENT_SCREEN,
      ComponentRepairState::RMAD_COMPONENT_TRACKPAD};
  for (ComponentRepairState::Component name : component_name_list) {
    ComponentRepairState* component_repair =
        components_repair->add_components();
    component_repair->set_name(name);
    component_repair->set_repair_state(
        ComponentRepairState::RMAD_REPAIR_UNKNOWN);
  }
  // TODO(chenghan): Use RetrieveState() to get previous user's selection.

  state_.set_allocated_components_repair(components_repair.release());
  return RMAD_ERROR_OK;
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

  // Verify if the selected components in the input state matches the probed
  // components.
  if (!VerifyInput(state)) {
    LOG(ERROR) << "Input verification failed.";
    // TODO(chenghan): Create a more specific error code.
    return {.error = RMAD_ERROR_TRANSITION_FAILED,
            .state_case = GetStateCase()};
  }

  state_ = state;
  // Still store the state to storage to keep user's selection.
  StoreState();

  return {.error = RMAD_ERROR_OK,
          .state_case = RmadState::StateCase::kDeviceDestination};
}

bool ComponentsRepairStateHandler::VerifyInput(const RmadState& state) const {
  // TODO(chenghan): Verify if the user selected components are probed on the
  //                 device.
  return true;
}

}  // namespace rmad
