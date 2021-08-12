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
  if (!state_.has_setup_calibration() && !RetrieveState()) {
    state_.set_allocated_setup_calibration(new SetupCalibrationState);
  }

  RetrieveVarsAndSetup();

  return RMAD_ERROR_OK;
}

BaseStateHandler::GetNextStateCaseReply
SetupCalibrationStateHandler::GetNextStateCase(const RmadState& state) {
  if (!state.has_setup_calibration()) {
    LOG(ERROR) << "RmadState missing |setup calibration| state.";
    return {.error = RMAD_ERROR_REQUEST_INVALID, .state_case = GetStateCase()};
  }

  // There's nothing in |SetupCalibrationState|.
  state_ = state;
  StoreState();

  if (running_setup_instruction_ ==
      RMAD_CALIBRATION_INSTRUCTION_NO_NEED_CALIBRATION) {
    return {.error = RMAD_ERROR_OK,
            .state_case = RmadState::StateCase::kProvisionDevice};
  }

  return {.error = RMAD_ERROR_OK,
          .state_case = RmadState::StateCase::kRunCalibration};
}

void SetupCalibrationStateHandler::RetrieveVarsAndSetup() {
  if (!GetCalibrationMap(json_store_, &calibration_map_)) {
    running_setup_instruction_ = RMAD_CALIBRATION_INSTRUCTION_UNKNOWN;
    LOG(ERROR) << "Failed to read calibration variables";
  } else if (!GetCurrentSetupInstruction(calibration_map_,
                                         &running_setup_instruction_)) {
    running_setup_instruction_ = RMAD_CALIBRATION_INSTRUCTION_UNKNOWN;
    LOG(ERROR) << "Failed to get setup instruction for calibration";
  }

  calibration_setup_signal_sender_->Run(running_setup_instruction_);
}

}  // namespace rmad
