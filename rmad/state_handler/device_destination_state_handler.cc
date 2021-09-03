// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/device_destination_state_handler.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/logging.h>

#include "rmad/constants.h"
#include "rmad/proto_bindings/rmad.pb.h"
#include "rmad/utils/cr50_utils_impl.h"

namespace rmad {

using ComponentRepairStatus = ComponentsRepairState::ComponentRepairStatus;

DeviceDestinationStateHandler::DeviceDestinationStateHandler(
    scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {
  cr50_utils_ = std::make_unique<Cr50UtilsImpl>();
}

DeviceDestinationStateHandler::DeviceDestinationStateHandler(
    scoped_refptr<JsonStore> json_store, std::unique_ptr<Cr50Utils> cr50_utils)
    : BaseStateHandler(json_store), cr50_utils_(std::move(cr50_utils)) {}

RmadErrorCode DeviceDestinationStateHandler::InitializeState() {
  if (!state_.has_device_destination()) {
    state_.set_allocated_device_destination(new DeviceDestinationState);
  }
  return RMAD_ERROR_OK;
}

BaseStateHandler::GetNextStateCaseReply
DeviceDestinationStateHandler::GetNextStateCase(const RmadState& state) {
  if (!state.has_device_destination()) {
    LOG(ERROR) << "RmadState missing |device destination| state.";
    return {.error = RMAD_ERROR_REQUEST_INVALID, .state_case = GetStateCase()};
  }
  const DeviceDestinationState& device_destination = state.device_destination();
  if (device_destination.destination() ==
      DeviceDestinationState::RMAD_DESTINATION_UNKNOWN) {
    LOG(ERROR) << "RmadState missing |destination| argument.";
    return {.error = RMAD_ERROR_REQUEST_ARGS_MISSING,
            .state_case = GetStateCase()};
  }

  state_ = state;
  StoreVars();

  // Check if conditions are met to skip disabling write protection and directly
  // go to Finalize step.
  if (CanSkipHwwp()) {
    return {.error = RMAD_ERROR_OK,
            .state_case = RmadState::StateCase::kFinalize};
  }

  // If factory mode is already enabled, go directly to WpDisableComplete state.
  if (cr50_utils_->IsFactoryModeEnabled()) {
    json_store_->SetValue(kWpDisableSkipped, true);
    return {.error = RMAD_ERROR_OK,
            .state_case = RmadState::StateCase::kWpDisableComplete};
  }

  return {.error = RMAD_ERROR_OK,
          .state_case = RmadState::StateCase::kWpDisableMethod};
}

bool DeviceDestinationStateHandler::StoreVars() const {
  return json_store_->SetValue(
      kSameOwner, state_.device_destination().destination() ==
                      DeviceDestinationState::RMAD_DESTINATION_SAME);
}

bool DeviceDestinationStateHandler::CanSkipHwwp() const {
  // Device should go to the same owner, and no replaced component needs
  // calibration.
  bool same_owner;
  if (!json_store_->GetValue(kSameOwner, &same_owner) || !same_owner) {
    return false;
  }

  std::vector<std::string> replaced_component_names;
  if (!json_store_->GetValue(kReplacedComponentNames,
                             &replaced_component_names)) {
    return false;
  }

  // Using set union/intersection is faster, but since the number of components
  // is small, we keep it simple by using linear searches for each replaced
  // component. It shouldn't affect performance a lot.
  for (const std::string& component_name : replaced_component_names) {
    RmadComponent component;
    DCHECK(RmadComponent_Parse(component_name, &component));
    if (std::find(kComponentsNeedManualCalibration.begin(),
                  kComponentsNeedManualCalibration.end(),
                  component) != kComponentsNeedManualCalibration.end()) {
      return false;
    }
    if (std::find(kComponentsNeedAutoCalibration.begin(),
                  kComponentsNeedAutoCalibration.end(),
                  component) != kComponentsNeedAutoCalibration.end()) {
      return false;
    }
  }

  return true;
}

}  // namespace rmad
