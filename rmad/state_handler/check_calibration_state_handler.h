// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_STATE_HANDLER_CHECK_CALIBRATION_STATE_HANDLER_H_
#define RMAD_STATE_HANDLER_CHECK_CALIBRATION_STATE_HANDLER_H_

#include "rmad/state_handler/base_state_handler.h"

#include <map>
#include <string>

namespace rmad {

class CheckCalibrationStateHandler : public BaseStateHandler {
 public:
  explicit CheckCalibrationStateHandler(scoped_refptr<JsonStore> json_store);

  ASSIGN_STATE(RmadState::StateCase::kCheckCalibration);
  SET_REPEATABLE;

  RmadErrorCode InitializeState() override;
  GetNextStateCaseReply GetNextStateCase(const RmadState& state) override;

 protected:
  ~CheckCalibrationStateHandler() override = default;

 private:
  bool CheckIsCalibrationRequired(const RmadState& state,
                                  bool* need_calibration);
  // Store variables that can be used by other state handlers to make decisions.
  bool StoreVars() const;

  std::map<std::string, std::string> components_calibration_map_;
};

}  // namespace rmad

#endif  // RMAD_STATE_HANDLER_CHECK_CALIBRATION_STATE_HANDLER_H_
