// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/update_ro_firmware_state_handler.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/notreached.h>
#include <base/sequence_checker.h>
#include <base/task/task_traits.h>
#include <base/task/thread_pool.h>
#include <base/threading/sequenced_task_runner_handle.h>
#include <brillo/file_utils.h>
#include <re2/re2.h>

#include "rmad/constants.h"
#include "rmad/system/cros_disks_client_impl.h"
#include "rmad/system/power_manager_client_impl.h"
#include "rmad/utils/cmd_utils_impl.h"
#include "rmad/utils/crossystem_utils_impl.h"
#include "rmad/utils/dbus_utils.h"
#include "rmad/utils/flashrom_utils_impl.h"

namespace {

constexpr char kFirmwareUpdaterFilePath[] = "usr/sbin/chromeos-firmwareupdate";

bool IsRootfsPartition(const std::string& path) {
  return re2::RE2::FullMatch(path, R"(/dev/sd[a-z]3)");
}

}  // namespace

namespace rmad {

UpdateRoFirmwareStateHandler::UpdateRoFirmwareStateHandler(
    scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store), active_(false) {
  DETACH_FROM_SEQUENCE(sequence_checker_);
  cmd_utils_ = std::make_unique<CmdUtilsImpl>();
  crossystem_utils_ = std::make_unique<CrosSystemUtilsImpl>();
  flashrom_utils_ = std::make_unique<FlashromUtilsImpl>();
  cros_disks_client_ = std::make_unique<CrosDisksClientImpl>(GetSystemBus());
  power_manager_client_ =
      std::make_unique<PowerManagerClientImpl>(GetSystemBus());
}

UpdateRoFirmwareStateHandler::UpdateRoFirmwareStateHandler(
    scoped_refptr<JsonStore> json_store,
    std::unique_ptr<CmdUtils> cmd_utils,
    std::unique_ptr<CrosSystemUtils> crossystem_utils,
    std::unique_ptr<FlashromUtils> flashrom_utils,
    std::unique_ptr<CrosDisksClient> cros_disks_client,
    std::unique_ptr<PowerManagerClient> power_manager_client)
    : BaseStateHandler(json_store),
      cmd_utils_(std::move(cmd_utils)),
      crossystem_utils_(std::move(crossystem_utils)),
      flashrom_utils_(std::move(flashrom_utils)),
      cros_disks_client_(std::move(cros_disks_client)),
      power_manager_client_(std::move(power_manager_client)),
      active_(false) {
  DETACH_FROM_SEQUENCE(sequence_checker_);
}

RmadErrorCode UpdateRoFirmwareStateHandler::InitializeState() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!state_.has_update_ro_firmware()) {
    auto update_ro_firmware = std::make_unique<UpdateRoFirmwareState>();
    update_ro_firmware->set_optional(CanSkipUpdate());
    state_.set_allocated_update_ro_firmware(update_ro_firmware.release());

    sequenced_task_runner_ = base::SequencedTaskRunnerHandle::Get();
    updater_task_runner_ = base::ThreadPool::CreateTaskRunner(
        {base::TaskPriority::BEST_EFFORT, base::MayBlock()});

    cros_disks_client_->AddMountCompletedHandler(
        base::BindRepeating(&UpdateRoFirmwareStateHandler::OnMountCompleted,
                            base::Unretained(this)));
  }

  if (bool firmware_updated;
      json_store_->GetValue(kFirmwareUpdated, &firmware_updated) &&
      firmware_updated) {
    status_ = RMAD_UPDATE_RO_FIRMWARE_COMPLETE;
    poll_usb_ = false;
  } else {
    status_ = RMAD_UPDATE_RO_FIRMWARE_WAIT_USB;
    poll_usb_ = true;
  }
  active_ = true;
  StartTimers();
  return RMAD_ERROR_OK;
}

void UpdateRoFirmwareStateHandler::CleanUpState() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  active_ = false;
  StopTimers();
}

BaseStateHandler::GetNextStateCaseReply
UpdateRoFirmwareStateHandler::GetNextStateCase(const RmadState& state) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!state.has_update_ro_firmware()) {
    LOG(ERROR) << "RmadState missing |update RO firmware| state.";
    return NextStateCaseWrapper(RMAD_ERROR_REQUEST_INVALID);
  }
  const UpdateRoFirmwareState& update_ro_firmware = state.update_ro_firmware();
  if (update_ro_firmware.choice() ==
      UpdateRoFirmwareState::RMAD_UPDATE_CHOICE_UNKNOWN) {
    LOG(ERROR) << "RmadState missing |udpate| argument.";
    return NextStateCaseWrapper(RMAD_ERROR_REQUEST_ARGS_MISSING);
  }
  if (!state_.update_ro_firmware().optional() &&
      update_ro_firmware.choice() ==
          UpdateRoFirmwareState::RMAD_UPDATE_CHOICE_SKIP) {
    LOG(ERROR) << "RO firmware update is mandatory.";
    return NextStateCaseWrapper(RMAD_ERROR_REQUEST_ARGS_VIOLATION);
  }

  switch (state.update_ro_firmware().choice()) {
    case UpdateRoFirmwareState::RMAD_UPDATE_CHOICE_CONTINUE:
      if (status_ == RMAD_UPDATE_RO_FIRMWARE_COMPLETE) {
        return NextStateCaseWrapper(RmadState::StateCase::kUpdateDeviceInfo);
      } else {
        return NextStateCaseWrapper(RMAD_ERROR_WAIT);
      }
    case UpdateRoFirmwareState::RMAD_UPDATE_CHOICE_SKIP:
      return NextStateCaseWrapper(RmadState::StateCase::kUpdateDeviceInfo);
    default:
      break;
  }
  NOTREACHED();
  return NextStateCaseWrapper(RmadState::StateCase::STATE_NOT_SET,
                              RMAD_ERROR_NOT_SET, AdditionalActivity::NOTHING);
}

