// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/components_repair_state_handler.h"

#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "rmad/constants.h"
#include "rmad/system/runtime_probe_client_impl.h"
#include "rmad/utils/dbus_utils.h"

#include <base/logging.h>

namespace rmad {

using ComponentRepairStatus = ComponentsRepairState::ComponentRepairStatus;
using RepairStatus = ComponentRepairStatus::RepairStatus;

namespace {

const std::vector<RmadComponent> kProbeableComponents = {
    RMAD_COMPONENT_AUDIO_CODEC,   RMAD_COMPONENT_BATTERY,
    RMAD_COMPONENT_STORAGE,       RMAD_COMPONENT_CAMERA,
    RMAD_COMPONENT_STYLUS,        RMAD_COMPONENT_TOUCHPAD,
    RMAD_COMPONENT_TOUCHSCREEN,   RMAD_COMPONENT_DRAM,
    RMAD_COMPONENT_DISPLAY_PANEL, RMAD_COMPONENT_CELLULAR,
    RMAD_COMPONENT_ETHERNET,      RMAD_COMPONENT_WIRELESS,
};

const std::vector<RmadComponent> kUnprobeableComponents = {
    RMAD_COMPONENT_MAINBOARD_REWORK,
    RMAD_COMPONENT_KEYBOARD,
    RMAD_COMPONENT_POWER_BUTTON,
};

// Convert the list of |ComponentRepairStatus| to a mapping table of component
// repair states. Unfortunately protobuf doesn't support enum as map keys so we
// can only store them in a list in protobuf and convert to a map internally.
std::unordered_map<RmadComponent, RepairStatus> GetUserSelectionDictionary(
    const RmadState& state) {
  std::unordered_map<RmadComponent, RepairStatus> selection_dict;
  if (state.has_components_repair()) {
    const ComponentsRepairState& components_repair = state.components_repair();
    for (int i = 0; i < components_repair.component_repair_size(); ++i) {
      const ComponentRepairStatus& component_repair =
          components_repair.component_repair(i);
      const RmadComponent& component = component_repair.component();
      const RepairStatus& repair_status = component_repair.repair_status();
      if (component == RMAD_COMPONENT_UNKNOWN) {
        LOG(WARNING) << "RmadState component missing |component| argument.";
        continue;
      }
      if (selection_dict.find(component) != selection_dict.end()) {
        LOG(WARNING) << "RmadState has duplicate components "
                     << RmadComponent_Name(component);
        continue;
      }
      selection_dict.insert({component, repair_status});
    }
  }
  return selection_dict;
}

}  // namespace

ComponentsRepairStateHandler::ComponentsRepairStateHandler(
    scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {
  runtime_probe_client_ =
      std::make_unique<RuntimeProbeClientImpl>(GetSystemBus());
}

ComponentsRepairStateHandler::ComponentsRepairStateHandler(
    scoped_refptr<JsonStore> json_store,
    std::unique_ptr<RuntimeProbeClient> runtime_probe_client)
    : BaseStateHandler(json_store),
      runtime_probe_client_(std::move(runtime_probe_client)) {}

RmadErrorCode ComponentsRepairStateHandler::InitializeState() {
  // Always probe again and update |state_|.
  // Call runtime_probe to get all probed components.
  std::set<RmadComponent> probed_components;
  if (!runtime_probe_client_->ProbeCategories(&probed_components)) {
    LOG(ERROR) << "Failed to get probe result from runtime_probe";
    return RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED;
  }

  // TODO(chenghan): Integrate with RACC to check AVL compliance.
  auto components_repair = std::make_unique<ComponentsRepairState>();
  const std::unordered_map<RmadComponent, RepairStatus> previous_selection =
      GetUserSelectionDictionary(state_);
  // runtime_probe results.
  for (RmadComponent component : kProbeableComponents) {
    ComponentRepairStatus* component_repair =
        components_repair->add_component_repair();
    component_repair->set_component(component);
    // TODO(chenghan): Do we need to return detailed info, e.g. component names?
    if (probed_components.find(component) != probed_components.end()) {
      if (previous_selection.find(component) != previous_selection.end()) {
        component_repair->set_repair_status(previous_selection.at(component));
      } else {
        component_repair->set_repair_status(
            ComponentRepairStatus::RMAD_REPAIR_STATUS_UNKNOWN);
      }
    } else {
      component_repair->set_repair_status(
          ComponentRepairStatus::RMAD_REPAIR_STATUS_MISSING);
    }
  }
  // Other components.
  for (RmadComponent component : kUnprobeableComponents) {
    ComponentRepairStatus* component_repair =
        components_repair->add_component_repair();
    component_repair->set_component(component);
    component_repair->set_repair_status(
        ComponentRepairStatus::RMAD_REPAIR_STATUS_UNKNOWN);
  }
  state_.set_allocated_components_repair(components_repair.release());

  return RMAD_ERROR_OK;
}

BaseStateHandler::GetNextStateCaseReply
ComponentsRepairStateHandler::GetNextStateCase(const RmadState& state) {
  if (!ValidateUserSelection(state)) {
    return {.error = RMAD_ERROR_REQUEST_INVALID, .state_case = GetStateCase()};
  }

  state_ = state;
  // Store the state to storage to keep user's selection.
  StoreState();
  StoreVars();

  return {.error = RMAD_ERROR_OK,
          .state_case = RmadState::StateCase::kDeviceDestination};
}

bool ComponentsRepairStateHandler::ValidateUserSelection(
    const RmadState& state) const {
  if (!state.has_components_repair()) {
    LOG(ERROR) << "RmadState missing |components repair| state.";
    return false;
  }
  std::unordered_map<RmadComponent, RepairStatus> prev_user_selection =
      GetUserSelectionDictionary(state_);
  std::unordered_map<RmadComponent, RepairStatus> user_selection =
      GetUserSelectionDictionary(state);

  // Use |user_selection| to update |prev_user_selection|.
  for (auto [component, repair_status] : user_selection) {
    const std::string component_name = RmadComponent_Name(component);
    if (prev_user_selection.find(component) == prev_user_selection.end()) {
      LOG(ERROR) << "New state contains an unknown component "
                 << component_name;
      return false;
    }
    RepairStatus prev_repair_status = prev_user_selection[component];
    if (prev_repair_status ==
            ComponentRepairStatus::RMAD_REPAIR_STATUS_MISSING &&
        repair_status != ComponentRepairStatus::RMAD_REPAIR_STATUS_MISSING) {
      LOG(ERROR) << "New state contains repair state for unprobed component "
                 << component_name;
      return false;
    }
    if (prev_repair_status !=
            ComponentRepairStatus::RMAD_REPAIR_STATUS_MISSING &&
        repair_status == ComponentRepairStatus::RMAD_REPAIR_STATUS_MISSING) {
      LOG(ERROR) << "New state missing repair state for component "
                 << component_name;
      return false;
    }
    prev_user_selection[component] = repair_status;
  }
  // Check if there are any components that still has UNKNOWN repair state.
  for (auto [component, updated_repair_status] : prev_user_selection) {
    const std::string component_name = RmadComponent_Name(component);
    if (updated_repair_status ==
        ComponentRepairStatus::RMAD_REPAIR_STATUS_UNKNOWN) {
      LOG(ERROR) << "Component " << component_name
                 << " has unknown repair state";
      return false;
    }
  }

  return true;
}

bool ComponentsRepairStateHandler::StoreVars() const {
  std::vector<std::string> replaced_components;
  const std::unordered_map<RmadComponent, RepairStatus> user_selection =
      GetUserSelectionDictionary(state_);

  for (auto [component, repair_status] : user_selection) {
    if (repair_status == ComponentRepairStatus::RMAD_REPAIR_STATUS_REPLACED) {
      replaced_components.push_back(RmadComponent_Name(component));
    }
  }
  return json_store_->SetValue(kReplacedComponentNames, replaced_components);
}

}  // namespace rmad
