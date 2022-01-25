// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_STATE_HANDLER_WIPE_SELECTION_STATE_HANDLER_H_
#define RMAD_STATE_HANDLER_WIPE_SELECTION_STATE_HANDLER_H_

#include "rmad/state_handler/base_state_handler.h"

namespace rmad {

class WipeSelectionStateHandler : public BaseStateHandler {
 public:
  explicit WipeSelectionStateHandler(scoped_refptr<JsonStore> json_store);

  ASSIGN_STATE(RmadState::StateCase::kWipeSelection);
  SET_REPEATABLE;

  RmadErrorCode InitializeState() override;
  GetNextStateCaseReply GetNextStateCase(const RmadState& state) override;

  // Disable transition at boot.
  GetNextStateCaseReply TryGetNextStateCaseAtBoot() override {
    return NextStateCaseWrapper(RMAD_ERROR_TRANSITION_FAILED);
  }

 protected:
  ~WipeSelectionStateHandler() override = default;
};

namespace fake {

// Nothing needs to be faked.
class FakeWipeSelectionStateHandler : public WipeSelectionStateHandler {
 public:
  explicit FakeWipeSelectionStateHandler(scoped_refptr<JsonStore> json_store);

 protected:
  ~FakeWipeSelectionStateHandler() override = default;
};

}  // namespace fake

}  // namespace rmad

#endif  // RMAD_STATE_HANDLER_WIPE_SELECTION_STATE_HANDLER_H_
