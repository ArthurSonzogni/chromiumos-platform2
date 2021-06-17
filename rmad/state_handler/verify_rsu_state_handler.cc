// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/verify_rsu_state_handler.h"

#include <memory>
#include <string>
#include <utility>

#include "rmad/utils/cr50_utils_impl.h"
#include "rmad/utils/crossystem_utils_impl.h"

namespace rmad {

namespace {

// crossystem HWWP property name.
constexpr char kWriteProtectProperty[] = "wpsw_cur";

}  // namespace

VerifyRsuStateHandler::VerifyRsuStateHandler(
    scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {
  cr50_utils_ = std::make_unique<Cr50UtilsImpl>();
  crossystem_utils_ = std::make_unique<CrosSystemUtilsImpl>();
}

VerifyRsuStateHandler::VerifyRsuStateHandler(
    scoped_refptr<JsonStore> json_store,
    std::unique_ptr<Cr50Utils> cr50_utils,
    std::unique_ptr<CrosSystemUtils> crossystem_utils)
    : BaseStateHandler(json_store),
      cr50_utils_(std::move(cr50_utils)),
      crossystem_utils_(std::move(crossystem_utils)) {}

RmadErrorCode VerifyRsuStateHandler::InitializeState() {
  if (!state_.has_verify_rsu() && !RetrieveState()) {
    auto verify_rsu = std::make_unique<VerifyRsuState>();
    verify_rsu->set_success(VerifyFactoryModeEnabled());
    state_.set_allocated_verify_rsu(verify_rsu.release());
  }
  return RMAD_ERROR_OK;
}

BaseStateHandler::GetNextStateCaseReply VerifyRsuStateHandler::GetNextStateCase(
    const RmadState& state) {
  if (!state.has_verify_rsu()) {
    LOG(ERROR) << "RmadState missing |Verify RSU| state.";
    return {.error = RMAD_ERROR_REQUEST_INVALID, .state_case = GetStateCase()};
  }

  state_ = state;
  StoreState();

  if (!VerifyFactoryModeEnabled()) {
    return {.error = RMAD_ERROR_TRANSITION_FAILED,
            .state_case = GetStateCase()};
  }

  return {.error = RMAD_ERROR_OK,
          .state_case = RmadState::StateCase::kWpDisableComplete};
}

bool VerifyRsuStateHandler::VerifyFactoryModeEnabled() const {
  bool factory_mode_enabled = cr50_utils_->IsFactoryModeEnabled();
  int wp_status = 1;
  crossystem_utils_->GetInt(kWriteProtectProperty, &wp_status);
  LOG(INFO) << "VerifyRSU: Cr50 factory mode: "
            << (factory_mode_enabled ? "enabled" : "disabled");
  LOG(INFO) << "VerifyRSU: Hardware write protect: " << wp_status;
  // Factory mode enabled implies HWWP is off. Check both just to be extra sure.
  return factory_mode_enabled && (wp_status == 0);
}

}  // namespace rmad
