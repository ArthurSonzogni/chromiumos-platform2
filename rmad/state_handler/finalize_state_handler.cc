// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/finalize_state_handler.h"

#include <memory>
#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/notreached.h>
#include <base/task/sequenced_task_runner.h>

#include "rmad/constants.h"
#include "rmad/utils/cros_config_utils_impl.h"
#include "rmad/utils/gsc_utils_impl.h"
#include "rmad/utils/write_protect_utils_impl.h"

namespace {

constexpr char kEmptyBoardIdType[] = "ffffffff";
constexpr char kTestBoardIdType[] = "5a5a4352";             // ZZCR.
constexpr char kPvtBoardIdFlags[] = "00007f80";             // pvt.
constexpr char kCustomLabelPvtBoardIdFlags[] = "00003f80";  // customlabel_pvt.

}  // namespace

namespace rmad {

FinalizeStateHandler::FinalizeStateHandler(
    scoped_refptr<JsonStore> json_store,
    scoped_refptr<DaemonCallback> daemon_callback)
    : BaseStateHandler(json_store, daemon_callback),
      working_dir_path_(kDefaultWorkingDirPath) {
  cros_config_utils_ = std::make_unique<CrosConfigUtilsImpl>();
  gsc_utils_ = std::make_unique<GscUtilsImpl>();
  write_protect_utils_ = std::make_unique<WriteProtectUtilsImpl>();
}

FinalizeStateHandler::FinalizeStateHandler(
    scoped_refptr<JsonStore> json_store,
    scoped_refptr<DaemonCallback> daemon_callback,
    const base::FilePath& working_dir_path,
    std::unique_ptr<CrosConfigUtils> cros_config_utils,
    std::unique_ptr<GscUtils> gsc_utils,
    std::unique_ptr<WriteProtectUtils> write_protect_utils)
    : BaseStateHandler(json_store, daemon_callback),
      working_dir_path_(working_dir_path),
      cros_config_utils_(std::move(cros_config_utils)),
      gsc_utils_(std::move(gsc_utils)),
      write_protect_utils_(std::move(write_protect_utils)) {}

RmadErrorCode FinalizeStateHandler::InitializeState() {
  sequenced_task_runner_ = base::SequencedTaskRunner::GetCurrentDefault();

  if (!state_.has_finalize()) {
    state_.set_allocated_finalize(new FinalizeState);
    status_.set_status(FinalizeStatus::RMAD_FINALIZE_STATUS_UNKNOWN);
    status_.set_error(FinalizeStatus::RMAD_FINALIZE_ERROR_UNKNOWN);
  }

  return RMAD_ERROR_OK;
}

void FinalizeStateHandler::RunState() {
  StartStatusTimer();
  if (status_.status() == FinalizeStatus::RMAD_FINALIZE_STATUS_UNKNOWN) {
    StartFinalize();
  }
}

void FinalizeStateHandler::CleanUpState() {
  StopStatusTimer();
}

BaseStateHandler::GetNextStateCaseReply FinalizeStateHandler::GetNextStateCase(
    const RmadState& state) {
  if (!state.has_finalize()) {
    LOG(ERROR) << "RmadState missing |finalize| state.";
    return NextStateCaseWrapper(RMAD_ERROR_REQUEST_INVALID);
  }

  switch (state.finalize().choice()) {
    case FinalizeState::RMAD_FINALIZE_CHOICE_UNKNOWN:
      return NextStateCaseWrapper(RMAD_ERROR_REQUEST_ARGS_MISSING);
    case FinalizeState::RMAD_FINALIZE_CHOICE_CONTINUE:
      switch (status_.status()) {
        case FinalizeStatus::RMAD_FINALIZE_STATUS_UNKNOWN:
          [[fallthrough]];
        case FinalizeStatus::RMAD_FINALIZE_STATUS_IN_PROGRESS:
          return NextStateCaseWrapper(RMAD_ERROR_WAIT);
        case FinalizeStatus::RMAD_FINALIZE_STATUS_COMPLETE:
          [[fallthrough]];
        case FinalizeStatus::RMAD_FINALIZE_STATUS_FAILED_NON_BLOCKING:
          return NextStateCaseWrapper(RmadState::StateCase::kRepairComplete);
        case FinalizeStatus::RMAD_FINALIZE_STATUS_FAILED_BLOCKING:
          return NextStateCaseWrapper(RMAD_ERROR_FINALIZATION_FAILED);
        default:
          break;
      }
      NOTREACHED_IN_MIGRATION();
      break;
    case FinalizeState::RMAD_FINALIZE_CHOICE_RETRY:
      StartFinalize();
      return NextStateCaseWrapper(RMAD_ERROR_WAIT);
    default:
      break;
  }

  NOTREACHED_IN_MIGRATION();
  return NextStateCaseWrapper(RMAD_ERROR_TRANSITION_FAILED);
}

void FinalizeStateHandler::SendStatusSignal() {
  daemon_callback_->GetFinalizeSignalCallback().Run(status_);
}

void FinalizeStateHandler::StartStatusTimer() {
  StopStatusTimer();
  status_timer_.Start(FROM_HERE, kReportStatusInterval, this,
                      &FinalizeStateHandler::SendStatusSignal);
}

void FinalizeStateHandler::StopStatusTimer() {
  if (status_timer_.IsRunning()) {
    status_timer_.Stop();
  }
}

void FinalizeStateHandler::StartFinalize() {
  status_.set_status(FinalizeStatus::RMAD_FINALIZE_STATUS_IN_PROGRESS);
  status_.set_progress(0);
  status_.set_error(FinalizeStatus::RMAD_FINALIZE_ERROR_UNKNOWN);

  sequenced_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&FinalizeStateHandler::FinalizeTask,
                                base::Unretained(this)));
}