bool UpdateRoFirmwareStateHandler::CanSkipUpdate() {
  if (bool firmware_updated;
      json_store_->GetValue(kFirmwareUpdated, &firmware_updated) &&
      firmware_updated) {
    return true;
  }
  if (bool ro_verified;
      json_store_->GetValue(kRoFirmwareVerified, &ro_verified) && ro_verified) {
    return true;
  }
  return false;
}

void UpdateRoFirmwareStateHandler::StartTimers() {
  status_signal_timer_.Start(
      FROM_HERE, kPollInterval, this,
      &UpdateRoFirmwareStateHandler::SendFirmwareUpdateStatusSignal);
  check_usb_timer_.Start(FROM_HERE, kTaskInterval, this,
                         &UpdateRoFirmwareStateHandler::WaitUsb);
}

void UpdateRoFirmwareStateHandler::StopTimers() {
  if (status_signal_timer_.IsRunning()) {
    status_signal_timer_.Stop();
  }
  if (check_usb_timer_.IsRunning()) {
    check_usb_timer_.Stop();
  }
}

void UpdateRoFirmwareStateHandler::SendFirmwareUpdateStatusSignal() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  update_ro_firmware_status_signal_sender_->Run(status_);
}

void UpdateRoFirmwareStateHandler::WaitUsb() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!poll_usb_) {
    return;
  }
  bool found_root_partition = false;
  if (std::vector<std::string> result;
      cros_disks_client_->EnumerateDevices(&result) && result.size()) {
    for (const std::string& device : result) {
      if (DeviceProperties device_properties;
          cros_disks_client_->GetDeviceProperties(device, &device_properties)) {
        if (IsRootfsPartition(device_properties.device_file)) {
          // Only try to mount the first root partition found.
          found_root_partition = true;
          poll_usb_ = false;
          cros_disks_client_->Mount(device_properties.device_file, "ext2",
                                    {"ro"});
          break;
        }
      }
    }
    if (!found_root_partition) {
      // USB is inserted but no root partition found. Treat as file not found.
      status_ = RMAD_UPDATE_RO_FIRMWARE_FILE_NOT_FOUND;
    }
  } else {
    // No detected USB.
    status_ = RMAD_UPDATE_RO_FIRMWARE_WAIT_USB;
  }
}

void UpdateRoFirmwareStateHandler::OnMountCompleted(
    const rmad::MountEntry& entry) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // This callback is active even when we're not in the state, so we need this
  // check to prevent doing a firmware update in other states.
  // TODO(chenghan): Figure out how to detach the signal handler.
  if (!active_) {
    return;
  }
  if (entry.success) {
    if (IsRootfsPartition(entry.source)) {
      const base::FilePath firmware_updater_path =
          base::FilePath(entry.mount_path)
              .AppendASCII(kFirmwareUpdaterFilePath);
      if (base::PathExists(firmware_updater_path)) {
        status_ = RMAD_UPDATE_RO_FIRMWARE_UPDATING;
        updater_task_runner_->PostTask(
            FROM_HERE,
            base::BindOnce(&UpdateRoFirmwareStateHandler::RunFirmwareUpdater,
                           base::Unretained(this), entry.mount_path,
                           firmware_updater_path.MaybeAsASCII()));
        // Unmount is done after running the firmware updater.
        // TODO(chenghan): Copy the firmware updater so we can unmount here.
        return;
      }
    }
    Unmount(entry.mount_path);
  }

  LOG(WARNING) << "Cannot find firmware updater";
  status_ = RMAD_UPDATE_RO_FIRMWARE_FILE_NOT_FOUND;
  poll_usb_ = true;
}

void UpdateRoFirmwareStateHandler::RunFirmwareUpdater(
    const std::string& mount_path, const std::string& firmware_updater_path) {
  // TODO(chenghan): Run firmware updater here. This is currently fake and
  //                 always assume the update succeeds.
  bool update_success = true;
  sequenced_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&UpdateRoFirmwareStateHandler::Unmount,
                                base::Unretained(this), mount_path));
  sequenced_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&UpdateRoFirmwareStateHandler::OnUpdateFinished,
                                base::Unretained(this), update_success));
}

void UpdateRoFirmwareStateHandler::Unmount(const std::string& mount_path) {
  if (uint32_t result;
      cros_disks_client_->Unmount(mount_path, {}, &result) && result == 0) {
    VLOG(1) << "Unmount success " << result;
  } else {
    LOG(ERROR) << "Unmount failed";
  }
}

void UpdateRoFirmwareStateHandler::OnUpdateFinished(bool update_success) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (update_success) {
    json_store_->SetValue(kFirmwareUpdated, true);
    status_ = RMAD_UPDATE_RO_FIRMWARE_REBOOTING;
    PostRebootTask();
  } else {
    status_ = RMAD_UPDATE_RO_FIRMWARE_WAIT_USB;
    // TODO(chenghan): Emit update failed signal.
    poll_usb_ = true;
  }
}

void UpdateRoFirmwareStateHandler::PostRebootTask() {
  sequenced_task_runner_->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&UpdateRoFirmwareStateHandler::Reboot,
                     base::Unretained(this)),
      kRebootDelay);
}

void UpdateRoFirmwareStateHandler::Reboot() {
  if (!power_manager_client_->Restart()) {
    LOG(ERROR) << "Failed to reboot";
  }
}

}  // namespace rmad
