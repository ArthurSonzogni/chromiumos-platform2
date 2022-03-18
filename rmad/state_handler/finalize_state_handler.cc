// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/finalize_state_handler.h"

#include <algorithm>

#include <base/bind.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/notreached.h>
#include <base/synchronization/lock.h>
#include <base/task/task_traits.h>
#include <base/task/thread_pool.h>

#include "rmad/utils/cr50_utils_impl.h"
#include "rmad/utils/fake_cr50_utils.h"
#include "rmad/utils/fake_flashrom_utils.h"
#include "rmad/utils/flashrom_utils_impl.h"

namespace rmad {

namespace fake {

FakeFinalizeStateHandler::FakeFinalizeStateHandler(
    scoped_refptr<JsonStore> json_store, const base::FilePath& working_dir_path)
    : FinalizeStateHandler(json_store,
                           std::make_unique<FakeCr50Utils>(working_dir_path),
                           std::make_unique<FakeFlashromUtils>()) {}

}  // namespace fake

FinalizeStateHandler::FinalizeStateHandler(scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store), finalize_signal_sender_(base::DoNothing()) {
  cr50_utils_ = std::make_unique<Cr50UtilsImpl>();
  flashrom_utils_ = std::make_unique<FlashromUtilsImpl>();
}

FinalizeStateHandler::FinalizeStateHandler(
    scoped_refptr<JsonStore> json_store,
    std::unique_ptr<Cr50Utils> cr50_utils,
    std::unique_ptr<FlashromUtils> flashrom_utils)
    : BaseStateHandler(json_store),
      finalize_signal_sender_(base::DoNothing()),
      cr50_utils_(std::move(cr50_utils)),
      flashrom_utils_(std::move(flashrom_utils)) {}

RmadErrorCode FinalizeStateHandler::InitializeState() {
  if (!state_.has_finalize()) {
    state_.set_allocated_finalize(new FinalizeState);
    status_.set_status(FinalizeStatus::RMAD_FINALIZE_STATUS_UNKNOWN);
    status_.set_error(FinalizeStatus::RMAD_FINALIZE_ERROR_UNKNOWN);
  }
  if (!task_runner_) {
    task_runner_ = base::ThreadPool::CreateSequencedTaskRunner(
        {base::TaskPriority::BEST_EFFORT, base::MayBlock()});
  }

  StartTasks();
  return RMAD_ERROR_OK;
}

void FinalizeStateHandler::StartTasks() {
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
        case FinalizeStatus::RMAD_FINALIZE_STATUS_IN_PROGRESS:
          return NextStateCaseWrapper(RMAD_ERROR_WAIT);
        case FinalizeStatus::RMAD_FINALIZE_STATUS_COMPLETE:
          FALLTHROUGH;
        case FinalizeStatus::RMAD_FINALIZE_STATUS_FAILED_NON_BLOCKING:
          return NextStateCaseWrapper(RmadState::StateCase::kRepairComplete);
        case FinalizeStatus::RMAD_FINALIZE_STATUS_FAILED_BLOCKING:
          return NextStateCaseWrapper(RMAD_ERROR_FINALIZATION_FAILED);
        default:
          break;
      }
      NOTREACHED();
      break;
    case FinalizeState::RMAD_FINALIZE_CHOICE_RETRY:
      StartFinalize();
      return NextStateCaseWrapper(RMAD_ERROR_WAIT);
    default:
      break;
  }

  NOTREACHED();
  return NextStateCaseWrapper(RMAD_ERROR_TRANSITION_FAILED);
}

void FinalizeStateHandler::SendStatusSignal() {
  finalize_signal_sender_.Run(status_);
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
  task_runner_->PostTask(FROM_HERE,
                         base::BindOnce(&FinalizeStateHandler::FinalizeTask,
                                        base::Unretained(this)));
}

void FinalizeStateHandler::FinalizeTask() {
  if (!cr50_utils_->DisableFactoryMode()) {
    LOG(ERROR) << "Failed to disable factory mode";
    status_.set_status(FinalizeStatus::RMAD_FINALIZE_STATUS_FAILED_BLOCKING);
    status_.set_error(FinalizeStatus::RMAD_FINALIZE_ERROR_CANNOT_ENABLE_HWWP);
    return;
  }
  status_.set_progress(0.5);
  if (!flashrom_utils_->EnableSoftwareWriteProtection()) {
    LOG(ERROR) << "Failed to enable software write protection";
    status_.set_status(FinalizeStatus::RMAD_FINALIZE_STATUS_FAILED_BLOCKING);
    status_.set_error(FinalizeStatus::RMAD_FINALIZE_ERROR_CANNOT_ENABLE_SWWP);
    return;
  }
  // TODO(chenghan): Check cr50 data (e.g. board ID) and GBB flags.
  status_.set_status(FinalizeStatus::RMAD_FINALIZE_STATUS_COMPLETE);
  status_.set_progress(1);
  status_.set_error(FinalizeStatus::RMAD_FINALIZE_ERROR_UNKNOWN);
}

}  // namespace rmad
