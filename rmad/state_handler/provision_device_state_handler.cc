// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/provision_device_state_handler.h"

#include <openssl/rand.h>

#include <algorithm>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>

#include <base/bind.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/memory/scoped_refptr.h>
#include <base/notreached.h>
#include <base/synchronization/lock.h>
#include <base/task/task_traits.h>
#include <base/task/thread_pool.h>
#include <base/strings/string_number_conversions.h>

#include "rmad/constants.h"
#include "rmad/system/fake_power_manager_client.h"
#include "rmad/system/power_manager_client_impl.h"
#include "rmad/utils/cbi_utils_impl.h"
#include "rmad/utils/cros_config_utils_impl.h"
#include "rmad/utils/dbus_utils.h"
#include "rmad/utils/fake_cbi_utils.h"
#include "rmad/utils/fake_cros_config_utils.h"
#include "rmad/utils/fake_ssfc_utils.h"
#include "rmad/utils/fake_vpd_utils.h"
#include "rmad/utils/json_store.h"
#include "rmad/utils/ssfc_utils_impl.h"
#include "rmad/utils/vpd_utils_impl.h"

namespace {

constexpr int kStableDeviceSecretSize = 32;

constexpr double kProgressComplete = 1.0;
constexpr double kProgressFailedNonblocking = -1.0;
constexpr double kProgressFailedBlocking = -2.0;
constexpr double kProgressInit = 0.0;
constexpr double kProgressGetDestination = 0.3;
constexpr double kProgressGetModelName = 0.5;
constexpr double kProgressGetSSFC = 0.7;
constexpr double kProgressWriteSSFC = 0.8;
constexpr double kProgressUpdateStableDeviceSecret = 0.9;
constexpr double kProgressFlushOutVpdCache = kProgressComplete;

}  // namespace

