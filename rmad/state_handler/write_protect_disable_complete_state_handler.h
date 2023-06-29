// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_STATE_HANDLER_WRITE_PROTECT_DISABLE_COMPLETE_STATE_HANDLER_H_
#define RMAD_STATE_HANDLER_WRITE_PROTECT_DISABLE_COMPLETE_STATE_HANDLER_H_

#include "rmad/state_handler/base_state_handler.h"

#include <memory>

#include <base/timer/timer.h>

#include "rmad/utils/gsc_utils.h"
#include "rmad/utils/write_protect_utils.h"

namespace rmad {

class WriteProtectDisableCompleteStateHandler : public BaseStateHandler {
 public:
  // Wait for 3 seconds before rebooting.
  static constexpr base::TimeDelta kRebootDelay = base::Seconds(3);

  explicit WriteProtectDisableCompleteStateHandler(
      scoped_refptr<JsonStore> json_store,
      scoped_refptr<DaemonCallback> daemon_callback);
  // Used to inject mock |gsc_utils_| and |write_protect_utils_| for testing.
  explicit WriteProtectDisableCompleteStateHandler(
      scoped_refptr<JsonStore> json_store,
      scoped_refptr<DaemonCallback> daemon_callback,
      std::unique_ptr<GscUtils> gsc_utils,
      std::unique_ptr<WriteProtectUtils> write_protect_utils);

  ASSIGN_STATE(RmadState::StateCase::kWpDisableComplete);
  SET_UNREPEATABLE;

  RmadErrorCode InitializeState() override;
  GetNextStateCaseReply GetNextStateCase(const RmadState& state) override;

  // Try to auto-transition at boot.
  GetNextStateCaseReply TryGetNextStateCaseAtBoot() override;

 protected:
  ~WriteProtectDisableCompleteStateHandler() override = default;

 private:
  void RequestGscReboot();
  bool IsGscRebooted() const;

  std::unique_ptr<GscUtils> gsc_utils_;
  std::unique_ptr<WriteProtectUtils> write_protect_utils_;

  bool reboot_scheduled_;
  base::OneShotTimer timer_;
};

}  // namespace rmad

#endif  // RMAD_STATE_HANDLER_WRITE_PROTECT_DISABLE_COMPLETE_STATE_HANDLER_H_
