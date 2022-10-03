// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/run_calibration_state_handler.h"

#include <algorithm>
#include <memory>
#include <string>

#include <base/logging.h>
#include <base/task/task_traits.h>
#include <base/task/thread_pool.h>
#include <base/threading/thread_task_runner_handle.h>

#include "rmad/utils/accelerometer_calibration_utils_impl.h"
#include "rmad/utils/calibration_utils.h"
#include "rmad/utils/gyroscope_calibration_utils_impl.h"

namespace rmad {

RunCalibrationStateHandler::RunCalibrationStateHandler(
    scoped_refptr<JsonStore> json_store,
    scoped_refptr<DaemonCallback> daemon_callback)
    : BaseStateHandler(json_store, daemon_callback) {
  DETACH_FROM_SEQUENCE(sequence_checker_);
  vpd_utils_ = std::make_unique<VpdUtilsImpl>();
  sensor_calibration_utils_map_[RMAD_COMPONENT_BASE_ACCELEROMETER] =
      std::make_unique<AccelerometerCalibrationUtilsImpl>("base");
  sensor_calibration_utils_map_[RMAD_COMPONENT_LID_ACCELEROMETER] =
      std::make_unique<AccelerometerCalibrationUtilsImpl>("lid");
  sensor_calibration_utils_map_[RMAD_COMPONENT_BASE_GYROSCOPE] =
      std::make_unique<GyroscopeCalibrationUtilsImpl>("base");
  sensor_calibration_utils_map_[RMAD_COMPONENT_LID_GYROSCOPE] =
      std::make_unique<GyroscopeCalibrationUtilsImpl>("lid");
}

RunCalibrationStateHandler::RunCalibrationStateHandler(
    scoped_refptr<JsonStore> json_store,
    scoped_refptr<DaemonCallback> daemon_callback,
    std::unique_ptr<SensorCalibrationUtils> base_acc_utils,
    std::unique_ptr<SensorCalibrationUtils> lid_acc_utils,
    std::unique_ptr<SensorCalibrationUtils> base_gyro_utils,
    std::unique_ptr<SensorCalibrationUtils> lid_gyro_utils,
    std::unique_ptr<VpdUtils> vpd_utils)
    : BaseStateHandler(json_store, daemon_callback),
      vpd_utils_(std::move(vpd_utils)) {
  DETACH_FROM_SEQUENCE(sequence_checker_);
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
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!state_.has_run_calibration()) {
    state_.set_allocated_run_calibration(new RunCalibrationState);
    calibration_task_runner_ = base::ThreadPool::CreateTaskRunner(
        {base::TaskPriority::BEST_EFFORT, base::MayBlock()});
    sequenced_task_runner_ = base::SequencedTaskRunnerHandle::Get();
  }
  setup_instruction_ = RMAD_CALIBRATION_INSTRUCTION_NEED_TO_CHECK;
  if (std::string calibration_instruction;
      !json_store_->GetValue(kCalibrationInstruction,
                             &calibration_instruction) ||
      !CalibrationSetupInstruction_Parse(calibration_instruction,
                                         &setup_instruction_)) {
    LOG(WARNING) << "Device hasn't been setup for calibration yet!";
  }

  // We will run the calibration in RetrieveVarsAndCalibrate.
  if (!RetrieveVarsAndCalibrate()) {
    return RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED;
  }
  return RMAD_ERROR_OK;
}

void RunCalibrationStateHandler::CleanUpState() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

