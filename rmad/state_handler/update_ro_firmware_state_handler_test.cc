// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>

#include <base/memory/scoped_refptr.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rmad/proto_bindings/rmad.pb.h"
#include "rmad/state_handler/state_handler_test_common.h"
#include "rmad/state_handler/update_ro_firmware_state_handler.h"
#include "rmad/system/mock_tpm_manager_client.h"
#include "rmad/utils/json_store.h"

using testing::_;
using testing::DoAll;
using testing::NiceMock;
using testing::Return;
using testing::SetArgPointee;

namespace rmad {

class UpdateRoFirmwareStateHandlerTest : public StateHandlerTest {
 public:
  scoped_refptr<UpdateRoFirmwareStateHandler> CreateStateHandler(
      bool ro_verified) {
    // Mock |TpmManagerClient|.
    auto mock_tpm_manager_client =
        std::make_unique<NiceMock<MockTpmManagerClient>>();
    ON_CALL(*mock_tpm_manager_client, GetRoVerificationStatus(_))
        .WillByDefault(DoAll(
            SetArgPointee<0>(ro_verified ? RoVerificationStatus::PASS
                                         : RoVerificationStatus::UNSUPPORTED),
            Return(true)));
    auto handler = base::MakeRefCounted<UpdateRoFirmwareStateHandler>(
        json_store_, std::move(mock_tpm_manager_client));
    return handler;
  }
};

TEST_F(UpdateRoFirmwareStateHandlerTest, InitializeState_Success_RoVerified) {
  auto handler = CreateStateHandler(true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  EXPECT_EQ(handler->GetState().update_ro_firmware().optional(), true);
}

TEST_F(UpdateRoFirmwareStateHandlerTest,
       InitializeState_Success_RoNotVerified) {
  auto handler = CreateStateHandler(false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  EXPECT_EQ(handler->GetState().update_ro_firmware().optional(), false);
}

TEST_F(UpdateRoFirmwareStateHandlerTest, GetNextStateCase_Success_Skip) {
  auto handler = CreateStateHandler(true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto update_ro_firmware = std::make_unique<UpdateRoFirmwareState>();
  update_ro_firmware->set_optional(true);
  update_ro_firmware->set_update(UpdateRoFirmwareState::RMAD_UPDATE_SKIP);
  RmadState state;
  state.set_allocated_update_ro_firmware(update_ro_firmware.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
}

TEST_F(UpdateRoFirmwareStateHandlerTest, GetNextStateCase_Success_Update) {
  auto handler = CreateStateHandler(true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto update_ro_firmware = std::make_unique<UpdateRoFirmwareState>();
  update_ro_firmware->set_optional(true);
  update_ro_firmware->set_update(
      UpdateRoFirmwareState::RMAD_UPDATE_FIRMWARE_RECOVERY_UTILITY);
  RmadState state;
  state.set_allocated_update_ro_firmware(update_ro_firmware.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_WAIT);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateRoFirmware);
}

TEST_F(UpdateRoFirmwareStateHandlerTest, GetNextStateCase_MissingState) {
  auto handler = CreateStateHandler(true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // No UpdateRoFirmwareState.
  RmadState state;

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateRoFirmware);
}

TEST_F(UpdateRoFirmwareStateHandlerTest, GetNextStateCase_MissingArgs) {
  auto handler = CreateStateHandler(true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto update_ro_firmware = std::make_unique<UpdateRoFirmwareState>();
  update_ro_firmware->set_update(
      UpdateRoFirmwareState::RMAD_UPDATE_FIRMWARE_UNKNOWN);
  RmadState state;
  state.set_allocated_update_ro_firmware(update_ro_firmware.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_MISSING);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateRoFirmware);
}

TEST_F(UpdateRoFirmwareStateHandlerTest, GetNextStateCase_Violation) {
  auto handler = CreateStateHandler(false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto update_ro_firmware = std::make_unique<UpdateRoFirmwareState>();
  update_ro_firmware->set_update(UpdateRoFirmwareState::RMAD_UPDATE_SKIP);
  RmadState state;
  state.set_allocated_update_ro_firmware(update_ro_firmware.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_VIOLATION);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateRoFirmware);
}

TEST_F(UpdateRoFirmwareStateHandlerTest, GetNextStateCase_Download) {
  // Download is currently not supported.
  auto handler = CreateStateHandler(true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto update_ro_firmware = std::make_unique<UpdateRoFirmwareState>();
  update_ro_firmware->set_optional(true);
  update_ro_firmware->set_update(
      UpdateRoFirmwareState::RMAD_UPDATE_FIRMWARE_DOWNLOAD);
  RmadState state;
  state.set_allocated_update_ro_firmware(update_ro_firmware.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_TRANSITION_FAILED);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateRoFirmware);
}

}  // namespace rmad
