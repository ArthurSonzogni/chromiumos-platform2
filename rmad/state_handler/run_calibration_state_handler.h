// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_STATE_HANDLER_RUN_CALIBRATION_STATE_HANDLER_H_
#define RMAD_STATE_HANDLER_RUN_CALIBRATION_STATE_HANDLER_H_

#include "rmad/state_handler/base_state_handler.h"

#include <map>
#include <memory>
#include <utility>

#include <base/synchronization/lock.h>
#include <base/timer/timer.h>

#include "rmad/utils/calibration_utils.h"

namespace rmad {

class RunCalibrationStateHandler : public BaseStateHandler {
 public:
  // Poll every 2 seconds.
  static constexpr base::TimeDelta kPollInterval =
      base::TimeDelta::FromSeconds(2);

  explicit RunCalibrationStateHandler(scoped_refptr<JsonStore> json_store);

  void RegisterSignalSender(
      std::unique_ptr<CalibrationOverallSignalCallback> callback) override {
    calibration_overall_signal_sender_ = std::move(callback);
  }

  void RegisterSignalSender(
      std::unique_ptr<CalibrationComponentSignalCallback> callback) override {
    calibration_component_signal_sender_ = std::move(callback);
  }

  ASSIGN_STATE(RmadState::StateCase::kRunCalibration);
  SET_REPEATABLE;

  RmadErrorCode InitializeState() override;
  void CleanUpState() override;
  GetNextStateCaseReply GetNextStateCase(const RmadState& state) override;

 protected:
  ~RunCalibrationStateHandler() override = default;

 private:
  void RetrieveVarsAndCalibrate();
  bool ShouldRecalibrate(RmadErrorCode* error_code);
  void PollUntilCalibrationDone(RmadComponent component);

  void CheckGyroCalibrationTask();
  void CheckBaseAccCalibrationTask();
  void CheckLidAccCalibrationTask();

  void SaveAndSend(RmadComponent component, double progress);

  static bool GetGyroCalibrationProgress(double* progress);
  static bool GetBaseAccCalibrationProgress(double* progress);
  static bool GetLidAccCalibrationProgress(double* progress);

  base::Lock calibration_mutex_;
  // To ensure that calibration starts from a higher priority, we use an
  // ordered map to traverse it with its number of the setup instruction.
  // Once we find the first sensor to be calibrated, we only calibrate those
  // sensors that have the same setup instruction as it.
  InstructionCalibrationStatusMap calibration_map_;
  CalibrationSetupInstruction running_group_;
  std::unique_ptr<CalibrationOverallSignalCallback>
      calibration_overall_signal_sender_;
  std::unique_ptr<CalibrationComponentSignalCallback>
      calibration_component_signal_sender_;
  std::map<RmadComponent, std::unique_ptr<base::RepeatingTimer>> timer_map_;
};

}  // namespace rmad

#endif  // RMAD_STATE_HANDLER_RUN_CALIBRATION_STATE_HANDLER_H_
