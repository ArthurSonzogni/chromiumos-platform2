// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/run_calibration_state_handler.h"

#include <algorithm>
#include <memory>

#include <base/logging.h>
#include <base/task/task_traits.h>
#include <base/task/thread_pool.h>

#include "rmad/utils/accelerometer_calibration_utils_impl.h"
#include "rmad/utils/calibration_utils.h"
#include "rmad/utils/gyroscope_calibration_utils_impl.h"

namespace rmad {

RunCalibrationStateHandler::RunCalibrationStateHandler(
    scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {
  auto vpd_utils_thread_safe = base::MakeRefCounted<VpdUtilsImplThreadSafe>();
  sensor_calibration_utils_map_
      [RmadComponent::RMAD_COMPONENT_BASE_ACCELEROMETER] =
          std::make_unique<AccelerometerCalibrationUtilsImpl>(
              vpd_utils_thread_safe, "base");
  sensor_calibration_utils_map_
      [RmadComponent::RMAD_COMPONENT_LID_ACCELEROMETER] =
          std::make_unique<AccelerometerCalibrationUtilsImpl>(
              vpd_utils_thread_safe, "lid");
  sensor_calibration_utils_map_[RmadComponent::RMAD_COMPONENT_GYROSCOPE] =
      std::make_unique<GyroscopeCalibrationUtilsImpl>(vpd_utils_thread_safe,
                                                      "base");
}

RunCalibrationStateHandler::RunCalibrationStateHandler(
    scoped_refptr<JsonStore> json_store,
    std::unique_ptr<SensorCalibrationUtils> base_acc_utils,
    std::unique_ptr<SensorCalibrationUtils> lid_acc_utils,
    std::unique_ptr<SensorCalibrationUtils> base_gyro_utils)
    : BaseStateHandler(json_store) {
  sensor_calibration_utils_map_
      [RmadComponent::RMAD_COMPONENT_BASE_ACCELEROMETER] =
          std::move(base_acc_utils);
  sensor_calibration_utils_map_
      [RmadComponent::RMAD_COMPONENT_LID_ACCELEROMETER] =
          std::move(lid_acc_utils);
  sensor_calibration_utils_map_[RmadComponent::RMAD_COMPONENT_GYROSCOPE] =
      std::move(base_gyro_utils);
}

RmadErrorCode RunCalibrationStateHandler::InitializeState() {
  if (!state_.has_run_calibration()) {
    state_.set_allocated_run_calibration(new RunCalibrationState);
  }
  running_group_ = CalibrationSetupInstruction_MAX;

  if (!task_runner_) {
    task_runner_ = base::ThreadPool::CreateTaskRunner(
        {base::TaskPriority::BEST_EFFORT, base::MayBlock()});
  }
  progress_timer_map_[RmadComponent::RMAD_COMPONENT_BASE_ACCELEROMETER] =
      std::make_unique<base::RepeatingTimer>();
  progress_timer_map_[RmadComponent::RMAD_COMPONENT_LID_ACCELEROMETER] =
      std::make_unique<base::RepeatingTimer>();
  progress_timer_map_[RmadComponent::RMAD_COMPONENT_GYROSCOPE] =
      std::make_unique<base::RepeatingTimer>();

  // We will run the calibration in RetrieveVarsAndCalibrate.
  RetrieveVarsAndCalibrate();

  return RMAD_ERROR_OK;
}

void RunCalibrationStateHandler::CleanUpState() {
  for (auto& progress_timer : progress_timer_map_) {
    if (!progress_timer.second) {
      continue;
    }
    if (progress_timer.second->IsRunning()) {
      progress_timer.second->Stop();
    }
    progress_timer.second.reset();
  }
  progress_timer_map_.clear();
  if (task_runner_) {
    task_runner_.reset();
  }
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
      return {.error = error_code,
              .state_case = RmadState::StateCase::kCheckCalibration};
    } else {
      LOG(INFO) << "Rmad: The sensor calibration needs another round.";
      return {.error = error_code,
              .state_case = RmadState::StateCase::kSetupCalibration};
    }
  }

