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

#include "rmad/constants.h"
#include "rmad/utils/dbus_utils_impl.h"

namespace rmad {

using ComponentRepairStatus = ComponentsRepairState::ComponentRepairStatus;
using RepairStatus = ComponentRepairStatus::RepairStatus;

namespace {

const std::vector<
    std::pair<RmadComponent, int (runtime_probe::ProbeResult::*)() const>>
    PROBED_COMPONENT_SIZES = {
        {RMAD_COMPONENT_AUDIO_CODEC,
         &runtime_probe::ProbeResult::audio_codec_size},
        {RMAD_COMPONENT_BATTERY, &runtime_probe::ProbeResult::battery_size},
        {RMAD_COMPONENT_STORAGE, &runtime_probe::ProbeResult::storage_size},
        {RMAD_COMPONENT_CAMERA, &runtime_probe::ProbeResult::camera_size},
        {RMAD_COMPONENT_STYLUS, &runtime_probe::ProbeResult::stylus_size},
        {RMAD_COMPONENT_TOUCHPAD, &runtime_probe::ProbeResult::touchpad_size},
        {RMAD_COMPONENT_TOUCHSCREEN,
         &runtime_probe::ProbeResult::touchscreen_size},
        {RMAD_COMPONENT_DRAM, &runtime_probe::ProbeResult::dram_size},
        {RMAD_COMPONENT_DISPLAY_PANEL,
         &runtime_probe::ProbeResult::display_panel_size},
        {RMAD_COMPONENT_CELLULAR, &runtime_probe::ProbeResult::cellular_size},
        {RMAD_COMPONENT_ETHERNET, &runtime_probe::ProbeResult::ethernet_size},
        {RMAD_COMPONENT_WIRELESS, &runtime_probe::ProbeResult::wireless_size},
};

const std::vector<RmadComponent> OTHER_COMPONENTS = {
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
  const std::unordered_map<RmadComponent, RepairStatus> previous_selection =
      GetUserSelectionDictionary(state_);
  // runtime_probe results.
  for (auto& [component, probed_component_size] : PROBED_COMPONENT_SIZES) {
    ComponentRepairStatus* component_repair =
        components_repair->add_component_repair();
    component_repair->set_component(component);
    // TODO(chenghan): Do we need to return detailed info, e.g. component names?
    if ((reply.*probed_component_size)() > 0) {
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
  for (auto& component : OTHER_COMPONENTS) {
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
