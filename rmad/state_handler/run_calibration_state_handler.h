// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_STATE_HANDLER_RUN_CALIBRATION_STATE_HANDLER_H_
#define RMAD_STATE_HANDLER_RUN_CALIBRATION_STATE_HANDLER_H_

#include "rmad/state_handler/base_state_handler.h"

#include <map>
#include <memory>
#include <utility>

#include <base/memory/ref_counted.h>
#include <base/memory/scoped_refptr.h>
#include <base/task/task_runner.h>
#include <base/timer/timer.h>

#include "rmad/utils/calibration_utils.h"
#include "rmad/utils/sensor_calibration_utils.h"
#include "rmad/utils/vpd_utils_impl_thread_safe.h"

namespace rmad {

class RunCalibrationStateHandler : public BaseStateHandler {
 public:
  // Poll every 2 seconds.
  static constexpr base::TimeDelta kPollInterval = base::Seconds(2);

  explicit RunCalibrationStateHandler(scoped_refptr<JsonStore> json_store);

  // Used to inject |base_acc_utils|, |lid_acc_utils|, |base_gyro_utils|, and
  // |lid_gyro_utils| to mock |sensor_calibration_utils_map_| for testing.
  RunCalibrationStateHandler(
      scoped_refptr<JsonStore> json_store,
      std::unique_ptr<SensorCalibrationUtils> base_acc_utils,
      std::unique_ptr<SensorCalibrationUtils> lid_acc_utils,
      std::unique_ptr<SensorCalibrationUtils> base_gyro_utils,
      std::unique_ptr<SensorCalibrationUtils> lid_gyro_utils);

  void RegisterSignalSender(
      CalibrationOverallSignalCallback callback) override {
    calibration_overall_signal_sender_ = callback;
  }

  void RegisterSignalSender(
      CalibrationComponentSignalCallback callback) override {
    calibration_component_signal_sender_ = callback;
  }

  ASSIGN_STATE(RmadState::StateCase::kRunCalibration);
  SET_REPEATABLE;

  RmadErrorCode InitializeState() override;
  void CleanUpState() override;
  GetNextStateCaseReply GetNextStateCase(const RmadState& state) override;
  GetNextStateCaseReply TryGetNextStateCaseAtBoot() override;

 protected:
  ~RunCalibrationStateHandler() override = default;

 private:
  bool RetrieveVarsAndCalibrate();
  void CalibrateAndSendProgress(RmadComponent component);

  void CheckCalibrationTask(RmadComponent component);

  void SaveAndSend(RmadComponent component, double progress);

  // To ensure that calibration starts from a higher priority, we use an
  // ordered map to traverse it with its number of the setup instruction.
  // Once we find the first sensor to be calibrated, we only calibrate those
  // sensors that have the same setup instruction as it.
  InstructionCalibrationStatusMap calibration_map_;
  // The instruction that has been setup in previous handler.
  CalibrationSetupInstruction setup_instruction_;
  // The instruction that we are going to calibrate (might be the same as the
  // setup instruction when this cycle has not completed or the next instruction
  // after the current cycle has completed).
  CalibrationSetupInstruction running_instruction_;
  CalibrationOverallSignalCallback calibration_overall_signal_sender_;
  CalibrationComponentSignalCallback calibration_component_signal_sender_;

  // For each sensor, we should have its own utils to run calibration and poll
  // progress.
  std::map<RmadComponent, std::unique_ptr<SensorCalibrationUtils>>
      sensor_calibration_utils_map_;
  // Instead of using a mutex to lock the critical section, we use a timer
  // (tasks run sequentially on the main thread) to poll the progress.
  std::map<RmadComponent, std::unique_ptr<base::RepeatingTimer>>
      progress_timer_map_;
  // To run sensor calibration with the same setup simultaneously, we use a
  // normal task_runner to do it.
  scoped_refptr<base::TaskRunner> task_runner_;
  scoped_refptr<VpdUtilsImplThreadSafe> vpd_utils_thread_safe_;
};

namespace fake {

class FakeRunCalibrationStateHandler : public RunCalibrationStateHandler {
 public:
  explicit FakeRunCalibrationStateHandler(scoped_refptr<JsonStore> json_store);

 protected:
  ~FakeRunCalibrationStateHandler() override = default;
};

}  // namespace fake

}  // namespace rmad

#endif  // RMAD_STATE_HANDLER_RUN_CALIBRATION_STATE_HANDLER_H_
