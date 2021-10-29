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
#include "rmad/utils/fake_vpd_utils.h"
#include "rmad/utils/json_store.h"
#include "rmad/utils/vpd_utils_impl.h"

namespace {

constexpr int kStableDeviceSecretSize = 32;

constexpr double kProgressComplete = 1.0;
constexpr double kProgressFailedNonblocking = -1.0;
constexpr double kProgressFailedBlocking = -2.0;
constexpr double kProgressInit = 0.0;
constexpr double kProgressGetDestination = 0.3;
constexpr double kProgressGenerateStableDeviceSecret = 0.5;
constexpr double kProgressWriteStableDeviceSecret = 0.7;
constexpr double kProgressFlushOutVpdCache = kProgressComplete;

}  // namespace

namespace rmad {

namespace fake {

FakeProvisionDeviceStateHandler::FakeProvisionDeviceStateHandler(
    scoped_refptr<JsonStore> json_store, const base::FilePath& working_dir_path)
    : ProvisionDeviceStateHandler(
          json_store, std::make_unique<fake::FakeVpdUtils>(working_dir_path)) {}

}  // namespace fake

ProvisionDeviceStateHandler::ProvisionDeviceStateHandler(
    scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {
  vpd_utils_ = std::make_unique<VpdUtilsImpl>();
}

ProvisionDeviceStateHandler::ProvisionDeviceStateHandler(
    scoped_refptr<JsonStore> json_store, std::unique_ptr<VpdUtils> vpd_utils)
    : BaseStateHandler(json_store), vpd_utils_(std::move(vpd_utils)) {}

RmadErrorCode ProvisionDeviceStateHandler::InitializeState() {
  if (!task_runner_) {
    task_runner_ = base::ThreadPool::CreateSequencedTaskRunner(
        {base::TaskPriority::BEST_EFFORT, base::MayBlock()});
  }

  if (!state_.has_provision_device()) {
    state_.set_allocated_provision_device(new ProvisionDeviceState);
    UpdateProgress(kProgressInit,
                   ProvisionStatus::RMAD_PROVISION_STATUS_UNKNOWN);
  }
  if (!provision_signal_sender_) {
    return RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED;
  }

  if (GetProgress().status() ==
      ProvisionStatus::RMAD_PROVISION_STATUS_UNKNOWN) {
    StartProvision();
  }
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
          if (bool keep_device_open;
              json_store_->GetValue(kKeepDeviceOpen, &keep_device_open) &&
              keep_device_open) {
            return NextStateCaseWrapper(
                RmadState::StateCase::kWpEnablePhysical);
          } else {
            return NextStateCaseWrapper(RmadState::StateCase::kFinalize);
          }
        case ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_BLOCKING:
          return NextStateCaseWrapper(RMAD_ERROR_PROVISIONING_FAILED);
        default:
          break;
      }
      break;
    case ProvisionDeviceState::RMAD_PROVISION_CHOICE_RETRY:
      StartProvision();
      return NextStateCaseWrapper(RMAD_ERROR_WAIT);
    default:
      break;
  }

  NOTREACHED();
  return NextStateCaseWrapper(RMAD_ERROR_TRANSITION_FAILED);
}

void ProvisionDeviceStateHandler::SendStatusSignal() {
  const ProvisionStatus& status = GetProgress();
  provision_signal_sender_->Run(status);
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
  StartStatusTimer();
}

void ProvisionDeviceStateHandler::RunProvision() {
  bool same_owner = false;
  if (!json_store_->GetValue(kSameOwner, &same_owner)) {
    LOG(ERROR) << "Failed to get device destination from json store";
    UpdateProgress(kProgressFailedBlocking,
                   ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_BLOCKING);
    return;
  }
  UpdateProgress(kProgressGetDestination,
                 ProvisionStatus::RMAD_PROVISION_STATUS_IN_PROGRESS);

  if (!same_owner) {
    std::string stable_device_secret;
    if (!GenerateStableDeviceSecret(&stable_device_secret)) {
      UpdateProgress(
          kProgressFailedNonblocking,
          ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_NON_BLOCKING);
      return;
    }
    UpdateProgress(kProgressGenerateStableDeviceSecret,
                   ProvisionStatus::RMAD_PROVISION_STATUS_IN_PROGRESS);

    if (!vpd_utils_->SetStableDeviceSecret(stable_device_secret)) {
      UpdateProgress(
          kProgressFailedNonblocking,
          ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_NON_BLOCKING);
      return;
    }
    UpdateProgress(kProgressWriteStableDeviceSecret,
                   ProvisionStatus::RMAD_PROVISION_STATUS_IN_PROGRESS);
    // TODO(genechang): Reset fingerprint sensor here."
  }

  // TODO(genechang): Write SSFC and FW_CONFIG here.

  if (!vpd_utils_->FlushOutRoVpdCache()) {
    UpdateProgress(kProgressFailedNonblocking,
                   ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_NON_BLOCKING);
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

}  // namespace rmad
