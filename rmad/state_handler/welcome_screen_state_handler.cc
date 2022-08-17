// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/welcome_screen_state_handler.h"

#include <memory>
#include <utility>

#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/notreached.h>
#include <base/threading/sequenced_task_runner_handle.h>

#include "rmad/system/fake_hardware_verifier_client.h"
#include "rmad/system/hardware_verifier_client_impl.h"
#include "rmad/utils/dbus_utils.h"

namespace rmad {

WelcomeScreenStateHandler::WelcomeScreenStateHandler(
    scoped_refptr<JsonStore> json_store,
    scoped_refptr<DaemonCallback> daemon_callback)
    : BaseStateHandler(json_store, daemon_callback) {
  hardware_verifier_client_ =
      std::make_unique<HardwareVerifierClientImpl>(GetSystemBus());
}

WelcomeScreenStateHandler::WelcomeScreenStateHandler(
    scoped_refptr<JsonStore> json_store,
    scoped_refptr<DaemonCallback> daemon_callback,
    std::unique_ptr<HardwareVerifierClient> hardware_verifier_client)
    : BaseStateHandler(json_store, daemon_callback),
      hardware_verifier_client_(std::move(hardware_verifier_client)) {}

RmadErrorCode WelcomeScreenStateHandler::InitializeState() {
  if (!state_.has_welcome()) {
    state_.set_allocated_welcome(new WelcomeState);
  }

  return RMAD_ERROR_OK;
}

void WelcomeScreenStateHandler::OnGetStateTask() const {
  base::SequencedTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::BindOnce(&WelcomeScreenStateHandler::RunHardwareVerifier,
                                base::Unretained(this)));
}

BaseStateHandler::GetNextStateCaseReply
WelcomeScreenStateHandler::GetNextStateCase(const RmadState& state) {
  if (!state.has_welcome()) {
    LOG(ERROR) << "RmadState missing |welcome| state.";
    return NextStateCaseWrapper(RMAD_ERROR_REQUEST_INVALID);
  }

  switch (state.welcome().choice()) {
    case WelcomeState::RMAD_CHOICE_UNKNOWN:
      return NextStateCaseWrapper(RMAD_ERROR_REQUEST_ARGS_MISSING);
    case WelcomeState::RMAD_CHOICE_FINALIZE_REPAIR:
      return NextStateCaseWrapper(RmadState::StateCase::kComponentsRepair);
    default:
      break;
  }
  NOTREACHED();
  return NextStateCaseWrapper(RmadState::StateCase::STATE_NOT_SET,
                              RMAD_ERROR_NOT_SET,
                              RMAD_ADDITIONAL_ACTIVITY_NOTHING);
}

void WelcomeScreenStateHandler::RunHardwareVerifier() const {
  HardwareVerificationResult result;
  if (hardware_verifier_client_->GetHardwareVerificationResult(&result)) {
    daemon_callback_->GetHardwareVerificationSignalCallback().Run(result);
  } else {
    LOG(ERROR) << "Failed to get hardware verification result";
  }
}

}  // namespace rmad
