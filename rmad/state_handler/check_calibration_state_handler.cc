// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/check_calibration_state_handler.h"

#include <memory>

#include <base/logging.h>
#include <base/strings/string_number_conversions.h>

#include "rmad/constants.h"

namespace rmad {

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
    const CheckCalibrationState::CalibrationStatus& component =
        components.components(i);
    if (component.name() == CheckCalibrationState::CalibrationStatus::
                                RMAD_CALIBRATION_COMPONENT_UNKNOWN) {
      LOG(ERROR) << "RmadState component missing |name| argument.";
      return false;
    }

    // Since the entire calibration process is check -> setup -> calibrate ->
    // complete or return to check, the status may be waiting, in progress
    // (timeout), failed, complete or skip here.
    switch (component.status()) {
      // For in progress and failed cases, we also need to calibrate it.
      case CheckCalibrationState::CalibrationStatus::RMAD_CALIBRATE_WAITING:
      case CheckCalibrationState::CalibrationStatus::RMAD_CALIBRATE_IN_PROGRESS:
      case CheckCalibrationState::CalibrationStatus::RMAD_CALIBRATE_FAILED:
        *need_calibration = true;
        break;
      // For those already calibrated and skipped component, we don't need to
      // calibrate it.
      case CheckCalibrationState::CalibrationStatus::RMAD_CALIBRATE_COMPLETE:
      case CheckCalibrationState::CalibrationStatus::RMAD_CALIBRATE_SKIP:
        break;
      case CheckCalibrationState::CalibrationStatus::RMAD_CALIBRATE_UNKNOWN:
      default:
        LOG(ERROR) << "RmadState component missing |calibrate_state| argument.";
        return false;
    }
    components_calibration_map_[base::NumberToString(component.name())] =
        base::NumberToString(component.status());
  }

  return true;
}

bool CheckCalibrationStateHandler::StoreVars() const {
  return json_store_->SetValue(kCalibrationMap, components_calibration_map_);
}

}  // namespace rmad
