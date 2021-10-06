// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_STATE_HANDLER_CHECK_CALIBRATION_STATE_HANDLER_H_
#define RMAD_STATE_HANDLER_CHECK_CALIBRATION_STATE_HANDLER_H_

#include "rmad/state_handler/base_state_handler.h"

#include <map>
#include <memory>

#include "rmad/utils/calibration_utils.h"
#include "rmad/utils/iio_sensor_probe_utils.h"

namespace rmad {

class CheckCalibrationStateHandler : public BaseStateHandler {
 public:
  explicit CheckCalibrationStateHandler(scoped_refptr<JsonStore> json_store);
  // Used to inject mocked |iio_sensor_probe_utils_| for testing.
  CheckCalibrationStateHandler(
      scoped_refptr<JsonStore> json_store,
      std::unique_ptr<IioSensorProbeUtils> iio_sensor_probe_utils);

  ASSIGN_STATE(RmadState::StateCase::kCheckCalibration);
  SET_REPEATABLE;

  RmadErrorCode InitializeState() override;
  GetNextStateCaseReply GetNextStateCase(const RmadState& state) override;

 protected:
  ~CheckCalibrationStateHandler() override = default;

 private:
  bool CheckIsUserSelectionValid(const CheckCalibrationState& user_selection,
                                 RmadErrorCode* error_code);
  bool CheckIsCalibrationRequired(const RmadState& state,
                                  bool* need_calibration,
                                  RmadErrorCode* error_code);

  std::unique_ptr<IioSensorProbeUtils> iio_sensor_probe_utils_;

  // To ensure that calibration starts from a higher priority, we use an
  // ordered map to traverse the enumerator of its setup instruction.
  // Once we find the first sensor to be calibrated, we only calibrate those
  // sensors that have the same setup instruction as it.
  InstructionCalibrationStatusMap calibration_map_;
};

}  // namespace rmad

#endif  // RMAD_STATE_HANDLER_CHECK_CALIBRATION_STATE_HANDLER_H_
