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

  void RegisterSignalSender(
      std::unique_ptr<
          base::RepeatingCallback<bool(CheckCalibrationState::CalibrationStatus,
                                       double)  // NOLINT(readability/casting)
                                  >> callback) override {
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
  void CheckAccCalibrationTask();
  void CheckGyroCalibrationTask();
  void SaveAndSend(
      CheckCalibrationState::CalibrationStatus::Component component,
      int progress_percentage);

  static bool ConvertToComponenetStatus(
      const std::pair<std::string, std::string>& id_status_str_pair,
      CheckCalibrationState::CalibrationStatus* component_status);

  static bool GetAccCalibrationProgress(int* progress_percentage);
  static bool GetGyroCalibrationProgress(int* progress_percentage);

  base::Lock calibration_mutex_;
  std::map<std::string, std::string> components_calibration_map_;
  std::unique_ptr<base::RepeatingCallback<bool(
      CheckCalibrationState::CalibrationStatus, double)>>
      calibration_signal_sender_;
  std::map<CheckCalibrationState::CalibrationStatus::Component,
           std::unique_ptr<base::RepeatingTimer>>
      timer_map_;
};

}  // namespace rmad

#endif  // RMAD_STATE_HANDLER_RUN_CALIBRATION_STATE_HANDLER_H_
