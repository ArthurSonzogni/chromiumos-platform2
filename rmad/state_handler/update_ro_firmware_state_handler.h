// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_STATE_HANDLER_UPDATE_RO_FIRMWARE_STATE_HANDLER_H_
#define RMAD_STATE_HANDLER_UPDATE_RO_FIRMWARE_STATE_HANDLER_H_

#include "rmad/state_handler/base_state_handler.h"

#include <memory>
#include <string>
#include <utility>

#include <base/memory/scoped_refptr.h>
#include <base/sequence_checker.h>
#include <base/task_runner.h>
#include <base/timer/timer.h>

#include "rmad/proto_bindings/rmad.pb.h"
#include "rmad/system/cros_disks_client.h"
#include "rmad/system/power_manager_client.h"
#include "rmad/utils/cmd_utils.h"
#include "rmad/utils/crossystem_utils.h"
#include "rmad/utils/flashrom_utils.h"

namespace rmad {

class UpdateRoFirmwareStateHandler : public BaseStateHandler {
 public:
  static constexpr base::TimeDelta kPollInterval =
      base::TimeDelta::FromSeconds(1);
  static constexpr base::TimeDelta kTaskInterval =
      base::TimeDelta::FromSeconds(2);
  static constexpr base::TimeDelta kRebootDelay =
      base::TimeDelta::FromSeconds(5);

  explicit UpdateRoFirmwareStateHandler(scoped_refptr<JsonStore> json_store);
  // Used to inject mock |cmd_utils_|, |crossystem_utils|, |flashrom_utils|,
  // |cros_disks_client_| and |power_manager_client_| for testing.
  UpdateRoFirmwareStateHandler(
      scoped_refptr<JsonStore> json_store,
      std::unique_ptr<CmdUtils> cmd_utils,
      std::unique_ptr<CrosSystemUtils> crossystem_utils,
      std::unique_ptr<FlashromUtils> flashrom_utils,
      std::unique_ptr<CrosDisksClient> cros_disks_client,
      std::unique_ptr<PowerManagerClient> power_manager_client);

  ASSIGN_STATE(RmadState::StateCase::kUpdateRoFirmware);
  SET_REPEATABLE;

  void RegisterSignalSender(
      std::unique_ptr<UpdateRoFirmwareStatusSignalCallback> callback) override {
    update_ro_firmware_status_signal_sender_ = std::move(callback);
  }

  RmadErrorCode InitializeState() override;
  void CleanUpState() override;
  GetNextStateCaseReply GetNextStateCase(const RmadState& state) override;

 protected:
  ~UpdateRoFirmwareStateHandler() override = default;

 private:
  bool CanSkipUpdate();

  void StartTimers();
  void StopTimers();
  void SendFirmwareUpdateStatusSignal();
  void WaitUsb();
  void OnMountCompleted(const rmad::MountEntry& entry);
  bool RunFirmwareUpdater(const std::string& firmware_updater_path);
  void UpdateFirmware(const std::string& mount_path,
                      const std::string& firmware_updater_path);
  void Unmount(const std::string& mount_path);
  void OnUpdateFinished(bool update_success);

  // Functions for rebooting.
  void PostRebootTask();
  void Reboot();

  // True if the class is not initialized with default constructor.
  bool is_mocked_;

  std::unique_ptr<CmdUtils> cmd_utils_;
  std::unique_ptr<CrosSystemUtils> crossystem_utils_;
  std::unique_ptr<FlashromUtils> flashrom_utils_;
  std::unique_ptr<CrosDisksClient> cros_disks_client_;
  std::unique_ptr<PowerManagerClient> power_manager_client_;
  std::unique_ptr<UpdateRoFirmwareStatusSignalCallback>
      update_ro_firmware_status_signal_sender_;

  // All accesses to |active_|, |status_| and |poll_usb_| should be on the
  // same sequence.
  SEQUENCE_CHECKER(sequence_checker_);
  bool active_;
  UpdateRoFirmwareStatus status_;
  bool poll_usb_;

  // Timer for sending status signals.
  base::RepeatingTimer status_signal_timer_;
  // Timer for checking USB.
  base::RepeatingTimer check_usb_timer_;
  // Sequence runner for thread-safe read/write of |active_|, |status_| and
  // |poll_usb_|.
  scoped_refptr<base::SequencedTaskRunner> sequenced_task_runner_;
  // Task runner for firmware updater.
  scoped_refptr<base::TaskRunner> updater_task_runner_;
};

namespace fake {

// This fake state handler always says that firmware update is done. This is
// quite different from the normal one, so we write it from scratch.
class FakeUpdateRoFirmwareStateHandler : public BaseStateHandler {
 public:
  static constexpr base::TimeDelta kPollInterval =
      base::TimeDelta::FromSeconds(1);

  explicit FakeUpdateRoFirmwareStateHandler(
      scoped_refptr<JsonStore> json_store);

  ASSIGN_STATE(RmadState::StateCase::kUpdateRoFirmware);
  SET_REPEATABLE;

  void RegisterSignalSender(
      std::unique_ptr<UpdateRoFirmwareStatusSignalCallback> callback) override {
    update_ro_firmware_status_signal_sender_ = std::move(callback);
  }

  RmadErrorCode InitializeState() override;
  void CleanUpState() override;
  GetNextStateCaseReply GetNextStateCase(const RmadState& state) override;

 protected:
  ~FakeUpdateRoFirmwareStateHandler() override = default;

 private:
  void SendFirmwareUpdateStatusSignal();

  std::unique_ptr<UpdateRoFirmwareStatusSignalCallback>
      update_ro_firmware_status_signal_sender_;
  // Timer for sending status signals.
  base::RepeatingTimer status_signal_timer_;
};

}  // namespace fake

}  // namespace rmad

#endif  // RMAD_STATE_HANDLER_UPDATE_RO_FIRMWARE_STATE_HANDLER_H_
