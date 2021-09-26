// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/setup_calibration_state_handler.h"

#include <base/logging.h>

#include "rmad/utils/calibration_utils.h"

namespace rmad {

SetupCalibrationStateHandler::SetupCalibrationStateHandler(
    scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {}

RmadErrorCode SetupCalibrationStateHandler::InitializeState() {
  // The calibration map is initialized and written into the json store in
  // GetNextStateCase of CheckCalibration. Therefore, once we fail back here, it
  // will be rewritten again.
  if (!GetCalibrationMap(json_store_, &calibration_map_)) {
    running_setup_instruction_ = RMAD_CALIBRATION_INSTRUCTION_UNKNOWN;
    LOG(ERROR) << "Failed to read calibration variables";
    return RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED;
  } else if (!GetCurrentSetupInstruction(calibration_map_,
                                         &running_setup_instruction_)) {
    running_setup_instruction_ = RMAD_CALIBRATION_INSTRUCTION_UNKNOWN;
    LOG(ERROR) << "Failed to get setup instruction for calibration";
    return RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED;
  }

  auto setup_calibration_state = std::make_unique<SetupCalibrationState>();
  setup_calibration_state->set_instruction(running_setup_instruction_);
  state_.set_allocated_setup_calibration(setup_calibration_state.release());
  return RMAD_ERROR_OK;
}

BaseStateHandler::GetNextStateCaseReply
SetupCalibrationStateHandler::GetNextStateCase(const RmadState& state) {
  if (!state.has_setup_calibration()) {
    LOG(ERROR) << "RmadState missing |setup calibration| state.";
    return {.error = RMAD_ERROR_REQUEST_INVALID, .state_case = GetStateCase()};
  }

  if (running_setup_instruction_ != state.setup_calibration().instruction()) {
    LOG(ERROR) << "The read-only setup instruction is changed.";
    return {.error = RMAD_ERROR_REQUEST_INVALID,
            .state_case = RmadState::StateCase::kSetupCalibration};
  }

  if (running_setup_instruction_ ==
      RMAD_CALIBRATION_INSTRUCTION_NO_NEED_CALIBRATION) {
    LOG(WARNING)
        << "We don't need to calibrate but still enter the setup state.";
    return {.error = RMAD_ERROR_OK,
            .state_case = RmadState::StateCase::kProvisionDevice};
  } else if (running_setup_instruction_ ==
             RMAD_CALIBRATION_INSTRUCTION_UNKNOWN) {
    LOG(ERROR) << "We entered the setup state without a valid instruction.";
    return {.error = RMAD_ERROR_OK,
            .state_case = RmadState::StateCase::kCheckCalibration};
  }

  return {.error = RMAD_ERROR_OK,
          .state_case = RmadState::StateCase::kRunCalibration};
}

}  // namespace rmad
