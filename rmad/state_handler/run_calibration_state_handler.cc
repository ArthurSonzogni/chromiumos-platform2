// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/run_calibration_state_handler.h"

#include <algorithm>
#include <limits>
#include <memory>

#include <base/logging.h>
#include <base/strings/string_number_conversions.h>

#include "rmad/constants.h"

namespace rmad {

using CalibrationStatus = CheckCalibrationState::CalibrationStatus;

RunCalibrationStateHandler::RunCalibrationStateHandler(
    scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {}

RmadErrorCode RunCalibrationStateHandler::InitializeState() {
  if (!state_.has_run_calibration() && !RetrieveState()) {
    state_.set_allocated_run_calibration(new RunCalibrationState);
  }
  running_priority_ = std::numeric_limits<int>::max();

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
    LOG(ERROR) << "RmadState missing |calibrate components| state.";
    return {.error = RMAD_ERROR_REQUEST_INVALID, .state_case = GetStateCase()};
  }

  // Since the actual calibration has already started in InitializeState,
  // Chrome should wait for the signal to trigger GetNextStateCaseReply. Under
  // normal circumstances, we expect that the calibration has been completed
  // here, but it may fail or is still in progress (hanging?). This is why we
  // should check here.
  if (ShouldRecalibrate()) {
    LOG(ERROR) << "Rmad: The sensor calibration is not complete.";
    return {.error = RMAD_ERROR_CALIBRATION_FAILED,
            .state_case = RmadState::StateCase::kCheckCalibration};
  }

  // There's nothing in |RunCalibrations|.
  state_ = state;
  StoreState();

  // TODO(genechang): We should check whether we should perform the next round
  // of calibration (different setup) here.

  return {.error = RMAD_ERROR_OK,
          .state_case = RmadState::StateCase::kProvisionDevice};
}

void RunCalibrationStateHandler::RetrieveVarsAndCalibrate() {
  if (!GetPriorityCalibrationMap()) {
    SaveAndSend(CalibrationStatus::RMAD_CALIBRATION_COMPONENT_UNKNOWN, -1);
    LOG(ERROR) << "Failed to read calibration variables";
    return;
  }

  for (auto priority_components : priority_components_calibration_map_) {
    int priority = priority_components.first;
    for (auto component_status : priority_components.second) {
      switch (component_status.second) {
        // Under normal circumstances, we expect that the first calibration has
        // been completed, and the status here is waiting. However, it may fail
        // or get stuck (still in progress) and we should retry the calibration
        // here.
        case CalibrationStatus::RMAD_CALIBRATE_WAITING:
        case CalibrationStatus::RMAD_CALIBRATE_IN_PROGRESS:
        case CalibrationStatus::RMAD_CALIBRATE_FAILED:
          // We start parsing from first priority by ordered map structure.
          if (running_priority_ >= priority) {
            // TODO(genechang): Should execute calibration here.
            PollUntilCalibrationDone(component_status.first);
            running_priority_ = priority;
          }
          break;
        // For those sensors that are calibrated or skipped, we do not need to
        // re-calibrate them.
        case CalibrationStatus::RMAD_CALIBRATE_COMPLETE:
        case CalibrationStatus::RMAD_CALIBRATE_SKIP:
          break;
        case CalibrationStatus::RMAD_CALIBRATE_UNKNOWN:
        default:
          SaveAndSend(CalibrationStatus::RMAD_CALIBRATION_COMPONENT_UNKNOWN,
                      -1);
          LOG(ERROR)
              << "Rmad calibration component calibrate_state is missing.";
          return;
      }
    }
  }
}

bool RunCalibrationStateHandler::ShouldRecalibrate() {
  for (auto priority_components : priority_components_calibration_map_) {
    for (auto component_status : priority_components.second) {
      // Under normal situations, we expect that the calibration has been
      // completed here, but it may fail, stuck or signal loss. Therefore, we
      // expect Chrome to still send the request after the timeout. This is why
      // we allow different statuses here.
      switch (component_status.second) {
        // For those sensors that are calibrated or skipped, we do not need to
        // re-calibrate them.
        case CalibrationStatus::RMAD_CALIBRATE_COMPLETE:
        case CalibrationStatus::RMAD_CALIBRATE_SKIP:
          break;
        // For all incomplete, unskipped, or unknown statuses, we should
        // re-calibrate them.
        case CalibrationStatus::RMAD_CALIBRATE_IN_PROGRESS:
        case CalibrationStatus::RMAD_CALIBRATE_WAITING:
        case CalibrationStatus::RMAD_CALIBRATE_FAILED:
          return true;
        case CalibrationStatus::RMAD_CALIBRATE_UNKNOWN:
        default:
          LOG(ERROR) << "Rmad calibration status is missing.";
          return true;
      }
    }
  }

  return false;
}

void RunCalibrationStateHandler::PollUntilCalibrationDone(
    CalibrationStatus::Component component) {
  if (!timer_map_[component]) {
    timer_map_[component] = std::make_unique<base::RepeatingTimer>();
  }

  if (timer_map_[component]->IsRunning()) {
    timer_map_[component]->Stop();
  }

  if (component == CalibrationStatus::RMAD_CALIBRATION_COMPONENT_GYROSCOPE) {
    timer_map_[component]->Start(
        FROM_HERE, kPollInterval, this,
        &RunCalibrationStateHandler::CheckGyroCalibrationTask);
  } else if (component ==
             CalibrationStatus::RMAD_CALIBRATION_COMPONENT_BASE_ACCELEROMETER) {
    timer_map_[component]->Start(
        FROM_HERE, kPollInterval, this,
        &RunCalibrationStateHandler::CheckBaseAccCalibrationTask);
  } else if (component ==
             CalibrationStatus::RMAD_CALIBRATION_COMPONENT_LID_ACCELEROMETER) {
    timer_map_[component]->Start(
        FROM_HERE, kPollInterval, this,
        &RunCalibrationStateHandler::CheckLidAccCalibrationTask);
  } else {
    LOG(ERROR) << "Rmad calibration component name is invalid.";
    return;
  }

  LOG_STREAM(INFO) << "Start polling calibration progress for "
                   << CalibrationStatus::Component_Name(component);
}

void RunCalibrationStateHandler::CheckGyroCalibrationTask() {
  int progress = 0;

  if (!GetGyroCalibrationProgress(&progress)) {
    LOG(WARNING) << "Failed to get gyroscpoe calibration progress";
    return;
  }

  SaveAndSend(CalibrationStatus::RMAD_CALIBRATION_COMPONENT_GYROSCOPE,
              progress);
}

void RunCalibrationStateHandler::CheckBaseAccCalibrationTask() {
  int progress = 0;

  if (!GetBaseAccCalibrationProgress(&progress)) {
    LOG(WARNING) << "Failed to get base accelerometer calibration progress";
    return;
  }

  SaveAndSend(CalibrationStatus::RMAD_CALIBRATION_COMPONENT_BASE_ACCELEROMETER,
              progress);
}

void RunCalibrationStateHandler::CheckLidAccCalibrationTask() {
  int progress = 0;

  if (!GetLidAccCalibrationProgress(&progress)) {
    LOG(WARNING) << "Failed to get lid accelerometer calibration progress";
    return;
  }

  SaveAndSend(CalibrationStatus::RMAD_CALIBRATION_COMPONENT_ACCELEROMETER,
              progress);
}

void RunCalibrationStateHandler::SaveAndSend(
    CalibrationStatus::Component component, int progress_percentage) {
  CalibrationStatus::Status status =
      CalibrationStatus::RMAD_CALIBRATE_IN_PROGRESS;

  if (progress_percentage == 100) {
    status = CalibrationStatus::RMAD_CALIBRATE_COMPLETE;
    if (timer_map_[component]) {
      timer_map_[component]->Stop();
    }
  } else if (progress_percentage < 0) {
    status = CalibrationStatus::RMAD_CALIBRATE_FAILED;
    if (timer_map_[component]) {
      timer_map_[component]->Stop();
    }
  }

  if (priority_components_calibration_map_[running_priority_][component] !=
      status) {
    base::AutoLock lock_scope(calibration_mutex_);
    priority_components_calibration_map_[running_priority_][component] = status;
    SetPriorityCalibrationMap();
  }

  CalibrationStatus component_status;
  component_status.set_name(component);
  component_status.set_status(status);
  double progress = static_cast<double>(progress_percentage) / 100.0;
  calibration_signal_sender_->Run(std::move(component_status), progress);
}

bool RunCalibrationStateHandler::GetPriorityCalibrationMap() {
  base::AutoLock lock_scope(calibration_mutex_);
  priority_components_calibration_map_.clear();

  std::map<std::string, std::map<std::string, std::string>> json_value_map;
  if (!json_store_->GetValue(kCalibrationMap, &json_value_map)) {
    return false;
  }

  for (auto priority_components : json_value_map) {
    int priority;
    if (!base::StringToInt(priority_components.first, &priority)) {
      return false;
    }
    for (auto component_status : priority_components.second) {
      CalibrationStatus::Component component;
      if (!CalibrationStatus::Component_Parse(component_status.first,
                                              &component) ||
          component == CalibrationStatus::RMAD_CALIBRATION_COMPONENT_UNKNOWN) {
        LOG(ERROR) << "Failed to parse component name from variables";
        return false;
      }
      CalibrationStatus::Status status;
      if (!CalibrationStatus::Status_Parse(component_status.second, &status) ||
          status == CalibrationStatus::RMAD_CALIBRATE_UNKNOWN) {
        LOG(ERROR) << "Failed to parse status name from variables";
        return false;
      }
      priority_components_calibration_map_[priority][component] = status;
    }
  }

  return true;
}

bool RunCalibrationStateHandler::SetPriorityCalibrationMap() {
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

// TODO(genechang): This is currently fake. Should check gyroscope calibration
// progress.
bool RunCalibrationStateHandler::GetGyroCalibrationProgress(
    int* progress_percentage) {
  static int gyro_progress = 0;
  *progress_percentage = gyro_progress;
  gyro_progress += 10;
  if (gyro_progress > 100) {
    gyro_progress = 0;
  }
  return true;
}

// TODO(genechang): This is currently fake. Should check base acceleromoter
// calibration progress.
bool RunCalibrationStateHandler::GetBaseAccCalibrationProgress(
    int* progress_percentage) {
  static int base_acc_progress = 0;
  *progress_percentage = base_acc_progress;
  base_acc_progress += 10;
  if (base_acc_progress > 100) {
    base_acc_progress = 0;
  }
  return true;
}

// TODO(genechang): This is currently fake. Should check lid acceleromoter
// calibration progress.
bool RunCalibrationStateHandler::GetLidAccCalibrationProgress(
    int* progress_percentage) {
  static int lid_acc_progress = 0;
  *progress_percentage = lid_acc_progress;
  lid_acc_progress += 10;
  if (lid_acc_progress > 100) {
    lid_acc_progress = 0;
  }
  return true;
}

}  // namespace rmad
