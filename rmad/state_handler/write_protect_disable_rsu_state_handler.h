// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_STATE_HANDLER_WRITE_PROTECT_DISABLE_RSU_STATE_HANDLER_H_
#define RMAD_STATE_HANDLER_WRITE_PROTECT_DISABLE_RSU_STATE_HANDLER_H_

#include "rmad/state_handler/base_state_handler.h"

#include <memory>

namespace rmad {

class Cr50Utils;

class WriteProtectDisableRsuStateHandler : public BaseStateHandler {
 public:
  explicit WriteProtectDisableRsuStateHandler(
      scoped_refptr<JsonStore> json_store);
  // Used to inject mock |cr50_utils_| for testing.
  WriteProtectDisableRsuStateHandler(scoped_refptr<JsonStore> json_store,
                                     std::unique_ptr<Cr50Utils> cr50_utils);
  ~WriteProtectDisableRsuStateHandler() override = default;

  ASSIGN_STATE(RmadState::StateCase::kWpDisableRsu);
  SET_REPEATABLE;

  RmadErrorCode InitializeState() override;
  GetNextStateCaseReply GetNextStateCase(const RmadState& state) override;

 private:
  std::unique_ptr<Cr50Utils> cr50_utils_;
};

}  // namespace rmad

#endif  // RMAD_STATE_HANDLER_WRITE_PROTECT_DISABLE_RSU_STATE_HANDLER_H_
