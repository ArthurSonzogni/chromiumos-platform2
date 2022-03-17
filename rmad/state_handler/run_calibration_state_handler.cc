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
#include "rmad/utils/fake_sensor_calibration_utils.h"
#include "rmad/utils/gyroscope_calibration_utils_impl.h"

namespace rmad {

namespace fake {

FakeRunCalibrationStateHandler::FakeRunCalibrationStateHandler(
    scoped_refptr<JsonStore> json_store)
    : RunCalibrationStateHandler(
          json_store,
          std::make_unique<FakeSensorCalibrationUtils>(),
          std::make_unique<FakeSensorCalibrationUtils>(),
          std::make_unique<FakeSensorCalibrationUtils>(),
          std::make_unique<FakeSensorCalibrationUtils>()) {}

}  // namespace fake

RunCalibrationStateHandler::RunCalibrationStateHandler(
    scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store),
      calibration_overall_signal_sender_(base::DoNothing()),
      calibration_component_signal_sender_(base::DoNothing()) {
  vpd_utils_thread_safe_ = base::MakeRefCounted<VpdUtilsImplThreadSafe>();
  sensor_calibration_utils_map_[RMAD_COMPONENT_BASE_ACCELEROMETER] =
      std::make_unique<AccelerometerCalibrationUtilsImpl>(
          vpd_utils_thread_safe_, "base");
  sensor_calibration_utils_map_[RMAD_COMPONENT_LID_ACCELEROMETER] =
      std::make_unique<AccelerometerCalibrationUtilsImpl>(
          vpd_utils_thread_safe_, "lid");
  sensor_calibration_utils_map_[RMAD_COMPONENT_BASE_GYROSCOPE] =
      std::make_unique<GyroscopeCalibrationUtilsImpl>(vpd_utils_thread_safe_,
                                                      "base");
  sensor_calibration_utils_map_[RMAD_COMPONENT_LID_GYROSCOPE] =
      std::make_unique<GyroscopeCalibrationUtilsImpl>(vpd_utils_thread_safe_,
                                                      "lid");
}

RunCalibrationStateHandler::RunCalibrationStateHandler(
    scoped_refptr<JsonStore> json_store,
    std::unique_ptr<SensorCalibrationUtils> base_acc_utils,
    std::unique_ptr<SensorCalibrationUtils> lid_acc_utils,
    std::unique_ptr<SensorCalibrationUtils> base_gyro_utils,
    std::unique_ptr<SensorCalibrationUtils> lid_gyro_utils)
    : BaseStateHandler(json_store),
      calibration_overall_signal_sender_(base::DoNothing()),
      calibration_component_signal_sender_(base::DoNothing()) {
  sensor_calibration_utils_map_[RMAD_COMPONENT_BASE_ACCELEROMETER] =
      std::move(base_acc_utils);
  sensor_calibration_utils_map_[RMAD_COMPONENT_LID_ACCELEROMETER] =
      std::move(lid_acc_utils);
  sensor_calibration_utils_map_[RMAD_COMPONENT_BASE_GYROSCOPE] =
      std::move(base_gyro_utils);
  sensor_calibration_utils_map_[RMAD_COMPONENT_LID_GYROSCOPE] =
      std::move(lid_gyro_utils);
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
  progress_timer_map_[RMAD_COMPONENT_BASE_ACCELEROMETER] =
      std::make_unique<base::RepeatingTimer>();
  progress_timer_map_[RMAD_COMPONENT_LID_ACCELEROMETER] =
      std::make_unique<base::RepeatingTimer>();
  progress_timer_map_[RMAD_COMPONENT_BASE_GYROSCOPE] =
      std::make_unique<base::RepeatingTimer>();
  progress_timer_map_[RMAD_COMPONENT_LID_GYROSCOPE] =
      std::make_unique<base::RepeatingTimer>();

  // We will run the calibration in RetrieveVarsAndCalibrate.
  if (!RetrieveVarsAndCalibrate()) {
    return RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED;
  }
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
  if (vpd_utils_thread_safe_.get()) {
    vpd_utils_thread_safe_->FlushOutRoVpdCache();
  }
}

BaseStateHandler::GetNextStateCaseReply
RunCalibrationStateHandler::GetNextStateCase(const RmadState& state) {
  if (!state.has_run_calibration()) {
    LOG(ERROR) << "RmadState missing |run calibration| state.";
    return NextStateCaseWrapper(RMAD_ERROR_REQUEST_INVALID);
  }

  // Since the actual calibration has already started in InitializeState,
  // Chrome should wait for the signal to trigger GetNextStateCaseReply. Under
  // normal circumstances, we expect that the calibration has been completed
  // here.
  CalibrationSetupInstruction instruction =
      GetCurrentSetupInstruction(calibration_map_);
  if (instruction == RMAD_CALIBRATION_INSTRUCTION_NEED_TO_CHECK) {
    LOG(ERROR) << "Rmad: Sensor calibration is failed.";
    return NextStateCaseWrapper(RmadState::StateCase::kCheckCalibration);
  } else if (instruction == RMAD_CALIBRATION_INSTRUCTION_NO_NEED_CALIBRATION) {
    if (bool keep_device_open;
        json_store_->GetValue(kKeepDeviceOpen, &keep_device_open) &&
        keep_device_open) {
      return NextStateCaseWrapper(RmadState::StateCase::kWpEnablePhysical);
    } else {
      return NextStateCaseWrapper(RmadState::StateCase::kFinalize);
    }
  } else if (instruction == running_group_) {
    LOG(INFO) << "Rmad: Sensor calibrations is still running.";
    return NextStateCaseWrapper(RMAD_ERROR_WAIT);
  } else {
    LOG(INFO) << "Rmad: Sensor calibration needs another round.";
    return NextStateCaseWrapper(RmadState::StateCase::kSetupCalibration);
  }
}

BaseStateHandler::GetNextStateCaseReply
RunCalibrationStateHandler::TryGetNextStateCaseAtBoot() {
  // Since we do not allow calibration without setup, it should not be started
  // in this state. The transition to kCheckCalibration provides users with more
  // information.
  return NextStateCaseWrapper(RmadState::StateCase::kCheckCalibration);
}

bool RunCalibrationStateHandler::RetrieveVarsAndCalibrate() {
  if (!GetCalibrationMap(json_store_, &calibration_map_)) {
    calibration_overall_signal_sender_.Run(
        RMAD_CALIBRATION_OVERALL_INITIALIZATION_FAILED);
    LOG(ERROR) << "Failed to read calibration variables";
    return false;
  }

  // Mark unexpected status to failed.
  for (auto [instruction, components] : calibration_map_) {
    for (auto [component, status] : components) {
      if (IsInProgressStatus(status) || IsUnknownStatus(status)) {
        calibration_map_[instruction][component] =
            CalibrationComponentStatus::RMAD_CALIBRATION_FAILED;
      }
    }
  }

  if (!SetCalibrationMap(json_store_, calibration_map_)) {
    LOG(ERROR) << "Failed to set calibration variables";
    return false;
  }

  running_group_ = GetCurrentSetupInstruction(calibration_map_);
  // It failed in the beginning, this shouldn't happen.
  if (running_group_ == RMAD_CALIBRATION_INSTRUCTION_NEED_TO_CHECK) {
    calibration_overall_signal_sender_.Run(
        RMAD_CALIBRATION_OVERALL_INITIALIZATION_FAILED);
    LOG(WARNING) << "Calibration process failed at the beginning, this "
                    "shouldn't happen.";
    return true;
  }

  // It was done at the beginning, this shouldn't happen.
  if (running_group_ == RMAD_CALIBRATION_INSTRUCTION_NO_NEED_CALIBRATION) {
    calibration_overall_signal_sender_.Run(RMAD_CALIBRATION_OVERALL_COMPLETE);
    LOG(WARNING) << "Calibration process complete at the beginning, this "
                    "shouldn't happen.";
    return true;
  }

  for (auto [component, status] : calibration_map_[running_group_]) {
    if (status == CalibrationComponentStatus::RMAD_CALIBRATION_WAITING) {
      CalibrateAndSendProgress(component);
    }
  }

  return true;
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

  if (progress >= 1.0) {
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

    bool in_progress = false;
    bool failed = false;
    for (auto [ignore, other_status] : calibration_map_[running_group_]) {
      in_progress |= IsInProgressStatus(other_status) ||
                     IsWaitingForCalibration(other_status);
      failed |=
          other_status == CalibrationComponentStatus::RMAD_CALIBRATION_FAILED;
    }

    // We only update the overall status after all calibrations are done.
    if (!in_progress) {
      failed |= (vpd_utils_thread_safe_.get() &&
                 !vpd_utils_thread_safe_->FlushOutRoVpdCache());
      if (failed) {
        calibration_overall_signal_sender_.Run(
            CalibrationOverallStatus::
                RMAD_CALIBRATION_OVERALL_CURRENT_ROUND_FAILED);
      } else if (GetCurrentSetupInstruction(calibration_map_) ==
                 RMAD_CALIBRATION_INSTRUCTION_NO_NEED_CALIBRATION) {
        calibration_overall_signal_sender_.Run(
            CalibrationOverallStatus::RMAD_CALIBRATION_OVERALL_COMPLETE);
      } else {
        calibration_overall_signal_sender_.Run(
            CalibrationOverallStatus::
                RMAD_CALIBRATION_OVERALL_CURRENT_ROUND_COMPLETE);
      }
    }
  }

  CalibrationComponentStatus component_status;
  component_status.set_component(component);
  component_status.set_status(status);
  component_status.set_progress(progress);
  calibration_component_signal_sender_.Run(std::move(component_status));

  if (status != CalibrationComponentStatus::RMAD_CALIBRATION_IN_PROGRESS) {
    progress_timer_map_[component]->Stop();
  }
}

}  // namespace rmad
