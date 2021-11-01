// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/welcome_screen_state_handler.h"

#include <memory>
#include <utility>

#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/notreached.h>
#include <base/task/task_traits.h>
#include <base/task/thread_pool.h>

#include "rmad/utils/fake_hardware_verifier_utils.h"
#include "rmad/utils/hardware_verifier_utils_impl.h"

namespace rmad {

namespace fake {

FakeWelcomeScreenStateHandler::FakeWelcomeScreenStateHandler(
    scoped_refptr<JsonStore> json_store, const base::FilePath& working_dir_path)
    : WelcomeScreenStateHandler(
          json_store,
          std::make_unique<fake::FakeHardwareVerifierUtils>(working_dir_path)) {
}

}  // namespace fake

WelcomeScreenStateHandler::WelcomeScreenStateHandler(
    scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {
  hardware_verifier_utils_ = std::make_unique<HardwareVerifierUtilsImpl>();
}

WelcomeScreenStateHandler::WelcomeScreenStateHandler(
    scoped_refptr<JsonStore> json_store,
    std::unique_ptr<HardwareVerifierUtils> hardware_verifier_utils)
    : BaseStateHandler(json_store),
      hardware_verifier_utils_(std::move(hardware_verifier_utils)) {}

RmadErrorCode WelcomeScreenStateHandler::InitializeState() {
  if (!task_runner_) {
    task_runner_ = base::ThreadPool::CreateSequencedTaskRunner(
        {base::TaskPriority::BEST_EFFORT, base::MayBlock()});
  }
  if (!state_.has_welcome()) {
    state_.set_allocated_welcome(new WelcomeState);
    task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(&WelcomeScreenStateHandler::RunHardwareVerifier,
                       base::Unretained(this)));
  }

  return RMAD_ERROR_OK;
}

BaseStateHandler::GetNextStateCaseReply
WelcomeScreenStateHandler::GetNextStateCase(const RmadState& state) {
  if (!state.has_welcome()) {
    LOG(ERROR) << "RmadState missing |welcome| state.";
    return {.error = RMAD_ERROR_REQUEST_INVALID, .state_case = GetStateCase()};
  }

  switch (state.welcome().choice()) {
    case WelcomeState::RMAD_CHOICE_UNKNOWN:
      return {.error = RMAD_ERROR_REQUEST_ARGS_MISSING,
              .state_case = GetStateCase()};
    case WelcomeState::RMAD_CHOICE_FINALIZE_REPAIR:
      return {.error = RMAD_ERROR_OK,
              .state_case = RmadState::StateCase::kComponentsRepair};
    default:
      break;
  }
  NOTREACHED();
  return {.error = RMAD_ERROR_NOT_SET,
          .state_case = RmadState::StateCase::STATE_NOT_SET};
}

void WelcomeScreenStateHandler::RunHardwareVerifier() const {
  HardwareVerificationResult result;
  if (hardware_verifier_utils_->GetHardwareVerificationResult(&result)) {
    hardware_verification_result_signal_sender_->Run(result);
  } else {
    LOG(ERROR) << "Failed to get hardware verification result";
  }
}

}  // namespace rmad