namespace rmad {

namespace fake {

FakeProvisionDeviceStateHandler::FakeProvisionDeviceStateHandler(
    scoped_refptr<JsonStore> json_store, const base::FilePath& working_dir_path)
    : ProvisionDeviceStateHandler(
          json_store,
          std::make_unique<fake::FakePowerManagerClient>(working_dir_path),
          std::make_unique<fake::FakeCbiUtils>(working_dir_path),
          std::make_unique<fake::FakeCrosConfigUtils>(),
          std::make_unique<fake::FakeSsfcUtils>(),
          std::make_unique<fake::FakeVpdUtils>(working_dir_path)) {}

}  // namespace fake

ProvisionDeviceStateHandler::ProvisionDeviceStateHandler(
    scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store),
      provision_signal_sender_(base::DoNothing()) {
  power_manager_client_ =
      std::make_unique<PowerManagerClientImpl>(GetSystemBus());
  cbi_utils_ = std::make_unique<CbiUtilsImpl>();
  cros_config_utils_ = std::make_unique<CrosConfigUtilsImpl>();
  ssfc_utils_ = std::make_unique<SsfcUtilsImpl>();
  vpd_utils_ = std::make_unique<VpdUtilsImpl>();
  status_.set_status(ProvisionStatus::RMAD_PROVISION_STATUS_UNKNOWN);
  status_.set_progress(kProgressInit);
}

ProvisionDeviceStateHandler::ProvisionDeviceStateHandler(
    scoped_refptr<JsonStore> json_store,
    std::unique_ptr<PowerManagerClient> power_manager_client,
    std::unique_ptr<CbiUtils> cbi_utils,
    std::unique_ptr<CrosConfigUtils> cros_config_utils,
    std::unique_ptr<SsfcUtils> ssfc_utils,
    std::unique_ptr<VpdUtils> vpd_utils)
    : BaseStateHandler(json_store),
      provision_signal_sender_(base::DoNothing()),
      power_manager_client_(std::move(power_manager_client)),
      cbi_utils_(std::move(cbi_utils)),
      cros_config_utils_(std::move(cros_config_utils)),
      ssfc_utils_(std::move(ssfc_utils)),
      vpd_utils_(std::move(vpd_utils)) {
  status_.set_status(ProvisionStatus::RMAD_PROVISION_STATUS_UNKNOWN);
  status_.set_progress(kProgressInit);
}

RmadErrorCode ProvisionDeviceStateHandler::InitializeState() {
  if (!state_.has_provision_device() && !RetrieveState()) {
    state_.set_allocated_provision_device(new ProvisionDeviceState);
  }

  if (!task_runner_) {
    task_runner_ = base::ThreadPool::CreateSequencedTaskRunner(
        {base::TaskPriority::BEST_EFFORT, base::MayBlock()});
  }

  // If status_name is set in |json_store_|, it means it has been provisioned.
  // We should restore the status and let users decide.
  ProvisionStatus::Status provision_status = GetProgress().status();
  if (std::string status_name;
      json_store_->GetValue(kProvisionFinishedStatus, &status_name) &&
      ProvisionStatus::Status_Parse(status_name, &provision_status)) {
    UpdateProgress(kProgressInit, provision_status);
  }

  if (provision_status == ProvisionStatus::RMAD_PROVISION_STATUS_UNKNOWN) {
    StartProvision();
  }

  // We always send status signals when we initialize the handler to notify the
  // Chrome side.
  StartStatusTimer();
  return RMAD_ERROR_OK;
}

void ProvisionDeviceStateHandler::CleanUpState() {
  StopStatusTimer();
}

BaseStateHandler::GetNextStateCaseReply
ProvisionDeviceStateHandler::GetNextStateCase(const RmadState& state) {
  if (!state.has_provision_device()) {
    LOG(ERROR) << "RmadState missing |provision| state.";
    return NextStateCaseWrapper(RMAD_ERROR_REQUEST_INVALID);
  }

  state_ = state;
  StoreState();
  const ProvisionStatus& status = GetProgress();
  switch (state.provision_device().choice()) {
    case ProvisionDeviceState::RMAD_PROVISION_CHOICE_UNKNOWN:
      return NextStateCaseWrapper(RMAD_ERROR_REQUEST_ARGS_MISSING);
    case ProvisionDeviceState::RMAD_PROVISION_CHOICE_CONTINUE:
      switch (status.status()) {
        case ProvisionStatus::RMAD_PROVISION_STATUS_IN_PROGRESS:
          return NextStateCaseWrapper(RMAD_ERROR_WAIT);
        case ProvisionStatus::RMAD_PROVISION_STATUS_COMPLETE:
          FALLTHROUGH;
        case ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_NON_BLOCKING:
          json_store_->SetValue(kProvisionFinishedStatus,
                                ProvisionStatus::Status_Name(status.status()));
          // Schedule a reboot after |kRebootDelay| seconds and return.
          reboot_timer_.Start(FROM_HERE, kRebootDelay, this,
                              &ProvisionDeviceStateHandler::Reboot);
          return NextStateCaseWrapper(GetStateCase(), RMAD_ERROR_EXPECT_REBOOT,
                                      AdditionalActivity::REBOOT);
        case ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_BLOCKING:
          return NextStateCaseWrapper(RMAD_ERROR_PROVISIONING_FAILED);
        default:
          break;
      }
      break;
    case ProvisionDeviceState::RMAD_PROVISION_CHOICE_RETRY:
      StartProvision();
      StartStatusTimer();
      return NextStateCaseWrapper(RMAD_ERROR_WAIT);
    default:
      break;
  }

  NOTREACHED();
  return NextStateCaseWrapper(RMAD_ERROR_TRANSITION_FAILED);
}

BaseStateHandler::GetNextStateCaseReply
ProvisionDeviceStateHandler::TryGetNextStateCaseAtBoot() {
  // If the status is already complete or non-blocking at startup, we should go
  // to the next state. Otherwise, don't transition.
  switch (GetProgress().status()) {
    case ProvisionStatus::RMAD_PROVISION_STATUS_COMPLETE:
      FALLTHROUGH;
    case ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_NON_BLOCKING:
      return NextStateCaseWrapper(RmadState::StateCase::kSetupCalibration);
    default:
      break;
  }

  return NextStateCaseWrapper(RMAD_ERROR_TRANSITION_FAILED);
}

void ProvisionDeviceStateHandler::SendStatusSignal() {
  const ProvisionStatus& status = GetProgress();
  provision_signal_sender_.Run(status);
  if (status.status() != ProvisionStatus::RMAD_PROVISION_STATUS_IN_PROGRESS) {
    StopStatusTimer();
  }
}

void ProvisionDeviceStateHandler::StartStatusTimer() {
  StopStatusTimer();
  status_timer_.Start(FROM_HERE, kReportStatusInterval, this,
                      &ProvisionDeviceStateHandler::SendStatusSignal);
}

void ProvisionDeviceStateHandler::StopStatusTimer() {
  if (status_timer_.IsRunning()) {
    status_timer_.Stop();
  }
}

void ProvisionDeviceStateHandler::StartProvision() {
  UpdateProgress(kProgressInit,
                 ProvisionStatus::RMAD_PROVISION_STATUS_IN_PROGRESS);
  task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&ProvisionDeviceStateHandler::RunProvision,
                                base::Unretained(this)));
}

