// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>

#include <base/memory/scoped_refptr.h>
#include <base/test/task_environment.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rmad/executor/udev/mock_udev_device.h"
#include "rmad/executor/udev/mock_udev_utils.h"
#include "rmad/metrics/metrics_utils.h"
#include "rmad/proto_bindings/rmad.pb.h"
#include "rmad/state_handler/state_handler_test_common.h"
#include "rmad/state_handler/update_ro_firmware_state_handler.h"
#include "rmad/system/mock_cros_disks_client.h"
#include "rmad/system/mock_power_manager_client.h"
#include "rmad/utils/json_store.h"
#include "rmad/utils/mock_cmd_utils.h"
#include "rmad/utils/mock_crossystem_utils.h"
#include "rmad/utils/mock_flashrom_utils.h"

using testing::_;
using testing::DoAll;
using testing::Eq;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::SetArgPointee;

namespace rmad {

class UpdateRoFirmwareStateHandlerTest : public StateHandlerTest {
 public:
  scoped_refptr<UpdateRoFirmwareStateHandler> CreateStateHandler(
      bool ro_verified, int hwwp_status) {
    MetricsUtils::SetMetricsValue(json_store_, kRoFirmwareVerified,
                                  ro_verified);
    // Mock |UdevUtils|.
    auto mock_udev_utils = std::make_unique<NiceMock<MockUdevUtils>>();
    ON_CALL(*mock_udev_utils, EnumerateBlockDevices())
        .WillByDefault(Invoke(
            []() { return std::vector<std::unique_ptr<UdevDevice>>(); }));

    // Mock |CmdUtils|.
    auto mock_cmd_utils = std::make_unique<NiceMock<MockCmdUtils>>();

    // Mock |CrosSystemUtils|.
    auto mock_crossystem_utils =
        std::make_unique<NiceMock<MockCrosSystemUtils>>();
    ON_CALL(*mock_crossystem_utils,
            GetInt(Eq(CrosSystemUtils::kHwwpStatusProperty), _))
        .WillByDefault(DoAll(SetArgPointee<1>(hwwp_status), Return(true)));

    // Mock |FlashromUtils|.
    auto mock_flashrom_utils = std::make_unique<NiceMock<MockFlashromUtils>>();

    // Mock |CrosDisksClient|.
    auto mock_cros_disks_client =
        std::make_unique<NiceMock<MockCrosDisksClient>>();

    // Mock |PowerManagerClient|.
    auto mock_power_manager_client =
        std::make_unique<NiceMock<MockPowerManagerClient>>();

    return base::MakeRefCounted<UpdateRoFirmwareStateHandler>(
        json_store_, daemon_callback_, std::move(mock_udev_utils),
        std::move(mock_cmd_utils), std::move(mock_crossystem_utils),
        std::move(mock_flashrom_utils), std::move(mock_cros_disks_client),
        std::move(mock_power_manager_client));
  }

 protected:
  base::test::TaskEnvironment task_environment_;
};

TEST_F(UpdateRoFirmwareStateHandlerTest, InitializeState_Success_RoVerified) {
  auto handler = CreateStateHandler(true, 0);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  EXPECT_EQ(handler->GetState().update_ro_firmware().optional(), true);
}

TEST_F(UpdateRoFirmwareStateHandlerTest,
       InitializeState_Success_RoNotVerified) {
  auto handler = CreateStateHandler(false, 0);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  EXPECT_EQ(handler->GetState().update_ro_firmware().optional(), false);
}

TEST_F(UpdateRoFirmwareStateHandlerTest, InitializeState_Failed) {
  auto handler = CreateStateHandler(true, 1);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_WP_ENABLED);
}

TEST_F(UpdateRoFirmwareStateHandlerTest, GetNextStateCase_Success_Skip) {
  auto handler = CreateStateHandler(true, 0);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto update_ro_firmware = std::make_unique<UpdateRoFirmwareState>();
  update_ro_firmware->set_choice(
      UpdateRoFirmwareState::RMAD_UPDATE_CHOICE_SKIP);
  RmadState state;
  state.set_allocated_update_ro_firmware(update_ro_firmware.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
}

TEST_F(UpdateRoFirmwareStateHandlerTest, GetNextStateCase_Success_Update) {
  auto handler = CreateStateHandler(true, 0);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto update_ro_firmware = std::make_unique<UpdateRoFirmwareState>();
  update_ro_firmware->set_choice(
      UpdateRoFirmwareState::RMAD_UPDATE_CHOICE_CONTINUE);
  RmadState state;
  state.set_allocated_update_ro_firmware(update_ro_firmware.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_WAIT);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateRoFirmware);
}

TEST_F(UpdateRoFirmwareStateHandlerTest, GetNextStateCase_MissingState) {
  auto handler = CreateStateHandler(true, 0);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // No UpdateRoFirmwareState.
  RmadState state;

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateRoFirmware);
}

TEST_F(UpdateRoFirmwareStateHandlerTest, GetNextStateCase_MissingArgs) {
  auto handler = CreateStateHandler(true, 0);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto update_ro_firmware = std::make_unique<UpdateRoFirmwareState>();
  update_ro_firmware->set_choice(
      UpdateRoFirmwareState::RMAD_UPDATE_CHOICE_UNKNOWN);
  RmadState state;
  state.set_allocated_update_ro_firmware(update_ro_firmware.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_MISSING);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateRoFirmware);
}

TEST_F(UpdateRoFirmwareStateHandlerTest, GetNextStateCase_Violation) {
  auto handler = CreateStateHandler(false, 0);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto update_ro_firmware = std::make_unique<UpdateRoFirmwareState>();
  update_ro_firmware->set_choice(
      UpdateRoFirmwareState::RMAD_UPDATE_CHOICE_SKIP);
  RmadState state;
  state.set_allocated_update_ro_firmware(update_ro_firmware.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_VIOLATION);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateRoFirmware);
}

}  // namespace rmad
