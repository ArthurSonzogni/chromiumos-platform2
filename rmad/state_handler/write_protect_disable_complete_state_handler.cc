// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/write_protect_disable_complete_state_handler.h"

#include <memory>
#include <utility>

#include "rmad/constants.h"
#include "rmad/utils/cr50_utils_impl.h"

#include <base/logging.h>

namespace rmad {

WriteProtectDisableCompleteStateHandler::
    WriteProtectDisableCompleteStateHandler(scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {
  cr50_utils_ = std::make_unique<Cr50UtilsImpl>();
}

WriteProtectDisableCompleteStateHandler::
    WriteProtectDisableCompleteStateHandler(
        scoped_refptr<JsonStore> json_store,
        std::unique_ptr<Cr50Utils> cr50_utils)
    : BaseStateHandler(json_store), cr50_utils_(std::move(cr50_utils)) {}

RmadErrorCode WriteProtectDisableCompleteStateHandler::InitializeState() {
  // Always probe again when entering the state.
  auto wp_disable_complete =
      std::make_unique<WriteProtectDisableCompleteState>();
  // Need to keep device open to disable hardware write protect if cr50 factory
  // mode is disabled.
  wp_disable_complete->set_keep_device_open(
      !cr50_utils_->IsFactoryModeEnabled());
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