BaseStateHandler::GetNextStateCaseReply
RunCalibrationStateHandler::GetNextStateCase(const RmadState& state) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!state.has_run_calibration()) {
    LOG(ERROR) << "RmadState missing |run calibration| state.";
    return NextStateCaseWrapper(RMAD_ERROR_REQUEST_INVALID);
  }

  // kWipeDevice should be set by previous states.
  bool wipe_device;
  if (!json_store_->GetValue(kWipeDevice, &wipe_device)) {
    LOG(ERROR) << "Variable " << kWipeDevice << " not found";
    return NextStateCaseWrapper(RMAD_ERROR_TRANSITION_FAILED);
  }

  // Since the actual calibration has already started in InitializeState,
  // Chrome should wait for the signal to trigger GetNextStateCaseReply. Under
  // normal circumstances, we expect that the calibration has been completed
  // here. Therefore, the running instruction (which will be updated during
  // InitializeState) should be set to the next calibration instruction.
  if (running_instruction_ == RMAD_CALIBRATION_INSTRUCTION_NEED_TO_CHECK) {
    LOG(ERROR) << "Rmad: Sensor calibration failed.";
    return NextStateCaseWrapper(RmadState::StateCase::kCheckCalibration);
  } else if (running_instruction_ ==
             RMAD_CALIBRATION_INSTRUCTION_NO_NEED_CALIBRATION) {
    if (wipe_device) {
      return NextStateCaseWrapper(RmadState::StateCase::kFinalize);
    } else {
      return NextStateCaseWrapper(RmadState::StateCase::kWpEnablePhysical);
    }
  } else if (running_instruction_ == setup_instruction_) {
    LOG(INFO) << "Rmad: Sensor calibrations is still running.";
    return NextStateCaseWrapper(RMAD_ERROR_WAIT);
  } else {
    LOG(INFO) << "Rmad: Sensor calibration needs another round.";
    LOG(INFO) << CalibrationSetupInstruction_Name(running_instruction_);
    return NextStateCaseWrapper(RmadState::StateCase::kSetupCalibration);
  }
}

BaseStateHandler::GetNextStateCaseReply
RunCalibrationStateHandler::TryGetNextStateCaseAtBoot() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // We don't expect any reboot during calibration, and here is the part right
  // after rebooting. Therefore, we will mark any unexpected status to failed
  // and transition to kCheckCalibration for further error handling.
  for (auto [instruction, components] : calibration_map_) {
    for (auto [component, status] : components) {
      if (IsInProgressStatus(status) || IsUnknownStatus(status)) {
        calibration_map_[instruction][component] =
            CalibrationComponentStatus::RMAD_CALIBRATION_FAILED;
      }
    }
  }

  // Since we want to keep all error handling in kCheckCalibration , it is
  // only logged here if writing to the status file fails.
  if (!SetCalibrationMap(json_store_, calibration_map_)) {
    LOG(ERROR) << "Failed to set calibration variables";
  }

  return NextStateCaseWrapper(RmadState::StateCase::kCheckCalibration);
}

bool RunCalibrationStateHandler::RetrieveVarsAndCalibrate() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!GetCalibrationMap(json_store_, &calibration_map_)) {
    SendOverallSignal(RMAD_CALIBRATION_OVERALL_INITIALIZATION_FAILED);
    LOG(ERROR) << "Failed to read calibration variables";
    return false;
  }

  running_instruction_ = GetCurrentSetupInstruction(calibration_map_);
  if (running_instruction_ == RMAD_CALIBRATION_INSTRUCTION_NEED_TO_CHECK) {
    SendOverallSignal(RMAD_CALIBRATION_OVERALL_INITIALIZATION_FAILED);
    return true;
  }

  if (running_instruction_ ==
      RMAD_CALIBRATION_INSTRUCTION_NO_NEED_CALIBRATION) {
    SendOverallSignal(RMAD_CALIBRATION_OVERALL_COMPLETE);
    return true;
  }

  if (setup_instruction_ == running_instruction_) {
    for (auto [component, status] : calibration_map_[running_instruction_]) {
      if (status == CalibrationComponentStatus::RMAD_CALIBRATION_WAITING) {
        CalibrateAndSendProgress(component);
      }
    }
  }

  return true;
}

