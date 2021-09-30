// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_STATE_HANDLER_UPDATE_RO_FIRMWARE_STATE_HANDLER_H_
#define RMAD_STATE_HANDLER_UPDATE_RO_FIRMWARE_STATE_HANDLER_H_

#include "rmad/state_handler/base_state_handler.h"

#include <memory>

#include "rmad/system/tpm_manager_client.h"

namespace rmad {

class UpdateRoFirmwareStateHandler : public BaseStateHandler {
 public:
  explicit UpdateRoFirmwareStateHandler(scoped_refptr<JsonStore> json_store);
  // Used to inject mock |tpm_manager_client_| for testing.
  UpdateRoFirmwareStateHandler(
      scoped_refptr<JsonStore> json_store,
      std::unique_ptr<TpmManagerClient> tpm_manager_client);

  ASSIGN_STATE(RmadState::StateCase::kUpdateRoFirmware);
  SET_REPEATABLE;

  RmadErrorCode InitializeState() override;
  GetNextStateCaseReply GetNextStateCase(const RmadState& state) override;

 protected:
  ~UpdateRoFirmwareStateHandler() override = default;

 private:
  void UpdateFirmwareFromUsbAsync();

  std::unique_ptr<TpmManagerClient> tpm_manager_client_;
};

}  // namespace rmad

#endif  // RMAD_STATE_HANDLER_UPDATE_RO_FIRMWARE_STATE_HANDLER_H_
