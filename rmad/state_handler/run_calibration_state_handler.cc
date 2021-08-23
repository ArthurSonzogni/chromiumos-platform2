// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/run_calibration_state_handler.h"

#include <algorithm>
#include <memory>

#include <base/logging.h>

#include "rmad/utils/calibration_utils.h"

namespace rmad {

RunCalibrationStateHandler::RunCalibrationStateHandler(
    scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {}

RmadErrorCode RunCalibrationStateHandler::InitializeState() {
  if (!state_.has_run_calibration()) {
    state_.set_allocated_run_calibration(new RunCalibrationState);
  }
  running_group_ = CalibrationSetupInstruction_MAX;

  // We will run the calibration in RetrieveVarsAndCalibrate.
  RetrieveVarsAndCalibrate();

  return RMAD_ERROR_OK;
}

void RunCalibrationStateHandler::CleanUpState() {
  for (auto& component_timer : timer_map_) {
    if (!component_timer.second) {
      continue;
    }
    if (component_timer.second->IsRunning()) {
      component_timer.second->Stop();
    }
    component_timer.second.reset();
  }
  timer_map_.clear();
}

BaseStateHandler::GetNextStateCaseReply
RunCalibrationStateHandler::GetNextStateCase(const RmadState& state) {
  if (!state.has_run_calibration()) {
    LOG(ERROR) << "RmadState missing |run calibration| state.";
    return {.error = RMAD_ERROR_REQUEST_INVALID, .state_case = GetStateCase()};
  }

  // Since the actual calibration has already started in InitializeState,
  // Chrome should wait for the signal to trigger GetNextStateCaseReply. Under
  // normal circumstances, we expect that the calibration has been completed
  // here, but it may fail or is still in progress (hanging?). This is why we
  // should check here.
  RmadErrorCode error_code;
  if (ShouldRecalibrate(&error_code)) {
    if (error_code != RMAD_ERROR_OK) {
      LOG(ERROR) << "Rmad: The sensor calibration is failed.";
    } else {
      LOG(INFO) << "Rmad: The sensor calibration needs another round.";
    }

    return {.error = error_code,
            .state_case = RmadState::StateCase::kCheckCalibration};
  }

  // There's nothing in |RunCalibrations|.
  state_ = state;

  // TODO(genechang): We should check whether we should perform the next round
  // of calibration (different setup) here.

  return {.error = RMAD_ERROR_OK,
          .state_case = RmadState::StateCase::kProvisionDevice};
}

void RunCalibrationStateHandler::RetrieveVarsAndCalibrate() {
  if (!GetCalibrationMap(json_store_, &calibration_map_)) {
    calibration_overall_signal_sender_->Run(
        RMAD_CALIBRATION_OVERALL_INITIALIZATION_FAILED);
    LOG(ERROR) << "Failed to read calibration variables";
    return;
  }

  if (!GetCurrentSetupInstruction(calibration_map_, &running_group_)) {
    calibration_overall_signal_sender_->Run(
        RMAD_CALIBRATION_OVERALL_INITIALIZATION_FAILED);
    LOG(ERROR) << "Failed to get components to be calibrated.";
    return;
  }

  if (running_group_ == RMAD_CALIBRATION_INSTRUCTION_NO_NEED_CALIBRATION) {
    calibration_overall_signal_sender_->Run(RMAD_CALIBRATION_OVERALL_COMPLETE);
    return;
  }

  for (auto component_status : calibration_map_[running_group_]) {
    if (ShouldCalibrate(component_status.second)) {
      // TODO(genechang): Should execute calibration here.
      PollUntilCalibrationDone(component_status.first);
    }
  }
}

bool RunCalibrationStateHandler::ShouldRecalibrate(RmadErrorCode* error_code) {
  *error_code = RMAD_ERROR_OK;
  for (auto instruction_components : calibration_map_) {
    CalibrationSetupInstruction setup_instruction =
        instruction_components.first;
    for (auto component_status : instruction_components.second) {
      if (component_status.first == RmadComponent::RMAD_COMPONENT_UNKNOWN) {
        *error_code = RMAD_ERROR_CALIBRATION_COMPONENT_MISSING;
        return true;
      }
      if (component_status.second ==
          CalibrationComponentStatus::RMAD_CALIBRATION_UNKNOWN) {
        *error_code = RMAD_ERROR_CALIBRATION_STATUS_MISSING;
        return true;
      }
      if (!IsValidCalibrationComponent(component_status.first)) {
        *error_code = RMAD_ERROR_CALIBRATION_COMPONENT_INVALID;
        return true;
      }

      if (ShouldCalibrate(component_status.second)) {
        // Under normal situations, we expect that the calibration has been
        // completed here, but it may fail, stuck or signal loss. Therefore, we
        // expect Chrome to still send the request after the timeout. This is
        // why we allow different statuses here.
        if (setup_instruction == running_group_) {
          *error_code = RMAD_ERROR_CALIBRATION_FAILED;
        }
        return true;
      }
    }
  }

  return false;
}

void RunCalibrationStateHandler::PollUntilCalibrationDone(
    RmadComponent component) {
  if (!timer_map_[component]) {
    timer_map_[component] = std::make_unique<base::RepeatingTimer>();
  }

  if (timer_map_[component]->IsRunning()) {
    timer_map_[component]->Stop();
  }

  if (component == RmadComponent::RMAD_COMPONENT_GYROSCOPE) {
    timer_map_[component]->Start(
        FROM_HERE, kPollInterval, this,
        &RunCalibrationStateHandler::CheckGyroCalibrationTask);
  } else if (component == RmadComponent::RMAD_COMPONENT_BASE_ACCELEROMETER) {
    timer_map_[component]->Start(
        FROM_HERE, kPollInterval, this,
        &RunCalibrationStateHandler::CheckBaseAccCalibrationTask);
  } else if (component == RmadComponent::RMAD_COMPONENT_LID_ACCELEROMETER) {
    timer_map_[component]->Start(
        FROM_HERE, kPollInterval, this,
        &RunCalibrationStateHandler::CheckLidAccCalibrationTask);
  } else {
    LOG(ERROR) << RmadComponent_Name(component) << " cannot be calibrated";
    return;
  }

  LOG(INFO) << "Start polling calibration progress for "
            << RmadComponent_Name(component);
}

void RunCalibrationStateHandler::CheckGyroCalibrationTask() {
  double progress = 0;

  if (!GetGyroCalibrationProgress(&progress)) {
    LOG(WARNING) << "Failed to get gyroscpoe calibration progress";
    return;
  }

  SaveAndSend(RmadComponent::RMAD_COMPONENT_GYROSCOPE, progress);
}

void RunCalibrationStateHandler::CheckBaseAccCalibrationTask() {
  double progress = 0;

  if (!GetBaseAccCalibrationProgress(&progress)) {
    LOG(WARNING) << "Failed to get base accelerometer calibration progress";
    return;
  }

  SaveAndSend(RmadComponent::RMAD_COMPONENT_BASE_ACCELEROMETER, progress);
}

void RunCalibrationStateHandler::CheckLidAccCalibrationTask() {
  double progress = 0;

  if (!GetLidAccCalibrationProgress(&progress)) {
    LOG(WARNING) << "Failed to get lid accelerometer calibration progress";
    return;
  }

  SaveAndSend(RmadComponent::RMAD_COMPONENT_LID_ACCELEROMETER, progress);
}

void RunCalibrationStateHandler::SaveAndSend(RmadComponent component,
                                             double progress) {
  CalibrationComponentStatus::CalibrationStatus status =
      CalibrationComponentStatus::RMAD_CALIBRATION_IN_PROGRESS;

  if (progress == 1.0) {
    status = CalibrationComponentStatus::RMAD_CALIBRATION_COMPLETE;
    if (timer_map_[component]) {
      timer_map_[component]->Stop();
    }
  } else if (progress < 0) {
    status = CalibrationComponentStatus::RMAD_CALIBRATION_FAILED;
    if (timer_map_[component]) {
      timer_map_[component]->Stop();
    }
  }

  if (calibration_map_[running_group_][component] != status) {
    base::AutoLock lock_scope(calibration_mutex_);
    calibration_map_[running_group_][component] = status;
    SetCalibrationMap(json_store_, calibration_map_);
  }

  CalibrationComponentStatus component_status;
  component_status.set_component(component);
  component_status.set_status(status);
  component_status.set_progress(progress);
  calibration_component_signal_sender_->Run(std::move(component_status));
}

// TODO(genechang): This is currently fake. Should check gyroscope calibration
// progress.
bool RunCalibrationStateHandler::GetGyroCalibrationProgress(double* progress) {
  static double gyro_progress = 0;
  *progress = gyro_progress;
  gyro_progress += 0.1;
  if (gyro_progress > 1.0) {
    gyro_progress = 0;
  }
  return true;
}

// TODO(genechang): This is currently fake. Should check base acceleromoter
// calibration progress.
bool RunCalibrationStateHandler::GetBaseAccCalibrationProgress(
    double* progress) {
  static double base_acc_progress = 0;
  *progress = base_acc_progress;
  base_acc_progress += 0.1;
  if (base_acc_progress > 1.0) {
    base_acc_progress = 0;
  }
  return true;
}

// TODO(genechang): This is currently fake. Should check lid acceleromoter
// calibration progress.
bool RunCalibrationStateHandler::GetLidAccCalibrationProgress(
    double* progress) {
  static double lid_acc_progress = 0;
  *progress = lid_acc_progress;
  lid_acc_progress += 0.1;
  if (lid_acc_progress > 1.0) {
    lid_acc_progress = 0;
  }
  return true;
}

}  // namespace rmad
