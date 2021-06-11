// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/components_repair_state_handler.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <dbus/runtime_probe/dbus-constants.h>
#include <runtime_probe/proto_bindings/runtime_probe.pb.h>

#include "rmad/utils/dbus_utils_impl.h"

namespace rmad {

using Component = ComponentRepairState::Component;
using RepairState = ComponentRepairState::RepairState;

namespace {

const std::vector<std::pair<ComponentRepairState::Component,
                            int (runtime_probe::ProbeResult::*)() const>>
    PROBED_COMPONENT_SIZES = {
        {ComponentRepairState::RMAD_COMPONENT_AUDIO_CODEC,
         &runtime_probe::ProbeResult::audio_codec_size},
        {ComponentRepairState::RMAD_COMPONENT_BATTERY,
         &runtime_probe::ProbeResult::battery_size},
        {ComponentRepairState::RMAD_COMPONENT_STORAGE,
         &runtime_probe::ProbeResult::storage_size},
        {ComponentRepairState::RMAD_COMPONENT_CAMERA,
         &runtime_probe::ProbeResult::camera_size},
        {ComponentRepairState::RMAD_COMPONENT_STYLUS,
         &runtime_probe::ProbeResult::stylus_size},
        {ComponentRepairState::RMAD_COMPONENT_TOUCHPAD,
         &runtime_probe::ProbeResult::touchpad_size},
        {ComponentRepairState::RMAD_COMPONENT_TOUCHSCREEN,
         &runtime_probe::ProbeResult::touchscreen_size},
        {ComponentRepairState::RMAD_COMPONENT_DRAM,
         &runtime_probe::ProbeResult::dram_size},
        {ComponentRepairState::RMAD_COMPONENT_DISPLAY_PANEL,
         &runtime_probe::ProbeResult::display_panel_size},
        {ComponentRepairState::RMAD_COMPONENT_CELLULAR,
         &runtime_probe::ProbeResult::cellular_size},
        {ComponentRepairState::RMAD_COMPONENT_ETHERNET,
         &runtime_probe::ProbeResult::ethernet_size},
        {ComponentRepairState::RMAD_COMPONENT_WIRELESS,
         &runtime_probe::ProbeResult::wireless_size},
};

const std::vector<Component> OTHER_COMPONENTS = {
    ComponentRepairState::RMAD_COMPONENT_MAINBOARD_REWORK,
    ComponentRepairState::RMAD_COMPONENT_KEYBOARD,
    ComponentRepairState::RMAD_COMPONENT_POWER_BUTTON,
};

// Convert the list of |ComponentRepairState| to a mapping table of component
// repair states. Unfortunately protobuf doesn't support enum as map keys so we
// can only store them in a list in protobuf and convert to a map internally.
std::unordered_map<Component, RepairState> GetUserSelectionDictionary(
    const RmadState& state) {
  std::unordered_map<Component, RepairState> selection_dict;
  if (state.has_components_repair()) {
    const ComponentsRepairState& components_repair = state.components_repair();
    for (int i = 0; i < components_repair.components_size(); ++i) {
      const ComponentRepairState& component_repair =
          components_repair.components(i);
      const Component& component = component_repair.name();
      const RepairState& repair_state = component_repair.repair_state();
      if (component == ComponentRepairState::RMAD_COMPONENT_UNKNOWN) {
        LOG(WARNING) << "RmadState component missing |name| argument.";
        continue;
      }
      if (selection_dict.find(component) != selection_dict.end()) {
        LOG(WARNING) << "RmadState has duplicate components "
                     << ComponentRepairState::Component_Name(component);
        continue;
      }
      selection_dict.insert({component, repair_state});
    }
  }
  return selection_dict;
}

}  // namespace

ComponentsRepairStateHandler::ComponentsRepairStateHandler(
    scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {
  dbus_utils_ = std::make_unique<DBusUtilsImpl>();
}

ComponentsRepairStateHandler::ComponentsRepairStateHandler(
    scoped_refptr<JsonStore> json_store, std::unique_ptr<DBusUtils> dbus_utils)
    : BaseStateHandler(json_store), dbus_utils_(std::move(dbus_utils)) {}

RmadErrorCode ComponentsRepairStateHandler::InitializeState() {
  // Call runtime_probe to get all probed components.
  runtime_probe::ProbeRequest request;
  request.set_probe_default_category(true);
  runtime_probe::ProbeResult reply;
  if (!dbus_utils_->CallDBusMethod(runtime_probe::kRuntimeProbeServiceName,
                                   runtime_probe::kRuntimeProbeServicePath,
                                   runtime_probe::kRuntimeProbeInterfaceName,
                                   runtime_probe::kProbeCategoriesMethod,
                                   request, &reply)) {
    LOG(ERROR) << "runtime_probe D-Bus call failed";
    return RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED;
  }
  if (reply.error() != runtime_probe::RUNTIME_PROBE_ERROR_NOT_SET) {
    LOG(ERROR) << "runtime_probe return error code " << reply.error();
    return RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED;
  }

  // Always probe again and update |state_|.
  // TODO(chenghan): Integrate with RACC to check AVL compliance.
  auto components_repair = std::make_unique<ComponentsRepairState>();
  const std::unordered_map<Component, RepairState> previous_selection =
      GetUserSelectionDictionary(state_);
  // runtime_probe results.
  for (auto& [component, probed_component_size] : PROBED_COMPONENT_SIZES) {
    ComponentRepairState* component_repair =
        components_repair->add_components();
    component_repair->set_name(component);
    // TODO(chenghan): Do we need to return detailed info, e.g. component names?
    if ((reply.*probed_component_size)() > 0) {
      if (previous_selection.find(component) != previous_selection.end()) {
        component_repair->set_repair_state(previous_selection.at(component));
      } else {
        component_repair->set_repair_state(
            ComponentRepairState::RMAD_REPAIR_UNKNOWN);
      }
    } else {
      component_repair->set_repair_state(
          ComponentRepairState::RMAD_REPAIR_MISSING);
    }
  }
  // Other components.
  for (auto& component : OTHER_COMPONENTS) {
    ComponentRepairState* component_repair =
        components_repair->add_components();
    component_repair->set_name(component);
    component_repair->set_repair_state(
        ComponentRepairState::RMAD_REPAIR_UNKNOWN);
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

  return {.error = RMAD_ERROR_OK,
          .state_case = RmadState::StateCase::kDeviceDestination};
}

bool ComponentsRepairStateHandler::ValidateUserSelection(
    const RmadState& state) const {
  if (!state.has_components_repair()) {
    LOG(ERROR) << "RmadState missing |components repair| state.";
    return false;
  }
  std::unordered_map<Component, RepairState> prev_user_selection =
      GetUserSelectionDictionary(state_);
  std::unordered_map<Component, RepairState> user_selection =
      GetUserSelectionDictionary(state);

  // Use |user_selection| to update |prev_user_selection|.
  for (auto [component, repair_state] : user_selection) {
    const std::string component_name =
        ComponentRepairState::Component_Name(component);
    if (prev_user_selection.find(component) == prev_user_selection.end()) {
      LOG(ERROR) << "New state contains an unknown component "
                 << component_name;
      return false;
    }
    RepairState prev_repair_state = prev_user_selection[component];
    if (prev_repair_state == ComponentRepairState::RMAD_REPAIR_MISSING &&
        repair_state != ComponentRepairState::RMAD_REPAIR_MISSING) {
      LOG(ERROR) << "New state contains repair state for unprobed component "
                 << component_name;
      return false;
    }
    if (prev_repair_state != ComponentRepairState::RMAD_REPAIR_MISSING &&
        repair_state == ComponentRepairState::RMAD_REPAIR_MISSING) {
      LOG(ERROR) << "New state missing repair state for component "
                 << component_name;
      return false;
    }
    prev_user_selection[component] = repair_state;
  }
  // Check if there are any components that still has UNKNOWN repair state.
  for (auto [component, updated_repair_state] : prev_user_selection) {
    const std::string component_name =
        ComponentRepairState::Component_Name(component);
    if (updated_repair_state == ComponentRepairState::RMAD_REPAIR_UNKNOWN) {
      LOG(ERROR) << "Component " << component_name
                 << " has unknown repair state";
      return false;
    }
  }

  return true;
}

}  // namespace rmad
