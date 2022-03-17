// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_STATE_HANDLER_PROVISION_DEVICE_STATE_HANDLER_H_
#define RMAD_STATE_HANDLER_PROVISION_DEVICE_STATE_HANDLER_H_

#include "rmad/state_handler/base_state_handler.h"

#include <memory>
#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <base/memory/scoped_refptr.h>
#include <base/synchronization/lock.h>
#include <base/timer/timer.h>

#include "rmad/system/power_manager_client.h"
#include "rmad/utils/cbi_utils.h"
#include "rmad/utils/cros_config_utils.h"
#include "rmad/utils/json_store.h"
#include "rmad/utils/ssfc_utils.h"
#include "rmad/utils/vpd_utils.h"

namespace rmad {

class ProvisionDeviceStateHandler : public BaseStateHandler {
 public:
  // Report status every second.
  static constexpr base::TimeDelta kReportStatusInterval = base::Seconds(1);

  // Wait for 5 seconds before rebooting.
  static constexpr base::TimeDelta kRebootDelay = base::Seconds(5);

  explicit ProvisionDeviceStateHandler(scoped_refptr<JsonStore> json_store);
  // Used to inject mock |cbi_utils_|, |cros_config_utils_|,
  // |power_manager_client_|, |ssfc_utils_|, and |vpd_utils_| for testing.
  ProvisionDeviceStateHandler(
      scoped_refptr<JsonStore> json_store,
      std::unique_ptr<PowerManagerClient> power_manager_client,
      std::unique_ptr<CbiUtils> cbi_utils,
      std::unique_ptr<CrosConfigUtils> cros_config_utils,
      std::unique_ptr<SsfcUtils> ssfc_utils,
      std::unique_ptr<VpdUtils> vpd_utils);

  ASSIGN_STATE(RmadState::StateCase::kProvisionDevice);
  SET_REPEATABLE;

  void RegisterSignalSender(ProvisionSignalCallback callback) override {
    provision_signal_sender_ = callback;
  }

  RmadErrorCode InitializeState() override;
  void CleanUpState() override;
  GetNextStateCaseReply GetNextStateCase(const RmadState& state) override;
  GetNextStateCaseReply TryGetNextStateCaseAtBoot() override;

  scoped_refptr<base::SequencedTaskRunner> GetTaskRunner() {
    return task_runner_;
  }

 protected:
  ~ProvisionDeviceStateHandler() override = default;

 private:
  void SendStatusSignal();
  void StartStatusTimer();
  void StopStatusTimer();

  void StartProvision();
  void RunProvision();
  void UpdateProgress(double progress, ProvisionStatus::Status status);
  ProvisionStatus GetProgress() const;

  bool GenerateStableDeviceSecret(std::string* stable_device_secret);
  void Reboot();

  ProvisionStatus status_;
  ProvisionSignalCallback provision_signal_sender_;
  std::unique_ptr<PowerManagerClient> power_manager_client_;
  std::unique_ptr<CbiUtils> cbi_utils_;
  std::unique_ptr<CrosConfigUtils> cros_config_utils_;
  std::unique_ptr<SsfcUtils> ssfc_utils_;
  std::unique_ptr<VpdUtils> vpd_utils_;
  scoped_refptr<base::SequencedTaskRunner> task_runner_;
  base::RepeatingTimer status_timer_;
  base::OneShotTimer reboot_timer_;
  mutable base::Lock lock_;
};

namespace fake {

class FakeProvisionDeviceStateHandler : public ProvisionDeviceStateHandler {
 public:
  FakeProvisionDeviceStateHandler(scoped_refptr<JsonStore> json_store,
                                  const base::FilePath& working_dir_path);

 protected:
  ~FakeProvisionDeviceStateHandler() override = default;
};

}  // namespace fake

}  // namespace rmad

#endif  // RMAD_STATE_HANDLER_PROVISION_DEVICE_STATE_HANDLER_H_
