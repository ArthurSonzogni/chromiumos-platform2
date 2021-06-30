// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/run_calibration_state_handler.h"

#include <algorithm>
#include <memory>

#include <base/logging.h>
#include <base/strings/string_number_conversions.h>

#include "rmad/constants.h"

using CalibrationStatus = rmad::CheckCalibrationState::CalibrationStatus;

namespace rmad {

RunCalibrationStateHandler::RunCalibrationStateHandler(
    scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {}

RmadErrorCode RunCalibrationStateHandler::InitializeState() {
  if (!state_.has_run_calibration() && !RetrieveState()) {
    state_.set_allocated_run_calibration(new RunCalibrationState);
    base::AutoLock lock_scope(calibration_mutex_);
    components_calibration_map_[base::NumberToString(
        CalibrationStatus::RMAD_CALIBRATION_COMPONENT_ACCELEROMETER)] =
        base::NumberToString(CalibrationStatus::RMAD_CALIBRATE_UNKNOWN);
    components_calibration_map_[base::NumberToString(
        CalibrationStatus::RMAD_CALIBRATION_COMPONENT_GYROSCOPE)] =
        base::NumberToString(CalibrationStatus::RMAD_CALIBRATE_UNKNOWN);
  }

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

  // Since the actual calibration has already started in InitializeState, chrome
  // should wait for the signal to trigger GetNextStateCaseReply. Under normal
  // circumstances, we expect that the calibration has been completed here, but
  // it may fail or is still in progress (hanging?). This is why we should check
  // here.
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
  if (!json_store_->GetValue(kCalibrationMap, &components_calibration_map_)) {
    LOG(ERROR) << "Failed to read calibration variables";
    return;
  }

  for (auto component : components_calibration_map_) {
    base::AutoLock lock_scope(calibration_mutex_);
    CalibrationStatus component_status;
    if (!ConvertToComponenetStatus(component, &component_status)) {
      return;
    }

    switch (component_status.status()) {
      // Under normal circumstances, we expect that the first calibration has
      // been completed, and the status here is waiting. However, it may fail or
      // get stuck (still in progress) and we should retry the calibration here.
      case CalibrationStatus::RMAD_CALIBRATE_WAITING:
      case CalibrationStatus::RMAD_CALIBRATE_IN_PROGRESS:
      case CalibrationStatus::RMAD_CALIBRATE_FAILED:
        // TODO(genechang): Should execute calibration here.
        PollUntilCalibrationDone(component_status.name());
        break;
      // For those sensors that are calibrated or skipped, we do not need to
      // re-calibrate them.
      case CalibrationStatus::RMAD_CALIBRATE_COMPLETE:
      case CalibrationStatus::RMAD_CALIBRATE_SKIP:
        break;
      case CalibrationStatus::RMAD_CALIBRATE_UNKNOWN:
      default:
        LOG(ERROR) << "Rmad calibration component calibrate_state is missing.";
        return;
    }
  }
}

bool RunCalibrationStateHandler::ShouldRecalibrate() {
  for (auto component : components_calibration_map_) {
    CalibrationStatus component_status;
    if (!ConvertToComponenetStatus(component, &component_status)) {
      return true;
    }

    // Under normal situations, we expect that the calibration has been
    // completed here, but it may fail, stuck or signal loss. Therefore, we
    // expect Chrome to still send the request after the timeout. This is why we
    // allow different statuses here.
    switch (component_status.status()) {
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

  std::string component_name;
  if (component ==
      CalibrationStatus::RMAD_CALIBRATION_COMPONENT_ACCELEROMETER) {
    component_name = "accelerometer";
    timer_map_[component]->Start(
        FROM_HERE, kPollInterval, this,
        &RunCalibrationStateHandler::CheckAccCalibrationTask);
  } else if (component ==
             CalibrationStatus::RMAD_CALIBRATION_COMPONENT_GYROSCOPE) {
    component_name = "gyroscope";
    timer_map_[component]->Start(
        FROM_HERE, kPollInterval, this,
        &RunCalibrationStateHandler::CheckGyroCalibrationTask);
  } else {
    LOG(ERROR) << "Rmad calibration component name is invalid.";
    return;
  }

  LOG_STREAM(INFO) << "Start polling calibration progress for "
                   << component_name;
}

void RunCalibrationStateHandler::CheckAccCalibrationTask() {
  int progress = 0;

  if (!GetAccCalibrationProgress(&progress)) {
    LOG(WARNING) << "Failed to get accelerometer calibration progress";
    return;
  }

  SaveAndSend(CalibrationStatus::RMAD_CALIBRATION_COMPONENT_ACCELEROMETER,
              progress);
}

void RunCalibrationStateHandler::CheckGyroCalibrationTask() {
  int progress = 0;

  if (!GetAccCalibrationProgress(&progress)) {
    LOG(WARNING) << "Failed to get accelerometer calibration progress";
    return;
  }

  SaveAndSend(CalibrationStatus::RMAD_CALIBRATION_COMPONENT_GYROSCOPE,
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

  if (std::string component_str = base::NumberToString(component),
      state_str = base::NumberToString(status);
      components_calibration_map_[component_str] != state_str) {
    base::AutoLock lock_scope(calibration_mutex_);
    components_calibration_map_[component_str] = state_str;
    json_store_->SetValue(kCalibrationMap, components_calibration_map_);
  }

  CalibrationStatus component_status;
  component_status.set_name(component);
  component_status.set_status(status);
  double progress = static_cast<double>(progress_percentage) / 100.0;
  calibration_signal_sender_->Run(std::move(component_status), progress);
}

bool RunCalibrationStateHandler::ConvertToComponenetStatus(
    const std::pair<std::string, std::string>& id_status_str_pair,
    CalibrationStatus* component_status) {
  int component_id;
  if (!base::StringToInt(id_status_str_pair.first, &component_id)) {
    LOG(ERROR) << "Failed to parse component name from variables";
    return false;
  }
  CalibrationStatus::Component component_name =
      static_cast<CalibrationStatus::Component>(component_id);

  if (component_name == CalibrationStatus::RMAD_CALIBRATION_COMPONENT_UNKNOWN) {
    LOG(ERROR) << "Rmad calibration component name is missing.";
    return false;
  }

  int status = 0;
  if (!base::StringToInt(id_status_str_pair.second, &status)) {
    LOG(ERROR) << "Rmad calibration component calibrate_state is missing.";
    return false;
  }

  component_status->set_name(component_name);
  component_status->set_status(static_cast<CalibrationStatus::Status>(status));
  return true;
}

// TODO(genechang): This is currently fake. Should check acceleromoter
// calibration progress.
bool RunCalibrationStateHandler::GetAccCalibrationProgress(
    int* progress_percentage) {
  static int acc_progress = 0;
  *progress_percentage = acc_progress;
  acc_progress += 10;
  if (acc_progress > 100) {
    acc_progress = 0;
  }
  return true;
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

}  // namespace rmad
