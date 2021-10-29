// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/update_ro_firmware_state_handler.h"

#include <memory>
#include <utility>

#include <base/logging.h>
#include <base/notreached.h>

#include "rmad/system/tpm_manager_client_impl.h"
#include "rmad/utils/dbus_utils.h"

namespace rmad {

UpdateRoFirmwareStateHandler::UpdateRoFirmwareStateHandler(
    scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {
  tpm_manager_client_ = std::make_unique<TpmManagerClientImpl>(GetSystemBus());
}

UpdateRoFirmwareStateHandler::UpdateRoFirmwareStateHandler(
    scoped_refptr<JsonStore> json_store,
    std::unique_ptr<TpmManagerClient> tpm_manager_client)
    : BaseStateHandler(json_store),
      tpm_manager_client_(std::move(tpm_manager_client)) {}

RmadErrorCode UpdateRoFirmwareStateHandler::InitializeState() {
  if (!state_.has_update_ro_firmware()) {
    auto update_ro_firmware = std::make_unique<UpdateRoFirmwareState>();
    RoVerificationStatus status;
    update_ro_firmware->set_optional(
        tpm_manager_client_->GetRoVerificationStatus(&status) &&
        status == RoVerificationStatus::PASS);
    state_.set_allocated_update_ro_firmware(update_ro_firmware.release());
  }
  return RMAD_ERROR_OK;
}

BaseStateHandler::GetNextStateCaseReply
UpdateRoFirmwareStateHandler::GetNextStateCase(const RmadState& state) {
  if (!state.has_update_ro_firmware()) {
    LOG(ERROR) << "RmadState missing |update RO firmware| state.";
    return NextStateCaseWrapper(RMAD_ERROR_REQUEST_INVALID);
  }
  const UpdateRoFirmwareState& update_ro_firmware = state.update_ro_firmware();
  if (update_ro_firmware.update() ==
      UpdateRoFirmwareState::RMAD_UPDATE_FIRMWARE_UNKNOWN) {
    LOG(ERROR) << "RmadState missing |udpate| argument.";
    return NextStateCaseWrapper(RMAD_ERROR_REQUEST_ARGS_MISSING);
  }
  if (!state_.update_ro_firmware().optional() &&
      update_ro_firmware.update() == UpdateRoFirmwareState::RMAD_UPDATE_SKIP) {
    LOG(ERROR) << "RO firmware update is mandatory.";
    return NextStateCaseWrapper(RMAD_ERROR_REQUEST_ARGS_VIOLATION);
  }

  // TODO(chenghan): This is currently a mock.
  switch (state.update_ro_firmware().update()) {
    case UpdateRoFirmwareState::RMAD_UPDATE_FIRMWARE_DOWNLOAD:
      // TODO(chenghan): This is not supported in V1.
      return NextStateCaseWrapper(RMAD_ERROR_TRANSITION_FAILED);
    case UpdateRoFirmwareState::RMAD_UPDATE_FIRMWARE_RECOVERY_UTILITY:
      UpdateFirmwareFromUsbAsync();
      return NextStateCaseWrapper(RMAD_ERROR_WAIT);
    case UpdateRoFirmwareState::RMAD_UPDATE_SKIP:
      return NextStateCaseWrapper(RmadState::StateCase::kUpdateDeviceInfo);
    default:
      break;
  }
  NOTREACHED();
  return NextStateCaseWrapper(RmadState::StateCase::STATE_NOT_SET,
                              RMAD_ERROR_NOT_SET, AdditionalActivity::NOTHING);
}

// TODO(chenghan): Implement this.
void UpdateRoFirmwareStateHandler::UpdateFirmwareFromUsbAsync() {}

}  // namespace rmad
