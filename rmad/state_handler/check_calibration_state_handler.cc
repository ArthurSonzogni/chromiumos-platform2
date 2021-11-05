// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/check_calibration_state_handler.h"

#include <memory>
#include <set>
#include <string>
#include <utility>

#include <base/logging.h>

#include "rmad/constants.h"
#include "rmad/utils/calibration_utils.h"
#include "rmad/utils/fake_iio_sensor_probe_utils.h"
#include "rmad/utils/iio_sensor_probe_utils_impl.h"

namespace rmad {

namespace {

// Convert a dictionary of {CalibrationSetupInstruction:
// {RmadComponent: CalibrationComponentStatus}} to a |RmadState|.
RmadState ConvertDictionaryToState(
    const InstructionCalibrationStatusMap& calibration_map) {
  auto check_calibration = std::make_unique<CheckCalibrationState>();
  for (const auto& [unused_instruction, components] : calibration_map) {
    for (const auto& [component, status] : components) {
      if (component == RMAD_COMPONENT_UNKNOWN) {
        LOG(WARNING) << "Dictionary contains UNKNOWN component";
        continue;
      }
      CalibrationComponentStatus* calibration_component_status =
          check_calibration->add_components();
      calibration_component_status->set_component(component);
      calibration_component_status->set_status(status);
      double progress = 0.0;
      if (status == CalibrationComponentStatus::RMAD_CALIBRATION_COMPLETE) {
        progress = 1.0;
      } else if (status ==
                 CalibrationComponentStatus::RMAD_CALIBRATION_FAILED) {
        progress = -1.0;
      }
      calibration_component_status->set_progress(progress);
    }
  }

  RmadState state;
  state.set_allocated_check_calibration(check_calibration.release());
  return state;
}

}  // namespace

namespace fake {

FakeCheckCalibrationStateHandler::FakeCheckCalibrationStateHandler(
    scoped_refptr<JsonStore> json_store)
    : CheckCalibrationStateHandler(
          json_store, std::make_unique<FakeIioSensorProbeUtils>()) {}

}  // namespace fake

CheckCalibrationStateHandler::CheckCalibrationStateHandler(
    scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {
  iio_sensor_probe_utils_ = std::make_unique<IioSensorProbeUtilsImpl>();
}

CheckCalibrationStateHandler::CheckCalibrationStateHandler(
    scoped_refptr<JsonStore> json_store,
    std::unique_ptr<IioSensorProbeUtils> iio_sensor_probe_utils)
    : BaseStateHandler(json_store),
      iio_sensor_probe_utils_(std::move(iio_sensor_probe_utils)) {}

RmadErrorCode CheckCalibrationStateHandler::InitializeState() {
  // It may return false if calibration map is uninitialized, which can be
  // ignored. We can initialize state handler from an empty or fulfilled
  // dictionary.
  GetCalibrationMap(json_store_, &calibration_map_);

  // Always probe again and use the probe results to update |state_|.
  std::set<RmadComponent> probed_components = iio_sensor_probe_utils_->Probe();

  // Update probeable components using runtime_probe results.
  for (RmadComponent component : kComponentsNeedManualCalibration) {
    if (probed_components.count(component) > 0) {
      auto& components =
          calibration_map_[GetCalibrationSetupInstruction(component)];
      // If the component is not found in the dictionary, it should be a new
      // sensor and we should calibrate it.
      if (components.find(component) == components.end()) {
        components[component] =
            CalibrationComponentStatus::RMAD_CALIBRATION_WAITING;
      }
    }
  }

  state_ = ConvertDictionaryToState(calibration_map_);
  return RMAD_ERROR_OK;
}

BaseStateHandler::GetNextStateCaseReply
CheckCalibrationStateHandler::GetNextStateCase(const RmadState& state) {
  bool need_calibration;
  RmadErrorCode error_code;
  if (!CheckIsCalibrationRequired(state, &need_calibration, &error_code)) {
    return {.error = error_code, .state_case = GetStateCase()};
  }

  state_ = state;
  SetCalibrationMap(json_store_, calibration_map_);

  if (need_calibration) {
    return {.error = RMAD_ERROR_OK,
            .state_case = RmadState::StateCase::kSetupCalibration};
  }

  return {.error = RMAD_ERROR_OK,
          .state_case = RmadState::StateCase::kProvisionDevice};
}

bool CheckCalibrationStateHandler::CheckIsUserSelectionValid(
    const CheckCalibrationState& user_selection, RmadErrorCode* error_code) {
  CHECK(state_.has_check_calibration());
  // Here we make sure that the size is the same, and then we can only check
  // whether the components from user selection are all in the dictionary.
  if (user_selection.components_size() !=
      state_.check_calibration().components_size()) {
    LOG(ERROR) << "Size of components has been changed!";
    *error_code = RMAD_ERROR_REQUEST_INVALID;
    return false;
  }

  // If a calibratable component is probed, it should be in the dictionary.
  // Otherwise, the component from user selection is invalid.
  for (int i = 0; i < user_selection.components_size(); ++i) {
    const auto& component = user_selection.components(i).component();
    const auto& status = user_selection.components(i).status();
    const auto instruction = GetCalibrationSetupInstruction(component);
    if (!calibration_map_[instruction].count(component)) {
      LOG(ERROR) << RmadComponent_Name(component)
                 << " has not been probed, it should not be selected!";
      *error_code = RMAD_ERROR_REQUEST_INVALID;
      return false;
    } else if (calibration_map_[instruction][component] != status &&
               status != CalibrationComponentStatus::RMAD_CALIBRATION_SKIP) {
      LOG(ERROR) << RmadComponent_Name(component)
                 << "'s status has been changed from "
                 << CalibrationComponentStatus::CalibrationStatus_Name(
                        calibration_map_[instruction][component])
                 << " to "
                 << CalibrationComponentStatus::CalibrationStatus_Name(status)
                 << ", it should not be changed manually!";
      *error_code = RMAD_ERROR_REQUEST_INVALID;
      return false;
    }
  }

  return true;
}

bool CheckCalibrationStateHandler::CheckIsCalibrationRequired(
    const RmadState& state, bool* need_calibration, RmadErrorCode* error_code) {
  if (!state.has_check_calibration()) {
    LOG(ERROR) << "RmadState missing |check calibration| state.";
    *error_code = RMAD_ERROR_REQUEST_INVALID;
    return false;
  }

  *need_calibration = false;

  const CheckCalibrationState& user_selection = state.check_calibration();
  if (!CheckIsUserSelectionValid(user_selection, error_code)) {
    return false;
  }

  for (int i = 0; i < user_selection.components_size(); ++i) {
    const CalibrationComponentStatus& component_status =
        user_selection.components(i);
    if (component_status.component() == RMAD_COMPONENT_UNKNOWN) {
      *error_code = RMAD_ERROR_REQUEST_ARGS_MISSING;
      LOG(ERROR) << "RmadState missing |component| argument.";
      return false;
    }

    CalibrationSetupInstruction instruction =
        GetCalibrationSetupInstruction(component_status.component());
    if (instruction == RMAD_CALIBRATION_INSTRUCTION_UNKNOWN) {
      *error_code = RMAD_ERROR_CALIBRATION_COMPONENT_INVALID;
      LOG_STREAM(ERROR) << RmadComponent_Name(component_status.component())
                        << " cannot be calibrated.";
      return false;
    }

    // Since the entire calibration process is check -> setup -> calibrate ->
    // complete or return to check, the status may be waiting, in progress
    // (timeout), failed, complete or skip here.
    switch (component_status.status()) {
      // For in progress and failed cases, we also need to calibrate it.
      case CalibrationComponentStatus::RMAD_CALIBRATION_WAITING:
      case CalibrationComponentStatus::RMAD_CALIBRATION_IN_PROGRESS:
      case CalibrationComponentStatus::RMAD_CALIBRATION_FAILED:
        *need_calibration = true;
        break;
      // For those already calibrated and skipped component, we don't need to
      // calibrate it.
      case CalibrationComponentStatus::RMAD_CALIBRATION_COMPLETE:
      case CalibrationComponentStatus::RMAD_CALIBRATION_SKIP:
        break;
      case CalibrationComponentStatus::RMAD_CALIBRATION_UNKNOWN:
      default:
        *error_code = RMAD_ERROR_REQUEST_ARGS_MISSING;
        LOG(ERROR)
            << "RmadState component missing |calibration_status| argument.";
        return false;
    }

    calibration_map_[instruction][component_status.component()] =
        component_status.status();
  }

  *error_code = RMAD_ERROR_OK;
  return true;
}

}  // namespace rmad
