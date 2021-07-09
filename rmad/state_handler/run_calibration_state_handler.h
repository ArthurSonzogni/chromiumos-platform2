// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_STATE_HANDLER_RUN_CALIBRATION_STATE_HANDLER_H_
#define RMAD_STATE_HANDLER_RUN_CALIBRATION_STATE_HANDLER_H_

#include "rmad/state_handler/base_state_handler.h"

#include <map>
#include <memory>
#include <string>
#include <utility>

#include <base/synchronization/lock.h>
#include <base/timer/timer.h>

namespace rmad {

class RunCalibrationStateHandler : public BaseStateHandler {
 public:
  // Poll every 2 seconds.
  static constexpr base::TimeDelta kPollInterval =
      base::TimeDelta::FromSeconds(2);

  explicit RunCalibrationStateHandler(scoped_refptr<JsonStore> json_store);

  using CalibrationSignalCallback = base::RepeatingCallback<bool(
      CheckCalibrationState::CalibrationStatus, double)>;
  void RegisterSignalSender(
      std::unique_ptr<CalibrationSignalCallback> callback) override {
    calibration_signal_sender_ = std::move(callback);
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
  bool ShouldRecalibrate();
  void PollUntilCalibrationDone(
      CheckCalibrationState::CalibrationStatus::Component component);
  void CheckCalibrationTask(
      CheckCalibrationState::CalibrationStatus::Component component);

  void CheckGyroCalibrationTask();
  void CheckBaseAccCalibrationTask();
  void CheckLidAccCalibrationTask();

  void SaveAndSend(
      CheckCalibrationState::CalibrationStatus::Component component,
      int progress_percentage);
  bool GetPriorityCalibrationMap();
  bool SetPriorityCalibrationMap();

  static bool GetGyroCalibrationProgress(int* progress_percentage);
  static bool GetBaseAccCalibrationProgress(int* progress_percentage);
  static bool GetLidAccCalibrationProgress(int* progress_percentage);

  base::Lock calibration_mutex_;
  // To ensure that calibration starts from a higher priority, we use an
  // ordered map to traverse it from high to low. Once we find the first
  // sensor to be calibrated, we only calibrate those sensors that have
  // the same priority as it.
  std::map<int,
           std::map<CheckCalibrationState::CalibrationStatus::Component,
                    CheckCalibrationState::CalibrationStatus::Status>>
      priority_components_calibration_map_;
  int running_priority_;
  std::unique_ptr<CalibrationSignalCallback> calibration_signal_sender_;
  std::map<CheckCalibrationState::CalibrationStatus::Component,
           std::unique_ptr<base::RepeatingTimer>>
      timer_map_;
};

}  // namespace rmad

#endif  // RMAD_STATE_HANDLER_RUN_CALIBRATION_STATE_HANDLER_H_
