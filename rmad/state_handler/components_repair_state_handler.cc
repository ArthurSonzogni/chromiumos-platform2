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
    RMAD_COMPONENT_KEYBOARD,
    RMAD_COMPONENT_POWER_BUTTON,
};

// Convert the list of |ComponentRepairStatus| in |state| to a mapping table of
// component repair states. Unfortunately protobuf doesn't support enum as map
// keys so we can only store them in a list in protobuf and convert to a map
// internally.
std::unordered_map<RmadComponent, RepairStatus> ConvertStateToDictionary(
    const RmadState& state) {
  std::unordered_map<RmadComponent, RepairStatus> component_status_map;
  if (state.has_components_repair()) {
    const ComponentsRepairState& components_repair = state.components_repair();
    for (int i = 0; i < components_repair.component_repair_size(); ++i) {
      const ComponentRepairStatus& component_repair =
          components_repair.component_repair(i);
      const RmadComponent& component = component_repair.component();
      const RepairStatus& repair_status = component_repair.repair_status();
      if (component == RMAD_COMPONENT_UNKNOWN) {
        LOG(WARNING) << "RmadState component missing |component| field.";
        continue;
      }
      if (component_status_map.count(component) > 0) {
        LOG(WARNING) << "RmadState has duplicate components "
                     << RmadComponent_Name(component);
        continue;
      }
      component_status_map.insert({component, repair_status});
    }
  }
  return component_status_map;
}

// Convert a dictionary of {RmadComponent: RepairStatus} to a |RmadState|.
RmadState ConvertDictionaryToState(
    const std::unordered_map<RmadComponent, RepairStatus>& component_status_map,
    bool mainboard_rework) {
  auto components_repair = std::make_unique<ComponentsRepairState>();
  for (auto [component, repair_status] : component_status_map) {
    if (component == RMAD_COMPONENT_UNKNOWN) {
      LOG(WARNING) << "Dictionary contains UNKNOWN component";
      continue;
    }
    ComponentRepairStatus* component_repair =
        components_repair->add_component_repair();
    component_repair->set_component(component);
    component_repair->set_repair_status(repair_status);
  }
  components_repair->set_mainboard_rework(mainboard_rework);

  RmadState state;
  state.set_allocated_components_repair(components_repair.release());
  return state;
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
  // |state_| should always contain the full list of components, unless it's
  // just being created. Always probe again and use the probe results to update
  // |state_|.
  if (!state_.has_components_repair() && !RetrieveState()) {
    state_.set_allocated_components_repair(new ComponentsRepairState);
  }
  std::unordered_map<RmadComponent, RepairStatus> component_status_map =
      ConvertStateToDictionary(state_);

  // Call runtime_probe to get all probed components.
  // TODO(chenghan): Integrate with RACC to check AVL compliance.
  std::set<RmadComponent> probed_components;
  if (!runtime_probe_client_->ProbeCategories(&probed_components)) {
    LOG(ERROR) << "Failed to get probe result from runtime_probe";
    return RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED;
  }

  // Update probeable components using runtime_probe results.
  for (RmadComponent component : kProbeableComponents) {
    if (probed_components.count(component) > 0) {
      if (component_status_map.count(component) == 0 ||
          component_status_map[component] ==
              ComponentRepairStatus::RMAD_REPAIR_STATUS_MISSING) {
        component_status_map[component] =
            ComponentRepairStatus::RMAD_REPAIR_STATUS_UNKNOWN;
      }
    } else {
      component_status_map[component] =
          ComponentRepairStatus::RMAD_REPAIR_STATUS_MISSING;
    }
  }
  // Update unprobeable components. These components are never MISSING because
  // we cannot probe them.
  for (RmadComponent component : kUnprobeableComponents) {
    if (component_status_map.count(component) == 0) {
      component_status_map[component] =
          ComponentRepairStatus::RMAD_REPAIR_STATUS_UNKNOWN;
    }
  }

  state_ = ConvertDictionaryToState(
      component_status_map, state_.components_repair().mainboard_rework());
  return RMAD_ERROR_OK;
}

BaseStateHandler::GetNextStateCaseReply
ComponentsRepairStateHandler::GetNextStateCase(const RmadState& state) {
  if (!ApplyUserSelection(state)) {
    return {.error = RMAD_ERROR_REQUEST_INVALID, .state_case = GetStateCase()};
  }

  // Store the state to storage to keep user's selection.
  StoreState();
  StoreVars();

  return {.error = RMAD_ERROR_OK,
          .state_case = RmadState::StateCase::kDeviceDestination};
}

bool ComponentsRepairStateHandler::ApplyUserSelection(const RmadState& state) {
  if (!state.has_components_repair()) {
    LOG(ERROR) << "RmadState missing |components repair| state.";
    return false;
  }

  std::unordered_map<RmadComponent, RepairStatus> current_map =
      ConvertStateToDictionary(state_);
  const std::unordered_map<RmadComponent, RepairStatus> update_map =
      ConvertStateToDictionary(state);
  const bool mainboard_rework = state.components_repair().mainboard_rework();

  if (mainboard_rework) {
    // MLB rework. Set all the probed components to REPLACED.
    for (auto& [component, repair_status] : current_map) {
      if (repair_status != ComponentRepairStatus::RMAD_REPAIR_STATUS_MISSING) {
        repair_status = ComponentRepairStatus::RMAD_REPAIR_STATUS_REPLACED;
      }
    }
  } else {
    // Not MLB rework. Use |update_map| to update |current_map|.
    for (auto [component, repair_status] : update_map) {
      const std::string component_name = RmadComponent_Name(component);
      if (current_map.count(component) == 0) {
        LOG(ERROR) << "New state contains an unknown component "
                   << component_name;
        return false;
      }
      RepairStatus prev_repair_status = current_map[component];
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
      current_map[component] = repair_status;
    }
  }
  // Check if there are any components that still has UNKNOWN repair state.
  for (auto [component, repair_status] : current_map) {
    if (repair_status == ComponentRepairStatus::RMAD_REPAIR_STATUS_UNKNOWN) {
      LOG(ERROR) << "Component " << RmadComponent_Name(component)
                 << " has unknown repair state";
      return false;
    }
  }

  // Convert |current_map| back to |state_|.
  state_ = ConvertDictionaryToState(current_map, mainboard_rework);
  return true;
}

bool ComponentsRepairStateHandler::StoreVars() const {
  std::vector<std::string> replaced_components;
  const std::unordered_map<RmadComponent, RepairStatus> component_status_map =
      ConvertStateToDictionary(state_);

  for (auto [component, repair_status] : component_status_map) {
    if (repair_status == ComponentRepairStatus::RMAD_REPAIR_STATUS_REPLACED) {
      replaced_components.push_back(RmadComponent_Name(component));
    }
  }
  return json_store_->SetValue(kReplacedComponentNames, replaced_components);
}

}  // namespace rmad
