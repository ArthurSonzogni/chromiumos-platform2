// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_STATE_HANDLER_WRITE_PROTECT_DISABLE_COMPLETE_STATE_HANDLER_H_
#define RMAD_STATE_HANDLER_WRITE_PROTECT_DISABLE_COMPLETE_STATE_HANDLER_H_

#include "rmad/state_handler/base_state_handler.h"

namespace rmad {

class WriteProtectDisableCompleteStateHandler : public BaseStateHandler {
 public:
  explicit WriteProtectDisableCompleteStateHandler(
      scoped_refptr<JsonStore> json_store);
  ~WriteProtectDisableCompleteStateHandler() override = default;

  // This state is not repeatable. Must go through the rest of the RMA flow
  // after disabling write protection.
  ASSIGN_STATE(RmadState::StateCase::kWpDisableComplete);

  RmadErrorCode InitializeState() override;
  GetNextStateCaseReply GetNextStateCase(const RmadState& state) override;
};

}  // namespace rmad

#endif  // RMAD_STATE_HANDLER_WRITE_PROTECT_DISABLE_COMPLETE_STATE_HANDLER_H_
