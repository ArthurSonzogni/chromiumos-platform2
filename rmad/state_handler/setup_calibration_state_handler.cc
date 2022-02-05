// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/setup_calibration_state_handler.h"

#include <set>

#include <base/logging.h>

#include "rmad/utils/calibration_utils.h"
#include "rmad/utils/fake_iio_sensor_probe_utils.h"
#include "rmad/utils/iio_sensor_probe_utils_impl.h"

namespace rmad {

namespace fake {

FakeSetupCalibrationStateHandler::FakeSetupCalibrationStateHandler(
    scoped_refptr<JsonStore> json_store)
    : SetupCalibrationStateHandler(
          json_store, std::make_unique<FakeIioSensorProbeUtils>()) {}

}  // namespace fake

SetupCalibrationStateHandler::SetupCalibrationStateHandler(
    scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store),
      running_setup_instruction_(RMAD_CALIBRATION_INSTRUCTION_UNKNOWN) {
  iio_sensor_probe_utils_ = std::make_unique<IioSensorProbeUtilsImpl>();
}

SetupCalibrationStateHandler::SetupCalibrationStateHandler(
    scoped_refptr<JsonStore> json_store,
    std::unique_ptr<IioSensorProbeUtils> iio_sensor_probe_utils)
    : BaseStateHandler(json_store),
      iio_sensor_probe_utils_(std::move(iio_sensor_probe_utils)),
      running_setup_instruction_(RMAD_CALIBRATION_INSTRUCTION_UNKNOWN) {}

RmadErrorCode SetupCalibrationStateHandler::InitializeState() {
  // Always probe again and use the probe results to update |state_|.
  std::set<RmadComponent> probed_components = iio_sensor_probe_utils_->Probe();
  // Update probeable components using runtime_probe results.
  for (RmadComponent component : probed_components) {
    // Ignore the components that cannot be calibrated.
    if (std::find(kComponentsNeedManualCalibration.begin(),
                  kComponentsNeedManualCalibration.end(),
                  component) == kComponentsNeedManualCalibration.end()) {
      continue;
    }
    calibration_map_[GetCalibrationSetupInstruction(component)][component] =
        CalibrationComponentStatus::RMAD_CALIBRATION_WAITING;
  }

  // Ignore the return value, since we can initialize state handler from an
  // empty or fulfilled dictionary.
  InstructionCalibrationStatusMap original_calibration_map;
  GetCalibrationMap(json_store_, &original_calibration_map);

  // We mark all components with an unexpected status as failed because it may
  // have some errors.
  for (auto [instruction, components] : original_calibration_map) {
    for (auto [component, status] : components) {
      if (calibration_map_.count(instruction) &&
          calibration_map_[instruction].count(component)) {
        if (IsInProgressStatus(status) || IsUnknownStatus(status)) {
          status = CalibrationComponentStatus::RMAD_CALIBRATION_FAILED;
        }
        calibration_map_[instruction][component] = status;
      }
    }
  }

  if (!SetCalibrationMap(json_store_, calibration_map_)) {
    LOG(ERROR) << "Failed to set calibration status.";
    return RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED;
  }

  running_setup_instruction_ = GetCurrentSetupInstruction(calibration_map_);

  auto setup_calibration_state = std::make_unique<SetupCalibrationState>();
  setup_calibration_state->set_instruction(running_setup_instruction_);
  state_.set_allocated_setup_calibration(setup_calibration_state.release());
  return RMAD_ERROR_OK;
}

BaseStateHandler::GetNextStateCaseReply
SetupCalibrationStateHandler::GetNextStateCase(const RmadState& state) {
  if (!state.has_setup_calibration()) {
    LOG(ERROR) << "RmadState missing |setup calibration| state.";
    return NextStateCaseWrapper(RMAD_ERROR_REQUEST_INVALID);
  }

  if (running_setup_instruction_ != state.setup_calibration().instruction()) {
    LOG(ERROR) << "The read-only setup instruction is changed.";
    return NextStateCaseWrapper(RMAD_ERROR_REQUEST_INVALID);
  }

  if (running_setup_instruction_ == RMAD_CALIBRATION_INSTRUCTION_UNKNOWN) {
    LOG(ERROR) << "The setup instruction is missing.";
    return NextStateCaseWrapper(RmadState::StateCase::kCheckCalibration);
  }

  // kWipeDevice should be set by previous states.
  bool wipe_device;
  if (!json_store_->GetValue(kWipeDevice, &wipe_device)) {
    LOG(ERROR) << "Variable " << kWipeDevice << " not found";
    return NextStateCaseWrapper(RMAD_ERROR_TRANSITION_FAILED);
  }

  if (running_setup_instruction_ ==
      RMAD_CALIBRATION_INSTRUCTION_NO_NEED_CALIBRATION) {
    if (wipe_device) {
      return NextStateCaseWrapper(RmadState::StateCase::kFinalize);
    } else {
      return NextStateCaseWrapper(RmadState::StateCase::kWpEnablePhysical);
    }
  }

  if (running_setup_instruction_ ==
      RMAD_CALIBRATION_INSTRUCTION_NEED_TO_CHECK) {
    return NextStateCaseWrapper(RmadState::StateCase::kCheckCalibration);
  }

  return NextStateCaseWrapper(RmadState::StateCase::kRunCalibration);
}

BaseStateHandler::GetNextStateCaseReply
SetupCalibrationStateHandler::TryGetNextStateCaseAtBoot() {
  if (running_setup_instruction_ ==
      RMAD_CALIBRATION_INSTRUCTION_NEED_TO_CHECK) {
    return NextStateCaseWrapper(RmadState::StateCase::kCheckCalibration);
  } else {
    return NextStateCaseWrapper(RMAD_ERROR_TRANSITION_FAILED);
  }
}

}  // namespace rmad
