// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_STATE_HANDLER_WRITE_PROTECT_DISABLE_RSU_STATE_HANDLER_H_
#define RMAD_STATE_HANDLER_WRITE_PROTECT_DISABLE_RSU_STATE_HANDLER_H_

#include "rmad/state_handler/base_state_handler.h"

#include <memory>

#include <base/files/file_path.h>
#include <base/timer/timer.h>

#include "rmad/system/power_manager_client.h"
#include "rmad/utils/cr50_utils.h"
#include "rmad/utils/crossystem_utils.h"
#include "rmad/utils/dbus_utils.h"

namespace rmad {

class Cr50Utils;
class CrosSystemUtils;
class PowerManagerClient;

class WriteProtectDisableRsuStateHandler : public BaseStateHandler {
 public:
  // Wait for 1 second before rebooting.
  static constexpr base::TimeDelta kRebootDelay = base::Seconds(1);

  explicit WriteProtectDisableRsuStateHandler(
      scoped_refptr<JsonStore> json_store,
      scoped_refptr<DaemonCallback> daemon_callback);
  // Used to inject mock |cr50_utils_|, |crossystem_utils_| and
  // |power_manager_client_| for testing.
  WriteProtectDisableRsuStateHandler(
      scoped_refptr<JsonStore> json_store,
      scoped_refptr<DaemonCallback> daemon_callback,
      const base::FilePath& working_dir_path,
      std::unique_ptr<Cr50Utils> cr50_utils,
      std::unique_ptr<CrosSystemUtils> crossystem_utils,
      std::unique_ptr<PowerManagerClient> power_manager_client);

  ASSIGN_STATE(RmadState::StateCase::kWpDisableRsu);
  SET_REPEATABLE;

  RmadErrorCode InitializeState() override;
  GetNextStateCaseReply GetNextStateCase(const RmadState& state) override;

  // Try to auto-transition at boot.
  GetNextStateCaseReply TryGetNextStateCaseAtBoot() override {
    return GetNextStateCase(state_);
  }

  // Override powerwash function. Allow disabling powerwash if running in a
  // debug build.
  bool CanDisablePowerwash() const override {
    int cros_debug;
    return crossystem_utils_->GetCrosDebug(&cros_debug) && cros_debug == 1;
  }

 protected:
  ~WriteProtectDisableRsuStateHandler() override = default;

 private:
  bool IsFactoryModeEnabled() const;
  void Reboot();

  base::FilePath working_dir_path_;

  std::unique_ptr<Cr50Utils> cr50_utils_;
  std::unique_ptr<CrosSystemUtils> crossystem_utils_;
  std::unique_ptr<PowerManagerClient> power_manager_client_;

  bool reboot_scheduled_;
  base::OneShotTimer timer_;
};

namespace fake {

class FakeWriteProtectDisableRsuStateHandler
    : public WriteProtectDisableRsuStateHandler {
 public:
  FakeWriteProtectDisableRsuStateHandler(
      scoped_refptr<JsonStore> json_store,
      scoped_refptr<DaemonCallback> daemon_callback,
      const base::FilePath& working_dir_path);

 protected:
  ~FakeWriteProtectDisableRsuStateHandler() override = default;
};

}  // namespace fake

}  // namespace rmad

#endif  // RMAD_STATE_HANDLER_WRITE_PROTECT_DISABLE_RSU_STATE_HANDLER_H_
