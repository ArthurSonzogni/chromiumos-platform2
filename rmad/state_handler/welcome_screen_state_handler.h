// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_STATE_HANDLER_WELCOME_SCREEN_STATE_HANDLER_H_
#define RMAD_STATE_HANDLER_WELCOME_SCREEN_STATE_HANDLER_H_

#include "rmad/state_handler/base_state_handler.h"

#include <memory>
#include <utility>

#include <base/files/file_path.h>
#include <base/sequenced_task_runner.h>

#include "rmad/utils/hardware_verifier_utils.h"

namespace rmad {

class WelcomeScreenStateHandler : public BaseStateHandler {
 public:
  explicit WelcomeScreenStateHandler(scoped_refptr<JsonStore> json_store);
  // Used to inject mock |hardware_verifier_utils_| for testing.
  WelcomeScreenStateHandler(
      scoped_refptr<JsonStore> json_store,
      std::unique_ptr<HardwareVerifierUtils> hardware_verifier_utils);

  ASSIGN_STATE(RmadState::StateCase::kWelcome);
  SET_REPEATABLE;

  void RegisterSignalSender(
      std::unique_ptr<HardwareVerificationResultSignalCallback> callback)
      override {
    hardware_verification_result_signal_sender_ = std::move(callback);
  }

  RmadErrorCode InitializeState() override;
  GetNextStateCaseReply GetNextStateCase(const RmadState& state) override;

  scoped_refptr<base::SequencedTaskRunner> GetTaskRunner() {
    return task_runner_;
  }

  void RunHardwareVerifier() const;

 protected:
  ~WelcomeScreenStateHandler() override = default;

 private:
  std::unique_ptr<HardwareVerifierUtils> hardware_verifier_utils_;
  std::unique_ptr<HardwareVerificationResultSignalCallback>
      hardware_verification_result_signal_sender_;
  scoped_refptr<base::SequencedTaskRunner> task_runner_;
};

namespace fake {

class FakeWelcomeScreenStateHandler : public WelcomeScreenStateHandler {
 public:
  FakeWelcomeScreenStateHandler(scoped_refptr<JsonStore> json_store,
                                const base::FilePath& working_dir_path);

 protected:
  ~FakeWelcomeScreenStateHandler() override = default;
};

}  // namespace fake

}  // namespace rmad

#endif  // RMAD_STATE_HANDLER_WELCOME_SCREEN_STATE_HANDLER_H_
