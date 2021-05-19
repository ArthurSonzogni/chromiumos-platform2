// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_STATE_HANDLER_WRITE_PROTECT_DISABLE_METHOD_STATE_HANDLER_H_
#define RMAD_STATE_HANDLER_WRITE_PROTECT_DISABLE_METHOD_STATE_HANDLER_H_

#include "rmad/state_handler/base_state_handler.h"

namespace rmad {

class WriteProtectDisableMethodStateHandler : public BaseStateHandler {
 public:
  explicit WriteProtectDisableMethodStateHandler(
      scoped_refptr<JsonStore> json_store);
  ~WriteProtectDisableMethodStateHandler() override = default;

  ASSIGN_STATE(RmadState::StateCase::kWpDisableMethod);
  SET_REPEATABLE;

  RmadErrorCode InitializeState() override;
  GetNextStateCaseReply GetNextStateCase(const RmadState& state) override;
};

}  // namespace rmad

#endif  // RMAD_STATE_HANDLER_WRITE_PROTECT_DISABLE_METHOD_STATE_HANDLER_H_
