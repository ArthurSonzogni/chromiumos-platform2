// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/components_repair_state_handler.h"

#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include <dbus/runtime_probe/dbus-constants.h>
#include <runtime_probe/proto_bindings/runtime_probe.pb.h>

#include "rmad/utils/dbus_utils.h"

namespace rmad {

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

const std::vector<ComponentRepairState::Component> OTHER_COMPONENTS = {
    ComponentRepairState::RMAD_COMPONENT_MAINBOARD_REWORK,
    ComponentRepairState::RMAD_COMPONENT_KEYBOARD,
    ComponentRepairState::RMAD_COMPONENT_POWER_BUTTON,
};

}  // namespace

ComponentsRepairStateHandler::ComponentsRepairStateHandler(
    scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {}

RmadErrorCode ComponentsRepairStateHandler::InitializeState() {
  // Call runtime_probe to get all probed components.
  runtime_probe::ProbeRequest request;
  request.set_probe_default_category(true);
  runtime_probe::ProbeResult reply;
  if (!CallDBusMethod(runtime_probe::kRuntimeProbeServiceName,
                      runtime_probe::kRuntimeProbeServicePath,
                      runtime_probe::kRuntimeProbeInterfaceName,
                      runtime_probe::kProbeCategoriesMethod, request, &reply)) {
    LOG(ERROR) << "runtime_probe D-Bus call failed";
    return RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED;
  }
  if (reply.error() != runtime_probe::RUNTIME_PROBE_ERROR_NOT_SET) {
    LOG(ERROR) << "runtime_probe return error code " << reply.error();
    return RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED;
  }

  // Do not read from storage. Always probe again and update |state_|.
  // TODO(chenghan): Integrate with RACC to check AVL compliance.
  auto components_repair = std::make_unique<ComponentsRepairState>();
  const std::unordered_map<ComponentRepairState::Component,
                           ComponentRepairState::RepairState>
      previous_selection = GetUserSelectionDictionary();
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
  // Store the state to storage to keep user's selection.
  StoreState();

  return {.error = RMAD_ERROR_OK,
          .state_case = RmadState::StateCase::kDeviceDestination};
}

bool ComponentsRepairStateHandler::VerifyInput(const RmadState& state) const {
  // TODO(chenghan): Verify if the user selected components are probed on the
  //                 device.
  return true;
}

std::unordered_map<ComponentRepairState::Component,
                   ComponentRepairState::RepairState>
ComponentsRepairStateHandler::GetUserSelectionDictionary() const {
  std::unordered_map<ComponentRepairState::Component,
                     ComponentRepairState::RepairState>
      selection_dict;
  if (state_.has_components_repair()) {
    const ComponentsRepairState& components_repair = state_.components_repair();
    for (int i = 0; i < components_repair.components_size(); ++i) {
      const ComponentRepairState& component = components_repair.components(i);
      if (component.repair_state() !=
          ComponentRepairState::RMAD_REPAIR_UNKNOWN) {
        selection_dict.insert({component.name(), component.repair_state()});
      }
    }
  }
  return selection_dict;
}

}  // namespace rmad
