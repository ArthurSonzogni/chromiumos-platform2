// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_STATE_HANDLER_SETUP_CALIBRATION_STATE_HANDLER_H_
#define RMAD_STATE_HANDLER_SETUP_CALIBRATION_STATE_HANDLER_H_

#include <memory>

#include "rmad/state_handler/base_state_handler.h"

namespace rmad {

class SetupCalibrationStateHandler : public BaseStateHandler {
 public:
  explicit SetupCalibrationStateHandler(scoped_refptr<JsonStore> json_store);

  void RegisterSignalSender(
      std::unique_ptr<CalibrationSetupSignalCallback> callback) override {
    calibration_setup_signal_sender_ = std::move(callback);
  }

  ASSIGN_STATE(RmadState::StateCase::kSetupCalibration);
  SET_REPEATABLE;

  RmadErrorCode InitializeState() override;
  GetNextStateCaseReply GetNextStateCase(const RmadState& state) override;

 protected:
  ~SetupCalibrationStateHandler() override = default;

 private:
  std::unique_ptr<CalibrationSetupSignalCallback>
      calibration_setup_signal_sender_;
};

}  // namespace rmad

#endif  // RMAD_STATE_HANDLER_SETUP_CALIBRATION_STATE_HANDLER_H_
