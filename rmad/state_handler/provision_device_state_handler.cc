// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/provision_device_state_handler.h"

#include <algorithm>

#include <base/bind.h>
#include <base/logging.h>
#include <base/notreached.h>
#include <base/synchronization/lock.h>

namespace rmad {

ProvisionDeviceStateHandler::ProvisionDeviceStateHandler(
    scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {}

RmadErrorCode ProvisionDeviceStateHandler::InitializeState() {
  if (!state_.has_provision_device()) {
    state_.set_allocated_provision_device(new ProvisionDeviceState);
    status_.set_status(ProvisionStatus::RMAD_PROVISION_STATUS_UNKNOWN);
    status_.set_progress(0);
  }
  if (!provision_signal_sender_) {
    return RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED;
  }

  StartStatusTimer();
  if (status_.status() == ProvisionStatus::RMAD_PROVISION_STATUS_UNKNOWN) {
    StartProvision();
  }
  return RMAD_ERROR_OK;
}

void ProvisionDeviceStateHandler::CleanUpState() {
  StopStatusTimer();
}

BaseStateHandler::GetNextStateCaseReply
ProvisionDeviceStateHandler::GetNextStateCase(const RmadState& state) {
  if (!state.has_provision_device()) {
    LOG(ERROR) << "RmadState missing |provision| state.";
    return {.error = RMAD_ERROR_REQUEST_INVALID, .state_case = GetStateCase()};
  }

  switch (state.provision_device().choice()) {
    case ProvisionDeviceState::RMAD_PROVISION_CHOICE_UNKNOWN:
      return {.error = RMAD_ERROR_REQUEST_ARGS_MISSING,
              .state_case = GetStateCase()};
    case ProvisionDeviceState::RMAD_PROVISION_CHOICE_CONTINUE:
      switch (status_.status()) {
        case ProvisionStatus::RMAD_PROVISION_STATUS_IN_PROGRESS:
          return {.error = RMAD_ERROR_WAIT, .state_case = GetStateCase()};
        case ProvisionStatus::RMAD_PROVISION_STATUS_COMPLETE:
          FALLTHROUGH;
        case ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_NON_BLOCKING:
          // TODO(chenghan): If device is still open, we should go to
          //                 kWpEnablePhysical state.
          return {.error = RMAD_ERROR_OK,
                  .state_case = RmadState::StateCase::kFinalize};
        case ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_BLOCKING:
          return {.error = RMAD_ERROR_PROVISIONING_FAILED,
                  .state_case = GetStateCase()};
        default:
          break;
      }
      break;
    case ProvisionDeviceState::RMAD_PROVISION_CHOICE_RETRY:
      StartProvision();
      return {.error = RMAD_ERROR_WAIT, .state_case = GetStateCase()};
    default:
      break;
  }

  NOTREACHED();
  return {.error = RMAD_ERROR_TRANSITION_FAILED, .state_case = GetStateCase()};
}

void ProvisionDeviceStateHandler::SendStatusSignal() {
  provision_signal_sender_->Run(status_);
}

void ProvisionDeviceStateHandler::StartStatusTimer() {
  StopStatusTimer();
  status_timer_.Start(FROM_HERE, kReportStatusInterval, this,
                      &ProvisionDeviceStateHandler::SendStatusSignal);
}

void ProvisionDeviceStateHandler::StopStatusTimer() {
  if (status_timer_.IsRunning()) {
    status_timer_.Stop();
  }
}

void ProvisionDeviceStateHandler::StartProvision() {
  UpdateProgress(true);

  // Mock provision progress.
  if (provision_timer_.IsRunning()) {
    provision_timer_.Stop();
  }
  provision_timer_.Start(
      FROM_HERE, kUpdateProgressInterval,
      base::BindRepeating(&ProvisionDeviceStateHandler::UpdateProgress,
                          base::Unretained(this), false));
}

void ProvisionDeviceStateHandler::UpdateProgress(bool restart) {
  base::AutoLock scoped_lock(lock_);
  if (restart) {
    status_.set_status(ProvisionStatus::RMAD_PROVISION_STATUS_IN_PROGRESS);
    status_.set_progress(0);
    return;
  }

  // This is currently fake.
  if (status_.status() != ProvisionStatus::RMAD_PROVISION_STATUS_IN_PROGRESS) {
    return;
  }
  status_.set_progress(status_.progress() + 0.3);
  if (status_.progress() >= 1) {
    status_.set_status(ProvisionStatus::RMAD_PROVISION_STATUS_COMPLETE);
    status_.set_progress(1);
    provision_timer_.Stop();
  }
}

}  // namespace rmad
