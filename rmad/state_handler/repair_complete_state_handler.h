// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_STATE_HANDLER_REPAIR_COMPLETE_STATE_HANDLER_H_
#define RMAD_STATE_HANDLER_REPAIR_COMPLETE_STATE_HANDLER_H_

#include "rmad/state_handler/base_state_handler.h"

#include <memory>
#include <utility>

#include <base/files/file_path.h>
#include <base/timer/timer.h>

#include "rmad/metrics/metrics_utils.h"
#include "rmad/system/power_manager_client.h"
#include "rmad/utils/sys_utils.h"

namespace rmad {

class RepairCompleteStateHandler : public BaseStateHandler {
 public:
  // Wait for 1 second before reboot/shutdown/cutoff.
  static constexpr base::TimeDelta kShutdownDelay = base::Seconds(1);
  // Report power cable state every second.
  static constexpr base::TimeDelta kReportPowerCableInterval = base::Seconds(1);

  explicit RepairCompleteStateHandler(scoped_refptr<JsonStore> json_store);
  // Used to inject |working_dir_path_| and |unencrypted_preserve_path|, and
  // mocked |power_manager_client_|, |sys_utils_| and |metrics_utils_| for
  // testing.
  RepairCompleteStateHandler(
      scoped_refptr<JsonStore> json_store,
      const base::FilePath& working_dir_path,
      const base::FilePath& unencrypted_preserve_path,
      std::unique_ptr<PowerManagerClient> power_manager_client,
      std::unique_ptr<SysUtils> sys_utils,
      std::unique_ptr<MetricsUtils> metrics_utils);

  ASSIGN_STATE(RmadState::StateCase::kRepairComplete);
  SET_UNREPEATABLE;

  void RegisterSignalSender(
      base::RepeatingCallback<void(bool)> callback) override {
    power_cable_signal_sender_ = callback;
  }

  RmadErrorCode InitializeState() override;
  void RunState() override;
  void CleanUpState() override;
  GetNextStateCaseReply GetNextStateCase(const RmadState& state) override;

  // Try to auto-transition at boot.
  GetNextStateCaseReply TryGetNextStateCaseAtBoot() override {
    return GetNextStateCase(state_);
  }

 protected:
  ~RepairCompleteStateHandler() override = default;

 private:
  void Reboot();
  void Shutdown();
  void Cutoff();
  void SendPowerCableStateSignal();
  bool IsPowerwashComplete() const;

  base::FilePath working_dir_path_;
  base::FilePath unencrypted_preserve_path_;

  base::RepeatingTimer power_cable_timer_;
  base::RepeatingCallback<void(bool)> power_cable_signal_sender_;

  std::unique_ptr<PowerManagerClient> power_manager_client_;
  std::unique_ptr<SysUtils> sys_utils_;
  std::unique_ptr<MetricsUtils> metrics_utils_;

  // If |locked_error_| is set, always return it in |GetNextStateCase|.
  RmadErrorCode locked_error_;

  base::OneShotTimer action_timer_;
};

namespace fake {

class FakeRepairCompleteStateHandler : public RepairCompleteStateHandler {
 public:
  FakeRepairCompleteStateHandler(scoped_refptr<JsonStore> json_store,
                                 const base::FilePath& working_dir_path);

 protected:
  ~FakeRepairCompleteStateHandler() override = default;
};

}  // namespace fake

}  // namespace rmad

#endif  // RMAD_STATE_HANDLER_REPAIR_COMPLETE_STATE_HANDLER_H_
