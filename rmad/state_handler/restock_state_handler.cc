// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/restock_state_handler.h"

#include <memory>
#include <utility>

#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/notreached.h>

#include "rmad/system/fake_power_manager_client.h"
#include "rmad/system/power_manager_client_impl.h"
#include "rmad/utils/dbus_utils.h"

namespace rmad {

namespace fake {

FakeRestockStateHandler::FakeRestockStateHandler(
    scoped_refptr<JsonStore> json_store, const base::FilePath& working_dir_path)
    : RestockStateHandler(
          json_store,
          std::make_unique<FakePowerManagerClient>(working_dir_path)) {}

}  // namespace fake

RestockStateHandler::RestockStateHandler(scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {
  power_manager_client_ =
      std::make_unique<PowerManagerClientImpl>(GetSystemBus());
}

RestockStateHandler::RestockStateHandler(
    scoped_refptr<JsonStore> json_store,
    std::unique_ptr<PowerManagerClient> power_manager_client)
    : BaseStateHandler(json_store),
      power_manager_client_(std::move(power_manager_client)) {}

RmadErrorCode RestockStateHandler::InitializeState() {
  if (!state_.has_restock() && !RetrieveState()) {
    state_.set_allocated_restock(new RestockState);
  }
  return RMAD_ERROR_OK;
}

BaseStateHandler::GetNextStateCaseReply RestockStateHandler::GetNextStateCase(
    const RmadState& state) {
  if (!state.has_restock()) {
    LOG(ERROR) << "RmadState missing |restock| state.";
    return NextStateCaseWrapper(RMAD_ERROR_REQUEST_INVALID);
  }

  // For the first bootup after restock and shutdown, the state machine will try
  // to automatically transition to the next state. Therefore, we do not store
  // the state to prevent the continuous shutdown.
  switch (state.restock().choice()) {
    case RestockState::RMAD_RESTOCK_UNKNOWN:
      return NextStateCaseWrapper(RMAD_ERROR_REQUEST_ARGS_MISSING);
    case RestockState::RMAD_RESTOCK_SHUTDOWN_AND_RESTOCK:
      // Wait for a while before shutting down.
      timer_.Start(FROM_HERE, kShutdownDelay, this,
                   &RestockStateHandler::Shutdown);
      return NextStateCaseWrapper(GetStateCase(), RMAD_ERROR_EXPECT_SHUTDOWN,
                                  AdditionalActivity::SHUTDOWN);
    case RestockState::RMAD_RESTOCK_CONTINUE_RMA:
      return NextStateCaseWrapper(RmadState::StateCase::kUpdateDeviceInfo);
    default:
      break;
  }
  NOTREACHED();
  return NextStateCaseWrapper(RmadState::StateCase::STATE_NOT_SET,
                              RMAD_ERROR_NOT_SET, AdditionalActivity::NOTHING);
}

void RestockStateHandler::Shutdown() {
  LOG(INFO) << "Shutting down to restock";
  if (!power_manager_client_->Shutdown()) {
    LOG(ERROR) << "Failed to shut down";
  }
}

}  // namespace rmad