void ProvisionDeviceStateHandler::RunProvision() {
  // We should do all blocking items first, and then do non-blocking items.
  // In this case, once it fails, we can directly update the status to
  // FAILED_BLOCKING or FAILED_NON_BLOCKING based on the failed item.

  bool same_owner = false;
  if (!json_store_->GetValue(kSameOwner, &same_owner)) {
    LOG(ERROR) << "Failed to get device destination from json store";
    UpdateProgress(kProgressFailedBlocking,
                   ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_BLOCKING);
    return;
  }
  UpdateProgress(kProgressGetDestination,
                 ProvisionStatus::RMAD_PROVISION_STATUS_IN_PROGRESS);

  std::string model_name;
  if (!cros_config_utils_->GetModelName(&model_name)) {
    LOG(ERROR) << "Failed to get model name from cros_config.";
    UpdateProgress(kProgressFailedBlocking,
                   ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_BLOCKING);
    return;
  }
  UpdateProgress(kProgressGetModelName,
                 ProvisionStatus::RMAD_PROVISION_STATUS_IN_PROGRESS);

  bool need_to_update_ssfc = false;
  uint32_t ssfc;
  if (!ssfc_utils_->GetSSFC(model_name, &need_to_update_ssfc, &ssfc)) {
    UpdateProgress(kProgressFailedBlocking,
                   ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_BLOCKING);
    return;
  }
  UpdateProgress(kProgressGetSSFC,
                 ProvisionStatus::RMAD_PROVISION_STATUS_IN_PROGRESS);

  if (need_to_update_ssfc && !cbi_utils_->SetSSFC(ssfc)) {
    UpdateProgress(kProgressFailedBlocking,
                   ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_BLOCKING);
    return;
  }
  UpdateProgress(kProgressWriteSSFC,
                 ProvisionStatus::RMAD_PROVISION_STATUS_IN_PROGRESS);

  // All blocking items should be finished before here.
  if (!same_owner) {
    std::string stable_device_secret;
    if (!GenerateStableDeviceSecret(&stable_device_secret) ||
        !vpd_utils_->SetStableDeviceSecret(stable_device_secret)) {
      UpdateProgress(kProgressFailedNonblocking,
                     ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_BLOCKING);
      return;
    }
    UpdateProgress(kProgressUpdateStableDeviceSecret,
                   ProvisionStatus::RMAD_PROVISION_STATUS_IN_PROGRESS);
    // TODO(genechang): Reset fingerprint sensor here."
  }

  if (!vpd_utils_->FlushOutRoVpdCache()) {
    UpdateProgress(kProgressFailedNonblocking,
                   ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_BLOCKING);
    return;
  }
  UpdateProgress(kProgressFlushOutVpdCache,
                 ProvisionStatus::RMAD_PROVISION_STATUS_COMPLETE);
}

void ProvisionDeviceStateHandler::UpdateProgress(
    double progress, ProvisionStatus::Status status) {
  base::AutoLock scoped_lock(lock_);
  status_.set_progress(progress);
  status_.set_status(status);
}

ProvisionStatus ProvisionDeviceStateHandler::GetProgress() const {
  base::AutoLock scoped_lock(lock_);
  return status_;
}

bool ProvisionDeviceStateHandler::GenerateStableDeviceSecret(
    std::string* stable_device_secret) {
  CHECK(stable_device_secret);
  unsigned char buffer[kStableDeviceSecretSize];
  if (RAND_bytes(buffer, kStableDeviceSecretSize) != 1) {
    LOG(ERROR) << "Failed to get random bytes.";
    return false;
  }

  *stable_device_secret = base::HexEncode(buffer, kStableDeviceSecretSize);
  return true;
}

void ProvisionDeviceStateHandler::Reboot() {
  LOG(INFO) << "Rebooting after updating configs.";
  if (!power_manager_client_->Restart()) {
    LOG(ERROR) << "Failed to reboot";
  }
}

}  // namespace rmad
