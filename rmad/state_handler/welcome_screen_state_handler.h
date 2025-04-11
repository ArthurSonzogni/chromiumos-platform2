// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_STATE_HANDLER_WELCOME_SCREEN_STATE_HANDLER_H_
#define RMAD_STATE_HANDLER_WELCOME_SCREEN_STATE_HANDLER_H_

#include <memory>
#include <utility>

#include <base/files/file_path.h>

#include "rmad/state_handler/base_state_handler.h"
#include "rmad/system/hardware_verifier_client.h"
#include "rmad/utils/rmad_config_utils.h"
#include "rmad/utils/vpd_utils.h"

namespace rmad {

class WelcomeScreenStateHandler : public BaseStateHandler {
 public:
  explicit WelcomeScreenStateHandler(
      scoped_refptr<JsonStore> json_store,
      scoped_refptr<DaemonCallback> daemon_callback);
  // Used to inject |working_dir_path|, |hardware_verifier_client_|,
  // |vpd_utils_|, and |rmad_config_utils_| for testing.
  explicit WelcomeScreenStateHandler(
      scoped_refptr<JsonStore> json_store,
      scoped_refptr<DaemonCallback> daemon_callback,
      const base::FilePath& working_dir_path,
      std::unique_ptr<HardwareVerifierClient> hardware_verifier_client,
      std::unique_ptr<VpdUtils> vpd_utils,
      std::unique_ptr<RmadConfigUtils> rmad_config_utils);

  ASSIGN_STATE(RmadState::StateCase::kWelcome);
  SET_REPEATABLE;

  RmadErrorCode InitializeState() override;
  GetNextStateCaseReply GetNextStateCase(const RmadState& state) override;

  void RunHardwareVerifier() const;

 protected:
  ~WelcomeScreenStateHandler() override = default;

  void OnGetStateTask() const override;

 private:
  bool ShouldSkipHardwareVerification() const;

  base::FilePath working_dir_path_;
  // Helper utilities.
  std::unique_ptr<HardwareVerifierClient> hardware_verifier_client_;
  std::unique_ptr<VpdUtils> vpd_utils_;
  std::unique_ptr<RmadConfigUtils> rmad_config_utils_;
};

}  // namespace rmad

#endif  // RMAD_STATE_HANDLER_WELCOME_SCREEN_STATE_HANDLER_H_
