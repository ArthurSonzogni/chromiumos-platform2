// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/write_protect_enable_physical_state_handler.h"

#include <memory>
#include <utility>

#include <base/files/file_path.h>

#include "rmad/utils/crossystem_utils_impl.h"
#include "rmad/utils/fake_crossystem_utils.h"
#include "rmad/utils/fake_flashrom_utils.h"
#include "rmad/utils/flashrom_utils_impl.h"

#include <base/logging.h>

namespace rmad {

namespace fake {

FakeWriteProtectEnablePhysicalStateHandler::
    FakeWriteProtectEnablePhysicalStateHandler(
        scoped_refptr<JsonStore> json_store,
        scoped_refptr<DaemonCallback> daemon_callback,
        const base::FilePath& working_dir_path)
    : WriteProtectEnablePhysicalStateHandler(
          json_store,
          daemon_callback,
          std::make_unique<FakeCrosSystemUtils>(working_dir_path),
          std::make_unique<FakeFlashromUtils>()) {}

}  // namespace fake

WriteProtectEnablePhysicalStateHandler::WriteProtectEnablePhysicalStateHandler(
    scoped_refptr<JsonStore> json_store,
    scoped_refptr<DaemonCallback> daemon_callback)
    : BaseStateHandler(json_store, daemon_callback),
      write_protect_signal_sender_(base::DoNothing()) {
  crossystem_utils_ = std::make_unique<CrosSystemUtilsImpl>();
  flashrom_utils_ = std::make_unique<FlashromUtilsImpl>();
}

WriteProtectEnablePhysicalStateHandler::WriteProtectEnablePhysicalStateHandler(
    scoped_refptr<JsonStore> json_store,
    scoped_refptr<DaemonCallback> daemon_callback,
    std::unique_ptr<CrosSystemUtils> crossystem_utils,
    std::unique_ptr<FlashromUtils> flashrom_utils)
    : BaseStateHandler(json_store, daemon_callback),
      write_protect_signal_sender_(base::DoNothing()),
      crossystem_utils_(std::move(crossystem_utils)),
      flashrom_utils_(std::move(flashrom_utils)) {}

RmadErrorCode WriteProtectEnablePhysicalStateHandler::InitializeState() {
  if (!state_.has_wp_enable_physical() && !RetrieveState()) {
    state_.set_allocated_wp_enable_physical(
        new WriteProtectEnablePhysicalState);
    // Enable SWWP when entering the state for the first time.
    if (!flashrom_utils_->EnableSoftwareWriteProtection()) {
      LOG(ERROR) << "Failed to enable software write protection";
      return RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED;
    }
    StoreState();
  }

  return RMAD_ERROR_OK;
}

void WriteProtectEnablePhysicalStateHandler::RunState() {
  LOG(INFO) << "Start polling write protection";
  if (timer_.IsRunning()) {
    timer_.Stop();
  }
  timer_.Start(
      FROM_HERE, kPollInterval, this,
      &WriteProtectEnablePhysicalStateHandler::CheckWriteProtectOnTask);
}

void WriteProtectEnablePhysicalStateHandler::CleanUpState() {
  // Stop the polling loop.
  if (timer_.IsRunning()) {
    timer_.Stop();
  }
}

BaseStateHandler::GetNextStateCaseReply
WriteProtectEnablePhysicalStateHandler::GetNextStateCase(
    const RmadState& state) {
  if (!state.has_wp_enable_physical()) {
    LOG(ERROR) << "RmadState missing |write protection enable| state.";
    return NextStateCaseWrapper(RMAD_ERROR_REQUEST_INVALID);
  }

  int hwwp_status;
  if (crossystem_utils_->GetHwwpStatus(&hwwp_status) && hwwp_status == 1) {
    return NextStateCaseWrapper(RmadState::StateCase::kFinalize);
  }
  return NextStateCaseWrapper(RMAD_ERROR_WAIT);
}

void WriteProtectEnablePhysicalStateHandler::CheckWriteProtectOnTask() {
  VLOG(1) << "Check write protection";

  int hwwp_status;
  if (!crossystem_utils_->GetHwwpStatus(&hwwp_status)) {
    LOG(ERROR) << "Failed to get HWWP status";
    return;
  }
  if (hwwp_status == 1) {
    write_protect_signal_sender_.Run(true);
    timer_.Stop();
  }
}

}  // namespace rmad
