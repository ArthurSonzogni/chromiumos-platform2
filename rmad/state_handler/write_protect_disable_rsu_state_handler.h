// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_STATE_HANDLER_WRITE_PROTECT_DISABLE_RSU_STATE_HANDLER_H_
#define RMAD_STATE_HANDLER_WRITE_PROTECT_DISABLE_RSU_STATE_HANDLER_H_

#include "rmad/state_handler/base_state_handler.h"

#include <memory>

#include <base/timer/timer.h>

#include "rmad/utils/crossystem_utils.h"
#include "rmad/utils/gsc_utils.h"
#include "rmad/utils/write_protect_utils.h"

namespace rmad {

class WriteProtectDisableRsuStateHandler : public BaseStateHandler {
 public:
  // Wait for 3 seconds between RSU and rebooting.
  // Enabling factory mode can take up to 2 seconds. Wait for at least 3 seconds
  // to be safe.
  static constexpr base::TimeDelta kRebootDelay = base::Seconds(3);

  explicit WriteProtectDisableRsuStateHandler(
      scoped_refptr<JsonStore> json_store,
      scoped_refptr<DaemonCallback> daemon_callback);
  // Used to inject mock |gsc_utils_|, |crossystem_utils_|, and
  // |write_protect_utils_| for testing.
  explicit WriteProtectDisableRsuStateHandler(
      scoped_refptr<JsonStore> json_store,
      scoped_refptr<DaemonCallback> daemon_callback,
      std::unique_ptr<GscUtils> gsc_utils,
      std::unique_ptr<CrosSystemUtils> crossystem_utils,
      std::unique_ptr<WriteProtectUtils> write_protect_utils);

  ASSIGN_STATE(RmadState::StateCase::kWpDisableRsu);
  SET_REPEATABLE;

  RmadErrorCode InitializeState() override;
  GetNextStateCaseReply GetNextStateCase(const RmadState& state) override;

  // Try to auto-transition at boot.
  GetNextStateCaseReply TryGetNextStateCaseAtBoot() override;

 protected:
  ~WriteProtectDisableRsuStateHandler() override = default;

 private:
  bool IsFactoryModeEnabled() const;
  void RequestRmaPowerwashAndRebootEc();
  void RequestRmaPowerwashAndRebootEcCallback(bool success);
  void RebootEc();
  void RebootEcCallback(bool success);

  std::unique_ptr<GscUtils> gsc_utils_;
  std::unique_ptr<CrosSystemUtils> crossystem_utils_;
  std::unique_ptr<WriteProtectUtils> write_protect_utils_;

  bool reboot_scheduled_;
  base::OneShotTimer timer_;
};

}  // namespace rmad

#endif  // RMAD_STATE_HANDLER_WRITE_PROTECT_DISABLE_RSU_STATE_HANDLER_H_
