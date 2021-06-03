// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/write_protect_enable_physical_state_handler.h"

#include "rmad/utils/crossystem_utils_impl.h"

namespace rmad {

namespace {

// Poll every two seconds.
constexpr int kPollIntervalSecs = 2;

// crossystem HWWP property name.
constexpr char kWriteProtectProperty[] = "wpsw_cur";

}  // namespace

WriteProtectEnablePhysicalStateHandler::WriteProtectEnablePhysicalStateHandler(
    scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {}

RmadErrorCode WriteProtectEnablePhysicalStateHandler::InitializeState() {
  if (!state_.has_wp_enable_physical() && !RetrieveState()) {
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

  // There's nothing in |WriteProtectEnablePhysicalState|.
  state_ = state;
  StoreState();

  CrosSystemUtilsImpl crossystem_utils;
  int wp_status;
  if (crossystem_utils.GetInt(kWriteProtectProperty, &wp_status) &&
      wp_status == 1) {
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
      FROM_HERE, base::TimeDelta::FromSeconds(kPollIntervalSecs), this,
      &WriteProtectEnablePhysicalStateHandler::CheckWriteProtectOnTask);
}

void WriteProtectEnablePhysicalStateHandler::CheckWriteProtectOnTask() {
  DCHECK(write_protect_signal_sender_);
  LOG(INFO) << "Check write protection";

  CrosSystemUtilsImpl crossystem_utils;
  int wp_status;
  if (!crossystem_utils.GetInt(kWriteProtectProperty, &wp_status)) {
    LOG(ERROR) << "Failed to get HWWP status";
    return;
  }
  if (wp_status == 1) {
    write_protect_signal_sender_->Run(true);
    timer_.Stop();
  }
}

}  // namespace rmad