  // There's nothing in |RunCalibrations|.
  state_ = state;

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
      CalibrateAndSendProgress(component_status.first);
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

void RunCalibrationStateHandler::CalibrateAndSendProgress(
    RmadComponent component) {
  auto& utils = sensor_calibration_utils_map_[component];
  if (!utils.get()) {
    LOG(ERROR) << RmadComponent_Name(component)
               << " does not support calibration.";
    return;
  }

  task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(base::IgnoreResult(&SensorCalibrationUtils::Calibrate),
                     base::Unretained(utils.get())));
  LOG(INFO) << "Start calibrating for " << RmadComponent_Name(component);

  progress_timer_map_[component]->Start(
      FROM_HERE, kPollInterval,
      base::BindRepeating(&RunCalibrationStateHandler::CheckCalibrationTask,
                          this, component));
  LOG(INFO) << "Start polling calibration progress for "
            << RmadComponent_Name(component);
}

void RunCalibrationStateHandler::CheckCalibrationTask(RmadComponent component) {
  auto& utils = sensor_calibration_utils_map_[component];
  double progress = 0;

  if (!utils->GetProgress(&progress)) {
    LOG(WARNING) << "Failed to get calibration progress for "
                 << utils->GetLocation() << ":" << utils->GetName();
    return;
  }

  SaveAndSend(component, progress);
}

void RunCalibrationStateHandler::SaveAndSend(RmadComponent component,
                                             double progress) {
  CalibrationComponentStatus::CalibrationStatus status =
      CalibrationComponentStatus::RMAD_CALIBRATION_IN_PROGRESS;

  if (progress == 1.0) {
    status = CalibrationComponentStatus::RMAD_CALIBRATION_COMPLETE;
  } else if (progress < 0) {
    status = CalibrationComponentStatus::RMAD_CALIBRATION_FAILED;
  }

  auto pre_status = calibration_map_[running_group_][component];
  if (pre_status != status) {
    // This is a critical section, but we don't need to lock it.
    // Instead of using a mutex to lock the critical section, we use a timer
    // (tasks run sequentially on the main thread) to prevent race conditions.
    calibration_map_[running_group_][component] = status;
    SetCalibrationMap(json_store_, calibration_map_);

    bool is_in_progress = false;
    bool is_failed = false;
    for (auto component_status_map : calibration_map_[running_group_]) {
      is_in_progress |=
          component_status_map.second ==
          CalibrationComponentStatus::RMAD_CALIBRATION_IN_PROGRESS;
      is_failed |= component_status_map.second ==
                   CalibrationComponentStatus::RMAD_CALIBRATION_FAILED;
    }

    // We only update the overall status after all calibrations are done.
    if (!is_in_progress) {
      if (is_failed) {
        calibration_overall_signal_sender_->Run(
            CalibrationOverallStatus::
                RMAD_CALIBRATION_OVERALL_CURRENT_ROUND_FAILED);
      } else if (RmadErrorCode ignore; ShouldRecalibrate(&ignore)) {
        calibration_overall_signal_sender_->Run(
            CalibrationOverallStatus::
                RMAD_CALIBRATION_OVERALL_CURRENT_ROUND_COMPLETE);
      } else {
        calibration_overall_signal_sender_->Run(
            CalibrationOverallStatus::RMAD_CALIBRATION_OVERALL_COMPLETE);
      }
    }
  }

  CalibrationComponentStatus component_status;
  component_status.set_component(component);
  component_status.set_status(status);
  component_status.set_progress(progress);
  calibration_component_signal_sender_->Run(std::move(component_status));

  if (status != CalibrationComponentStatus::RMAD_CALIBRATION_IN_PROGRESS) {
    progress_timer_map_[component]->Stop();
  }
}

}  // namespace rmad
