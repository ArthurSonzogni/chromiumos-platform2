// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/write_protect_enable_physical_state_handler.h"

#include <memory>
#include <utility>

#include "rmad/utils/crossystem_utils_impl.h"

#include <base/logging.h>

namespace rmad {

namespace {

// crossystem HWWP property name.
constexpr char kWriteProtectProperty[] = "wpsw_cur";

}  // namespace

WriteProtectEnablePhysicalStateHandler::WriteProtectEnablePhysicalStateHandler(
    scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {
  crossystem_utils_ = std::make_unique<CrosSystemUtilsImpl>();
}

WriteProtectEnablePhysicalStateHandler::WriteProtectEnablePhysicalStateHandler(
    scoped_refptr<JsonStore> json_store,
    std::unique_ptr<CrosSystemUtils> crossystem_utils)
    : BaseStateHandler(json_store),
      crossystem_utils_(std::move(crossystem_utils)) {}

RmadErrorCode WriteProtectEnablePhysicalStateHandler::InitializeState() {
  if (!state_.has_wp_enable_physical()) {
    state_.set_allocated_wp_enable_physical(
        new WriteProtectEnablePhysicalState);
  }
  if (!write_protect_signal_sender_) {
    return RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED;
  }

  PollUntilWriteProtectOn();
  return RMAD_ERROR_OK;
}

void WriteProtectEnablePhysicalStateHandler::CleanUpState() {
  // Stop the polling loop.
  if (timer_.IsRunning()) {
    timer_.Stop();
  }
}

BaseStateHandler::GetNextStateCaseReply
WriteProtectEnablePhysicalStateHandler::GetNextStateCase(
    const RmadState& state) {
  if (!state.has_wp_enable_physical()) {
    LOG(ERROR) << "RmadState missing |write protection enable| state.";
    return {.error = RMAD_ERROR_REQUEST_INVALID, .state_case = GetStateCase()};
  }

  int hwwp_status;
  if (crossystem_utils_->GetInt(kWriteProtectProperty, &hwwp_status) &&
      hwwp_status == 1) {
    return {.error = RMAD_ERROR_OK,
            .state_case = RmadState::StateCase::kFinalize};
  }
  return {.error = RMAD_ERROR_WAIT, .state_case = GetStateCase()};
}

void WriteProtectEnablePhysicalStateHandler::PollUntilWriteProtectOn() {
  LOG(INFO) << "Start polling write protection";
  if (timer_.IsRunning()) {
    timer_.Stop();
  }
  timer_.Start(
      FROM_HERE, kPollInterval, this,
      &WriteProtectEnablePhysicalStateHandler::CheckWriteProtectOnTask);
}

void WriteProtectEnablePhysicalStateHandler::CheckWriteProtectOnTask() {
  DCHECK(write_protect_signal_sender_);
  LOG(INFO) << "Check write protection";

  int hwwp_status;
  if (!crossystem_utils_->GetInt(kWriteProtectProperty, &hwwp_status)) {
    LOG(ERROR) << "Failed to get HWWP status";
    return;
  }
  if (hwwp_status == 1) {
    write_protect_signal_sender_->Run(true);
    timer_.Stop();
  }
}

}  // namespace rmad
