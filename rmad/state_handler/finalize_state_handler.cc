// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/finalize_state_handler.h"

#include <algorithm>

#include <base/bind.h>
#include <base/logging.h>
#include <base/notreached.h>
#include <base/synchronization/lock.h>

namespace rmad {

FinalizeStateHandler::FinalizeStateHandler(scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {}

RmadErrorCode FinalizeStateHandler::InitializeState() {
  if (!state_.has_finalize()) {
    state_.set_allocated_finalize(new FinalizeState);
    status_.set_status(FinalizeStatus::RMAD_FINALIZE_STATUS_UNKNOWN);
    status_.set_progress(0);
  }
  if (!finalize_signal_sender_) {
    return RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED;
  }

  StartStatusTimer();
  if (status_.status() == FinalizeStatus::RMAD_FINALIZE_STATUS_UNKNOWN) {
    StartFinalize();
  }
  return RMAD_ERROR_OK;
}

void FinalizeStateHandler::CleanUpState() {
  StopStatusTimer();
}

BaseStateHandler::GetNextStateCaseReply FinalizeStateHandler::GetNextStateCase(
    const RmadState& state) {
  if (!state.has_finalize()) {
    LOG(ERROR) << "RmadState missing |finalize| state.";
    return {.error = RMAD_ERROR_REQUEST_INVALID, .state_case = GetStateCase()};
  }

  switch (state.finalize().choice()) {
    case FinalizeState::RMAD_FINALIZE_CHOICE_UNKNOWN:
      return {.error = RMAD_ERROR_REQUEST_ARGS_MISSING,
              .state_case = GetStateCase()};
    case FinalizeState::RMAD_FINALIZE_CHOICE_CONTINUE:
      switch (status_.status()) {
        case FinalizeStatus::RMAD_FINALIZE_STATUS_IN_PROGRESS:
          return {.error = RMAD_ERROR_WAIT, .state_case = GetStateCase()};
        case FinalizeStatus::RMAD_FINALIZE_STATUS_COMPLETE:
          FALLTHROUGH;
        case FinalizeStatus::RMAD_FINALIZE_STATUS_FAILED_NON_BLOCKING:
          return {.error = RMAD_ERROR_OK,
                  .state_case = RmadState::StateCase::kRepairComplete};
        case FinalizeStatus::RMAD_FINALIZE_STATUS_FAILED_BLOCKING:
          return {.error = RMAD_ERROR_FINALIZATION_FAILED,
                  .state_case = GetStateCase()};
        default:
          break;
      }
      break;
    case FinalizeState::RMAD_FINALIZE_CHOICE_RETRY:
      StartFinalize();
      return {.error = RMAD_ERROR_WAIT, .state_case = GetStateCase()};
    default:
      break;
  }

  NOTREACHED();
  return {.error = RMAD_ERROR_TRANSITION_FAILED, .state_case = GetStateCase()};
}

void FinalizeStateHandler::SendStatusSignal() {
  finalize_signal_sender_->Run(status_);
}

void FinalizeStateHandler::StartStatusTimer() {
  StopStatusTimer();
  status_timer_.Start(FROM_HERE, kReportStatusInterval, this,
                      &FinalizeStateHandler::SendStatusSignal);
}

void FinalizeStateHandler::StopStatusTimer() {
  if (status_timer_.IsRunning()) {
    status_timer_.Stop();
  }
}

void FinalizeStateHandler::StartFinalize() {
  UpdateProgress(true);

  // Mock finalize progress.
  if (finalize_timer_.IsRunning()) {
    finalize_timer_.Stop();
  }
  finalize_timer_.Start(
      FROM_HERE, kUpdateProgressInterval,
      base::BindRepeating(&FinalizeStateHandler::UpdateProgress,
                          base::Unretained(this), false));
}

void FinalizeStateHandler::UpdateProgress(bool restart) {
  base::AutoLock scoped_lock(lock_);
  if (restart) {
    status_.set_status(FinalizeStatus::RMAD_FINALIZE_STATUS_IN_PROGRESS);
    status_.set_progress(0);
    return;
  }

  // This is currently fake.
  if (status_.status() != FinalizeStatus::RMAD_FINALIZE_STATUS_IN_PROGRESS) {
    return;
  }
  status_.set_progress(status_.progress() + 0.3);
  if (status_.progress() >= 1) {
    status_.set_status(FinalizeStatus::RMAD_FINALIZE_STATUS_COMPLETE);
    status_.set_progress(1);
    finalize_timer_.Stop();
  }
}

}  // namespace rmad
