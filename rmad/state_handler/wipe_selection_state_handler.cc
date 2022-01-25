// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/wipe_selection_state_handler.h"

#include <base/logging.h>

#include "rmad/constants.h"

namespace rmad {

namespace fake {

FakeWipeSelectionStateHandler::FakeWipeSelectionStateHandler(
    scoped_refptr<JsonStore> json_store)
    : WipeSelectionStateHandler(json_store) {}

}  // namespace fake

WipeSelectionStateHandler::WipeSelectionStateHandler(
    scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {}

RmadErrorCode WipeSelectionStateHandler::InitializeState() {
  if (!state_.has_wipe_selection()) {
    state_.set_allocated_wipe_selection(new WipeSelectionState);
  }
  return RMAD_ERROR_OK;
}

BaseStateHandler::GetNextStateCaseReply
WipeSelectionStateHandler::GetNextStateCase(const RmadState& state) {
  if (!state.has_wipe_selection()) {
    LOG(ERROR) << "RmadState missing |wipe selection| state.";
    return NextStateCaseWrapper(RMAD_ERROR_REQUEST_INVALID);
  }

  if (!json_store_->SetValue(kWipeDevice,
                             state.wipe_selection().wipe_device())) {
    LOG(ERROR) << "Failed to set |wipe device| option";
    return NextStateCaseWrapper(RMAD_ERROR_TRANSITION_FAILED);
  }

  // TODO(chenghan): This is currently fake.
  return NextStateCaseWrapper(RmadState::StateCase::kWpDisableMethod);
}

}  // namespace rmad
