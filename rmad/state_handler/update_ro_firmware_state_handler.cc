// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/update_ro_firmware_state_handler.h"

#include <memory>

#include "base/notreached.h"

namespace rmad {

UpdateRoFirmwareStateHandler::UpdateRoFirmwareStateHandler(
    scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {}

RmadErrorCode UpdateRoFirmwareStateHandler::InitializeState() {
  if (!state_.has_update_ro_firmware() && !RetrieveState()) {
    auto update_ro_firmware = std::make_unique<UpdateRoFirmwareState>();
    // TODO(chenghan): Set to false when RO verification is not supported.
    update_ro_firmware->set_optional(true);

    state_.set_allocated_update_ro_firmware(update_ro_firmware.release());
  }
  return RMAD_ERROR_OK;
}

BaseStateHandler::GetNextStateCaseReply
UpdateRoFirmwareStateHandler::GetNextStateCase(const RmadState& state) {
  if (!state.has_update_ro_firmware()) {
    LOG(ERROR) << "RmadState missing |update RO firmware| state.";
    return {.error = RMAD_ERROR_REQUEST_INVALID, .state_case = GetStateCase()};
  }
  const UpdateRoFirmwareState& update_ro_firmware = state.update_ro_firmware();
  if (update_ro_firmware.optional() != state_.update_ro_firmware().optional()) {
    LOG(ERROR) << "RmadState |optional| argument doesn't match.";
    return {.error = RMAD_ERROR_REQUEST_INVALID, .state_case = GetStateCase()};
  }
  if (update_ro_firmware.update() ==
      UpdateRoFirmwareState::RMAD_UPDATE_FIRMWARE_UNKNOWN) {
    LOG(ERROR) << "RmadState missing |udpate| argument.";
    return {.error = RMAD_ERROR_REQUEST_ARGS_MISSING,
            .state_case = GetStateCase()};
  }
  if (!update_ro_firmware.optional() &&
      update_ro_firmware.update() == UpdateRoFirmwareState::RMAD_UPDATE_SKIP) {
    LOG(ERROR) << "RO firmware update is mandatory.";
    return {.error = RMAD_ERROR_REQUEST_ARGS_VIOLATION,
            .state_case = GetStateCase()};
  }

  state_ = state;
  StoreState();

  // TODO(chenghan): This is currently a mock.
  switch (state_.update_ro_firmware().update()) {
    case UpdateRoFirmwareState::RMAD_UPDATE_FIRMWARE_DOWNLOAD:
      return {.error = RMAD_ERROR_TRANSITION_FAILED,
              .state_case = GetStateCase()};
    case UpdateRoFirmwareState::RMAD_UPDATE_FIRMWARE_RECOVERY_UTILITY:
      return {.error = RMAD_ERROR_TRANSITION_FAILED,
              .state_case = GetStateCase()};
    case UpdateRoFirmwareState::RMAD_UPDATE_SKIP:
      if (IsMainboardRepair()) {
        return {.error = RMAD_ERROR_OK,
                .state_case = RmadState::StateCase::kRestock};
      } else {
        return {.error = RMAD_ERROR_OK,
                .state_case = RmadState::StateCase::kUpdateDeviceInfo};
      }
    default:
      break;
  }
  NOTREACHED();
  return {.error = RMAD_ERROR_NOT_SET,
          .state_case = RmadState::StateCase::STATE_NOT_SET};
}

bool UpdateRoFirmwareStateHandler::IsMainboardRepair() const {
  // TODO(chenghan): Check |json_store_| for this info.
  return false;
}

}  // namespace rmad
