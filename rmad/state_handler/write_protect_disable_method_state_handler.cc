// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/write_protect_disable_method_state_handler.h"

#include <base/logging.h>
#include <base/notreached.h>

namespace rmad {

WriteProtectDisableMethodStateHandler::WriteProtectDisableMethodStateHandler(
    scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {}

RmadErrorCode WriteProtectDisableMethodStateHandler::InitializeState() {
  if (!state_.has_wp_disable_method() && !RetrieveState()) {
    state_.set_allocated_wp_disable_method(new WriteProtectDisableMethodState);
  }
  return RMAD_ERROR_OK;
}

BaseStateHandler::GetNextStateCaseReply
WriteProtectDisableMethodStateHandler::GetNextStateCase(
    const RmadState& state) {
  if (!state.has_wp_disable_method()) {
    LOG(ERROR) << "RmadState missing |write protection disable method| state.";
    return {.error = RMAD_ERROR_REQUEST_INVALID, .state_case = GetStateCase()};
  }
  const WriteProtectDisableMethodState& wp_disable_method =
      state.wp_disable_method();
  if (wp_disable_method.disable_method() ==
      WriteProtectDisableMethodState::RMAD_WP_DISABLE_UNKNOWN) {
    return {.error = RMAD_ERROR_REQUEST_ARGS_MISSING,
            .state_case = GetStateCase()};
  }

  state_ = state;
  StoreState();

  switch (state_.wp_disable_method().disable_method()) {
    case WriteProtectDisableMethodState::RMAD_WP_DISABLE_RSU:
      return {.error = RMAD_ERROR_OK,
              .state_case = RmadState::StateCase::kWpDisableRsu};
    case WriteProtectDisableMethodState::RMAD_WP_DISABLE_PHYSICAL:
      return {.error = RMAD_ERROR_OK,
              .state_case = RmadState::StateCase::kWpDisablePhysical};
    default:
      break;
  }
  NOTREACHED();
  return {.error = RMAD_ERROR_NOT_SET,
          .state_case = RmadState::StateCase::STATE_NOT_SET};
}

}  // namespace rmad