void RunCalibrationStateHandler::CalibrateAndSendProgress(
    RmadComponent component) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto& utils = sensor_calibration_utils_map_[component];
  if (!utils.get()) {
    LOG(ERROR) << RmadComponent_Name(component)
               << " does not support calibration.";
    return;
  }

  // We should set the state to in-progress here to handle the issue of
  // reinitialization before the first polling task (set status and send
  // progress).
  calibration_map_[setup_instruction_][component] =
      CalibrationComponentStatus::RMAD_CALIBRATION_IN_PROGRESS;
  SetCalibrationMap(json_store_, calibration_map_);

  calibration_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(
          &SensorCalibrationUtils::Calibrate, base::Unretained(utils.get()),
          base::BindRepeating(
              &RunCalibrationStateHandler::UpdateCalibrationProgress,
              base::Unretained(this), component),
          base::BindOnce(&RunCalibrationStateHandler::UpdateCalibrationResult,
                         base::Unretained(this))));
  LOG(INFO) << "Start calibrating for " << RmadComponent_Name(component);
}

void RunCalibrationStateHandler::UpdateCalibrationProgress(
    RmadComponent component, double progress) {
  DETACH_FROM_SEQUENCE(sequence_checker_);
  sequenced_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&RunCalibrationStateHandler::SaveAndSend,
                                base::Unretained(this), component, progress));
}

void RunCalibrationStateHandler::UpdateCalibrationResult(
    const std::map<std::string, int>& result) {
  sequenced_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(base::IgnoreResult(&VpdUtils::SetCalibbias),
                                base::Unretained(vpd_utils_.get()), result));
}

void RunCalibrationStateHandler::SaveAndSend(RmadComponent component,
                                             double progress) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  CalibrationComponentStatus::CalibrationStatus status =
      CalibrationComponentStatus::RMAD_CALIBRATION_IN_PROGRESS;

  if (progress >= 1.0) {
    status = CalibrationComponentStatus::RMAD_CALIBRATION_COMPLETE;
  } else if (progress < 0) {
    status = CalibrationComponentStatus::RMAD_CALIBRATION_FAILED;
  }

  CalibrationComponentStatus component_status;
  component_status.set_component(component);
  component_status.set_status(status);
  component_status.set_progress(progress);
  sequenced_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&RunCalibrationStateHandler::SendComponentSignal,
                     base::Unretained(this), component_status));

  auto pre_status = calibration_map_[setup_instruction_][component];
  if (pre_status != status) {
    calibration_map_[setup_instruction_][component] = status;
    SetCalibrationMap(json_store_, calibration_map_);

    bool in_progress = false;
    bool failed = false;
    for (auto [ignore, other_status] : calibration_map_[setup_instruction_]) {
      in_progress |= IsInProgressStatus(other_status) ||
                     IsWaitingForCalibration(other_status);
      failed |=
          other_status == CalibrationComponentStatus::RMAD_CALIBRATION_FAILED;
    }

    // We only update the overall status after all calibrations are done.
    if (!in_progress) {
      failed |= (vpd_utils_.get() && !vpd_utils_->FlushOutRoVpdCache());
      CalibrationOverallStatus overall_status;
      if (failed) {
        overall_status = CalibrationOverallStatus::
            RMAD_CALIBRATION_OVERALL_CURRENT_ROUND_FAILED;
      } else if (GetCurrentSetupInstruction(calibration_map_) ==
                 RMAD_CALIBRATION_INSTRUCTION_NO_NEED_CALIBRATION) {
        overall_status =
            CalibrationOverallStatus::RMAD_CALIBRATION_OVERALL_COMPLETE;
      } else {
        overall_status = CalibrationOverallStatus::
            RMAD_CALIBRATION_OVERALL_CURRENT_ROUND_COMPLETE;
      }
      // Since Chrome will trigger state transitions after receiving the overall
      // signal, we should post the overall signal after everything is done to
      // prevent another call from the state transition from breaking the
      // sequence.
      sequenced_task_runner_->PostTask(
          FROM_HERE,
          base::BindOnce(&RunCalibrationStateHandler::SendOverallSignal,
                         base::Unretained(this), overall_status));
    }
  }
}

void RunCalibrationStateHandler::SendComponentSignal(
    CalibrationComponentStatus component_status) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  daemon_callback_->GetCalibrationComponentSignalCallback().Run(
      component_status);
}

void RunCalibrationStateHandler::SendOverallSignal(
    CalibrationOverallStatus overall_status) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  daemon_callback_->GetCalibrationOverallSignalCallback().Run(overall_status);
}

}  // namespace rmad