void FinalizeStateHandler::FinalizeTask() {
  // Enable SWWP if HWWP is still disabled.
  if (auto hwwp_enabled =
          write_protect_utils_->GetHardwareWriteProtectionStatus();
      hwwp_enabled.has_value() && !hwwp_enabled.value()) {
    if (!write_protect_utils_->EnableSoftwareWriteProtection()) {
      LOG(ERROR) << "Failed to enable software write protection";
      status_.set_status(FinalizeStatus::RMAD_FINALIZE_STATUS_FAILED_BLOCKING);
      status_.set_error(FinalizeStatus::RMAD_FINALIZE_ERROR_CANNOT_ENABLE_SWWP);
      return;
    }
  }

  status_.set_progress(0.5);

  // Disable factory mode if it's still enabled.
  if (!gsc_utils_->DisableFactoryMode()) {
    LOG(ERROR) << "Failed to disable factory mode";
    status_.set_status(FinalizeStatus::RMAD_FINALIZE_STATUS_FAILED_BLOCKING);
    status_.set_error(FinalizeStatus::RMAD_FINALIZE_ERROR_CANNOT_ENABLE_HWWP);
    return;
  }

  status_.set_progress(0.8);

  // Make sure HWWP is disabled.
  if (auto hwwp_enabled =
          write_protect_utils_->GetHardwareWriteProtectionStatus();
      !hwwp_enabled.has_value() || !hwwp_enabled.value()) {
    LOG(ERROR) << "HWWP is still disabled";
    status_.set_status(FinalizeStatus::RMAD_FINALIZE_STATUS_FAILED_BLOCKING);
    status_.set_error(FinalizeStatus::RMAD_FINALIZE_ERROR_CANNOT_ENABLE_HWWP);
    return;
  }

  status_.set_progress(0.9);

  // Reset fingerprint sensor.
  if (IsFingerprintSupported()) {
    daemon_callback_->GetExecuteResetFpmcuEntropyCallback().Run(
        base::BindOnce(&FinalizeStateHandler::ResetFpmcuEntropyCallback,
                       base::Unretained(this)));
  } else {
    OnResetFpmcuEntropySucceed();
  }
}

void FinalizeStateHandler::ResetFpmcuEntropyCallback(bool success) {
  if (!success) {
    LOG(ERROR) << "Failed to reset FPS";
    status_.set_status(FinalizeStatus::RMAD_FINALIZE_STATUS_FAILED_BLOCKING);
    status_.set_error(FinalizeStatus::RMAD_FINALIZE_ERROR_INTERNAL);
    return;
  }

  OnResetFpmcuEntropySucceed();
}

void FinalizeStateHandler::OnResetFpmcuEntropySucceed() {
  status_.set_progress(0.95);

  // Make sure GSC board ID type and board ID flags are set.
  if (auto board_id_type = gsc_utils_->GetBoardIdType();
      !board_id_type.has_value() ||
      board_id_type.value() == kEmptyBoardIdType ||
      board_id_type.value() == kTestBoardIdType) {
    LOG(ERROR) << "GSC board ID type is invalid: "
               << board_id_type.value_or("");
    if (base::PathExists(working_dir_path_.AppendASCII(kTestDirPath))) {
      DLOG(INFO) << "GSC board ID check bypassed";
    } else {
      status_.set_status(FinalizeStatus::RMAD_FINALIZE_STATUS_FAILED_BLOCKING);
      status_.set_error(FinalizeStatus::RMAD_FINALIZE_ERROR_CR50);
      return;
    }
  }
  if (auto board_id_flags = gsc_utils_->GetBoardIdFlags();
      !board_id_flags.has_value() ||
      (board_id_flags.value() != kPvtBoardIdFlags &&
       board_id_flags.value() != kCustomLabelPvtBoardIdFlags)) {
    LOG(ERROR) << "GSC board ID flags is invalid: "
               << board_id_flags.value_or("");
    if (base::PathExists(working_dir_path_.AppendASCII(kTestDirPath))) {
      DLOG(INFO) << "GSC board ID flags check bypassed";
    } else {
      status_.set_status(FinalizeStatus::RMAD_FINALIZE_STATUS_FAILED_BLOCKING);
      status_.set_error(FinalizeStatus::RMAD_FINALIZE_ERROR_CR50);
      return;
    }
  }

  // TODO(chenghan): Check GBB flags.
  status_.set_status(FinalizeStatus::RMAD_FINALIZE_STATUS_COMPLETE);
  status_.set_progress(1);
  status_.set_error(FinalizeStatus::RMAD_FINALIZE_ERROR_UNKNOWN);
}

bool FinalizeStateHandler::IsFingerprintSupported() const {
  auto location = cros_config_utils_->GetFingerprintSensorLocation();
  return location.has_value() && location.value() != "none";
}

}  // namespace rmad
