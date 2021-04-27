// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/update_ro_firmware_state_handler.h"

#include <memory>

namespace rmad {

UpdateRoFirmwareStateHandler::UpdateRoFirmwareStateHandler(
    scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {
  ResetState();
}

RmadState::StateCase UpdateRoFirmwareStateHandler::GetNextStateCase() const {
  // TODO(chenghan): This is currently a mock.
  if (state_.update_ro_firmware().optional() ||
      state_.update_ro_firmware().update() ==
          UpdateRoFirmwareState::RMAD_UPDATE_SKIP) {
    if (IsMotherboardRepair()) {
      return RmadState::StateCase::kRestockState;
    } else {
      return RmadState::StateCase::kUpdateDeviceInfo;
    }
  }
  // Not ready to go to next state.
  return GetStateCase();
}

RmadErrorCode UpdateRoFirmwareStateHandler::UpdateState(
    const RmadState& state) {
  CHECK(state.has_update_ro_firmware())
      << "RmadState missing update RO firmware state.";
  const UpdateRoFirmwareState& update_ro_firmware = state.update_ro_firmware();
  if (update_ro_firmware.update() ==
      UpdateRoFirmwareState::RMAD_UPDATE_FIRMWARE_UNKNOWN) {
    // TODO(gavindodd): What is correct error for unset/missing fields?
    return RMAD_ERROR_REQUEST_INVALID;
  }

  auto new_update_ro_firmware =
      std::make_unique<UpdateRoFirmwareState>(update_ro_firmware);
  new_update_ro_firmware->set_update(state_.update_ro_firmware().update());
  state_.set_allocated_update_ro_firmware(new_update_ro_firmware.release());

  return RMAD_ERROR_OK;
}

RmadErrorCode UpdateRoFirmwareStateHandler::ResetState() {
  // TODO(gavindodd): Set state values in the WelcomeState proto and add to
  // json_store.
  auto update_ro_firmware = std::make_unique<UpdateRoFirmwareState>();
  // This is currently always optional.
  update_ro_firmware->set_optional(true);

  state_.set_allocated_update_ro_firmware(update_ro_firmware.release());

  return RMAD_ERROR_OK;
}

bool UpdateRoFirmwareStateHandler::IsMotherboardRepair() const {
  // TODO(chenghan): Check json_store for this info.
  return false;
}

}  // namespace rmad
