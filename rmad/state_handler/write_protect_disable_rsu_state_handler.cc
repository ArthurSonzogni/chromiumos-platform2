// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/write_protect_disable_rsu_state_handler.h"

#include <memory>
#include <string>

#include "rmad/utils/cr50_utils_impl.h"

namespace rmad {

WriteProtectDisableRsuStateHandler::WriteProtectDisableRsuStateHandler(
    scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {
  ResetState();
}

RmadState::StateCase WriteProtectDisableRsuStateHandler::GetNextStateCase()
    const {
  if (unlocked_) {
    return RmadState::StateCase::kWpDisableComplete;
  }
  // Not ready to go to next state.
  return GetStateCase();
}

RmadErrorCode WriteProtectDisableRsuStateHandler::UpdateState(
    const RmadState& state) {
  if (!state.has_wp_disable_rsu()) {
    LOG(ERROR) << "RmadState missing RSU state.";
    return RMAD_ERROR_REQUEST_INVALID;
  }
  const WriteProtectDisableRsuState& wp_disable_rsu = state.wp_disable_rsu();
  if (wp_disable_rsu.challenge_code() !=
      state_.wp_disable_rsu().challenge_code()) {
    LOG(ERROR) << "Challenge code doesn't match.";
    return RMAD_ERROR_REQUEST_INVALID;
  }
  Cr50UtilsImpl cr50_utils;
  if (!cr50_utils.PerformRsu(wp_disable_rsu.unlock_code())) {
    LOG(ERROR) << "Incorrect unlock code.";
    return RMAD_ERROR_WRITE_PROTECT_DISABLE_RSU_CODE_INVALID;
  }

  state_ = state;
  unlocked_ = true;
  return RMAD_ERROR_OK;
}

RmadErrorCode WriteProtectDisableRsuStateHandler::ResetState() {
  auto wp_disable_rsu = std::make_unique<WriteProtectDisableRsuState>();
  Cr50UtilsImpl cr50_utils;
  if (std::string challenge_code;
      cr50_utils.GetRsuChallengeCode(&challenge_code)) {
    wp_disable_rsu->set_challenge_code(challenge_code);
  } else {
    return RMAD_ERROR_WRITE_PROTECT_DISABLE_RSU_NO_CHALLENGE;
  }

  state_.set_allocated_wp_disable_rsu(wp_disable_rsu.release());
  unlocked_ = false;
  return RMAD_ERROR_OK;
}

}  // namespace rmad
