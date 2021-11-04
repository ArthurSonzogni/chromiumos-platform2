// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/write_protect_disable_complete_state_handler.h"

#include <memory>
#include <utility>

#include <base/logging.h>

#include "rmad/constants.h"

namespace rmad {

namespace fake {

FakeWriteProtectDisableCompleteStateHandler::
    FakeWriteProtectDisableCompleteStateHandler(
        scoped_refptr<JsonStore> json_store)
    : WriteProtectDisableCompleteStateHandler(json_store) {}

}  // namespace fake

WriteProtectDisableCompleteStateHandler::
    WriteProtectDisableCompleteStateHandler(scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {
}

RmadErrorCode WriteProtectDisableCompleteStateHandler::InitializeState() {
  // Always probe again when entering the state.
  auto wp_disable_complete =
      std::make_unique<WriteProtectDisableCompleteState>();
  // Need to keep device open to disable hardware write protect if the flag is
  // set in |json_store_|.
  bool keep_device_open = false;
  json_store_->GetValue(kKeepDeviceOpen, &keep_device_open);
  wp_disable_complete->set_keep_device_open(keep_device_open);
  // Check if WP disabling steps are skipped.
  bool wp_disable_skipped = false;
  json_store_->GetValue(kWpDisableSkipped, &wp_disable_skipped);
  wp_disable_complete->set_wp_disable_skipped(wp_disable_skipped);

  state_.set_allocated_wp_disable_complete(wp_disable_complete.release());
  return RMAD_ERROR_OK;
}

BaseStateHandler::GetNextStateCaseReply
WriteProtectDisableCompleteStateHandler::GetNextStateCase(
    const RmadState& state) {
  if (!state.has_wp_disable_complete()) {
    LOG(ERROR) << "RmadState missing |WP disable complete| state.";
    return {.error = RMAD_ERROR_REQUEST_INVALID, .state_case = GetStateCase()};
  }

  return {.error = RMAD_ERROR_OK,
          .state_case = RmadState::StateCase::kUpdateRoFirmware};
}

}  // namespace rmad
