// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/write_protect_disable_physical_state_handler.h"

#include <memory>
#include <utility>

#include "rmad/utils/crossystem_utils_impl.h"

namespace rmad {

namespace {

// crossystem HWWP property name.
constexpr char kWriteProtectProperty[] = "wpsw_cur";

}  // namespace

WriteProtectDisablePhysicalStateHandler::
    WriteProtectDisablePhysicalStateHandler(scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {
  crossystem_utils_ = std::make_unique<CrosSystemUtilsImpl>();
}

WriteProtectDisablePhysicalStateHandler::
    WriteProtectDisablePhysicalStateHandler(
        scoped_refptr<JsonStore> json_store,
        std::unique_ptr<CrosSystemUtils> crossystem_utils)
    : BaseStateHandler(json_store),
      crossystem_utils_(std::move(crossystem_utils)) {}

RmadErrorCode WriteProtectDisablePhysicalStateHandler::InitializeState() {
  if (!state_.has_wp_disable_physical() && !RetrieveState()) {
    state_.set_allocated_wp_disable_physical(
        new WriteProtectDisablePhysicalState);
  }
  if (!write_protect_signal_sender_) {
    return RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED;
  }

  PollUntilWriteProtectOff();
  return RMAD_ERROR_OK;
}

void WriteProtectDisablePhysicalStateHandler::CleanUpState() {
  // Stop the polling loop.
  if (timer_.IsRunning()) {
    timer_.Stop();
  }
}

BaseStateHandler::GetNextStateCaseReply
WriteProtectDisablePhysicalStateHandler::GetNextStateCase(
    const RmadState& state) {
  if (!state.has_wp_disable_physical()) {
    LOG(ERROR) << "RmadState missing |physical write protection| state.";
    return {.error = RMAD_ERROR_REQUEST_INVALID, .state_case = GetStateCase()};
  }

  // There's nothing in |WriteProtectDisablePhysicalState|.
  state_ = state;
  StoreState();

  int wp_status;
  if (crossystem_utils_->GetInt(kWriteProtectProperty, &wp_status) &&
      wp_status == 0) {
    return {.error = RMAD_ERROR_OK,
            .state_case = RmadState::StateCase::kWpDisableComplete};
  }

  return {.error = RMAD_ERROR_WAIT, .state_case = GetStateCase()};
}

void WriteProtectDisablePhysicalStateHandler::PollUntilWriteProtectOff() {
  LOG(INFO) << "Start polling write protection";
  if (timer_.IsRunning()) {
    timer_.Stop();
  }
  timer_.Start(
      FROM_HERE, kPollInterval, this,
      &WriteProtectDisablePhysicalStateHandler::CheckWriteProtectOffTask);
}

void WriteProtectDisablePhysicalStateHandler::CheckWriteProtectOffTask() {
  DCHECK(write_protect_signal_sender_);
  LOG(INFO) << "Check write protection";

  int wp_status;
  if (!crossystem_utils_->GetInt(kWriteProtectProperty, &wp_status)) {
    LOG(ERROR) << "Failed to get HWWP status";
    return;
  }
  if (wp_status == 0) {
    write_protect_signal_sender_->Run(false);
    timer_.Stop();
  }
}

}  // namespace rmad
