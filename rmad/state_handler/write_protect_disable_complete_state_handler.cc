// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/write_protect_disable_complete_state_handler.h"

#include <memory>
#include <utility>

#include "rmad/system/cryptohome_client_impl.h"
#include "rmad/utils/cr50_utils_impl.h"

#include <base/logging.h>

namespace rmad {

WriteProtectDisableCompleteStateHandler::
    WriteProtectDisableCompleteStateHandler(scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {
  cr50_utils_ = std::make_unique<Cr50UtilsImpl>();
  cryptohome_client_ = std::make_unique<CryptohomeClientImpl>();
}

WriteProtectDisableCompleteStateHandler::
    WriteProtectDisableCompleteStateHandler(
        scoped_refptr<JsonStore> json_store,
        std::unique_ptr<Cr50Utils> cr50_utils,
        std::unique_ptr<CryptohomeClient> cryptohome_client)
    : BaseStateHandler(json_store),
      cr50_utils_(std::move(cr50_utils)),
      cryptohome_client_(std::move(cryptohome_client)) {}

RmadErrorCode WriteProtectDisableCompleteStateHandler::InitializeState() {
  // Always probe again when entering the state.
  auto wp_disable_complete =
      std::make_unique<WriteProtectDisableCompleteState>();
  // Need to keep device open to disable hardware write protect if cr50 factory
  // mode is not enabled.
  wp_disable_complete->set_keep_device_open(
      !cr50_utils_->IsFactoryModeEnabled());
  // Can enable cr50 factory mode if it's not currently enabled, and device
  // doesn't have FWMP.
  wp_disable_complete->set_can_enable_factory_mode(
      !cr50_utils_->IsFactoryModeEnabled() && !cryptohome_client_->HasFwmp());
  // Initialize to false so it doesn't violate rules under all conditions.
  wp_disable_complete->set_enable_factory_mode(false);
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

  const WriteProtectDisableCompleteState& wp_disable_complete =
      state.wp_disable_complete();
  if (wp_disable_complete.can_enable_factory_mode() !=
      state_.wp_disable_complete().can_enable_factory_mode()) {
    LOG(ERROR) << "RmadState |can_enable_factory_mode| argument doesn't match";
    return {.error = RMAD_ERROR_REQUEST_INVALID, .state_case = GetStateCase()};
  }
  if (wp_disable_complete.enable_factory_mode() &&
      !wp_disable_complete.can_enable_factory_mode()) {
    LOG(ERROR) << "Cannot enable factory mode";
    return {.error = RMAD_ERROR_REQUEST_ARGS_VIOLATION,
            .state_case = GetStateCase()};
  }

  state_ = state;
  StoreState();

  if (state.wp_disable_complete().enable_factory_mode()) {
    cr50_utils_->EnableFactoryMode();
    return {.error = RMAD_ERROR_EXPECT_REBOOT, .state_case = GetStateCase()};
  }
  return {.error = RMAD_ERROR_OK,
          .state_case = RmadState::StateCase::kUpdateRoFirmware};
}

}  // namespace rmad
