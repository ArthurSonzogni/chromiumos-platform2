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

  using CalibrationSignalCallback =
      base::RepeatingCallback<bool(CalibrationComponentStatus)>;
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
  bool ShouldRecalibrate(RmadErrorCode* error_code);
  void PollUntilCalibrationDone(RmadComponent component);
  void CheckCalibrationTask(
      CheckCalibrationState::CalibrationStatus::Component component);

  void CheckGyroCalibrationTask();
  void CheckBaseAccCalibrationTask();
  void CheckLidAccCalibrationTask();

  void SaveAndSend(RmadComponent component, double progress);
  bool GetPriorityCalibrationMap();
  bool SetPriorityCalibrationMap();
  bool IsValidComponent(RmadComponent component) const;

  static bool GetGyroCalibrationProgress(double* progress);
  static bool GetBaseAccCalibrationProgress(double* progress);
  static bool GetLidAccCalibrationProgress(double* progress);

  base::Lock calibration_mutex_;
  // To ensure that calibration starts from a higher priority, we use an
  // ordered map to traverse it from high to low. Once we find the first
  // sensor to be calibrated, we only calibrate those sensors that have
  // the same priority as it.
  std::map<
      int,
      std::map<RmadComponent, CalibrationComponentStatus::CalibrationStatus>>
      priority_components_calibration_map_;
  int running_priority_;
  std::unique_ptr<CalibrationSignalCallback> calibration_signal_sender_;
  std::map<RmadComponent, std::unique_ptr<base::RepeatingTimer>> timer_map_;
};

}  // namespace rmad

#endif  // RMAD_STATE_HANDLER_RUN_CALIBRATION_STATE_HANDLER_H_
