// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_STATE_HANDLER_PROVISION_DEVICE_STATE_HANDLER_H_
#define RMAD_STATE_HANDLER_PROVISION_DEVICE_STATE_HANDLER_H_

#include "rmad/state_handler/base_state_handler.h"

namespace rmad {

class ProvisionDeviceStateHandler : public BaseStateHandler {
 public:
  explicit ProvisionDeviceStateHandler(scoped_refptr<JsonStore> json_store);
  ~ProvisionDeviceStateHandler() override = default;

  ASSIGN_STATE(RmadState::StateCase::kProvisionDevice);
  SET_REPEATABLE;

  GetNextStateCaseReply GetNextStateCase(const RmadState& state) override;
  RmadErrorCode ResetState() override;
};

}  // namespace rmad

#endif  // RMAD_STATE_HANDLER_PROVISION_DEVICE_STATE_HANDLER_H_
