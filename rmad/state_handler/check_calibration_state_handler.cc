// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/check_calibration_state_handler.h"

#include <limits>
#include <memory>

#include <base/logging.h>
#include <base/strings/string_number_conversions.h>

#include "rmad/constants.h"

namespace rmad {

using CalibrationStatus = CheckCalibrationState::CalibrationStatus;

int GetComponentCalibrationPriority(
    CheckCalibrationState_CalibrationStatus::Component component) {
  int priority = std::numeric_limits<int>::max();
  for (auto calibration_priority : kComponentsCalibrationPriority) {
    if (calibration_priority[0] == component) {
      priority = calibration_priority[1];
      break;
    }
  }

  if (priority == std::numeric_limits<int>::max()) {
    LOG(WARNING) << "Unknown priority device "
                 << CheckCalibrationState::CalibrationStatus::Component_Name(
                        component);
  }

  return priority;
}

CheckCalibrationStateHandler::CheckCalibrationStateHandler(
    scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {}

RmadErrorCode CheckCalibrationStateHandler::InitializeState() {
  if (!state_.has_check_calibration() && !RetrieveState()) {
    state_.set_allocated_check_calibration(new CheckCalibrationState);
  }
  return RMAD_ERROR_OK;
}

BaseStateHandler::GetNextStateCaseReply
CheckCalibrationStateHandler::GetNextStateCase(const RmadState& state) {
  bool need_calibration;
  if (!CheckIsCalibrationRequired(state, &need_calibration)) {
    return {.error = RMAD_ERROR_REQUEST_INVALID, .state_case = GetStateCase()};
  }

  state_ = state;
  StoreState();
  StoreVars();

  // TODO(genechang): We should check whether we should perform the multiple
  // rounds of calibration (different setup) here.

  if (need_calibration) {
    return {.error = RMAD_ERROR_OK,
            .state_case = RmadState::StateCase::kSetupCalibration};
  }

  return {.error = RMAD_ERROR_OK,
          .state_case = RmadState::StateCase::kProvisionDevice};
}

bool CheckCalibrationStateHandler::CheckIsCalibrationRequired(
    const RmadState& state, bool* need_calibration) {
  if (!state.has_check_calibration()) {
    LOG(ERROR) << "RmadState missing |components calibrate| state.";
    return false;
  }

  *need_calibration = false;

  const CheckCalibrationState& components = state.check_calibration();
  for (int i = 0; i < components.components_size(); ++i) {
    const CalibrationStatus& component_status = components.components(i);
    if (component_status.name() ==
        CalibrationStatus::RMAD_CALIBRATION_COMPONENT_UNKNOWN) {
      LOG(ERROR) << "RmadState component missing |name| argument.";
      return false;
    }

    // Since the entire calibration process is check -> setup -> calibrate ->
    // complete or return to check, the status may be waiting, in progress
    // (timeout), failed, complete or skip here.
    switch (component_status.status()) {
      // For in progress and failed cases, we also need to calibrate it.
      case CalibrationStatus::RMAD_CALIBRATE_WAITING:
      case CalibrationStatus::RMAD_CALIBRATE_IN_PROGRESS:
      case CalibrationStatus::RMAD_CALIBRATE_FAILED:
        *need_calibration = true;
        break;
      // For those already calibrated and skipped component, we don't need to
      // calibrate it.
      case CalibrationStatus::RMAD_CALIBRATE_COMPLETE:
      case CalibrationStatus::RMAD_CALIBRATE_SKIP:
        break;
      case CalibrationStatus::RMAD_CALIBRATE_UNKNOWN:
      default:
        LOG(ERROR) << "RmadState component missing |calibrate_state| argument.";
        return false;
    }

    // We don't check whether the priority is unknown, because even if the
    // priority is unknown (lowest) we can still calibrate it.
    int priority = GetComponentCalibrationPriority(component_status.name());
    priority_components_calibration_map_[priority][component_status.name()] =
        component_status.status();
  }

  return true;
}

bool CheckCalibrationStateHandler::StoreVars() const {
  // In order to save dictionary style variables to json, currently only
  // variables whose keys are strings are supported. This is why we converted
  // it to a string. In addition, in order to ensure that the file is still
  // readable after the enum sequence is updated, we also convert its value
  // into a readable string to deal with possible updates.
  std::map<std::string, std::map<std::string, std::string>> json_value_map;
  for (auto priority_components : priority_components_calibration_map_) {
    std::string priority = base::NumberToString(priority_components.first);
    for (auto component_status : priority_components.second) {
      std::string component_name =
          CalibrationStatus::Component_Name(component_status.first);
      std::string status_name =
          CalibrationStatus::Status_Name(component_status.second);
      json_value_map[priority][component_name] = status_name;
    }
  }

  return json_store_->SetValue(kCalibrationMap, json_value_map);
}

}  // namespace rmad
