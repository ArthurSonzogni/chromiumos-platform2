// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>

#include <base/memory/scoped_refptr.h>
#include <base/test/task_environment.h>
#include <gtest/gtest.h>

#include "rmad/constants.h"
#include "rmad/proto_bindings/rmad.pb.h"
#include "rmad/state_handler/state_handler_test_common.h"
#include "rmad/state_handler/write_protect_disable_complete_state_handler.h"
#include "rmad/utils/mock_gsc_utils.h"
#include "rmad/utils/mock_write_protect_utils.h"

using testing::Assign;
using testing::NiceMock;
using testing::Return;

namespace rmad {

class WriteProtectDisableCompleteStateHandlerTest : public StateHandlerTest {
 public:
  struct StateHandlerArgs {
    bool disable_swwp_succeeded = true;
    bool* reboot_called = nullptr;
  };

  scoped_refptr<WriteProtectDisableCompleteStateHandler> CreateStateHandler(
      const StateHandlerArgs& args) {
    // Mock |GscUtils|.
    auto mock_gsc_utils = std::make_unique<NiceMock<MockGscUtils>>();
    if (args.reboot_called) {
      ON_CALL(*mock_gsc_utils, Reboot())
          .WillByDefault(DoAll(Assign(args.reboot_called, true), Return(true)));
    } else {
      ON_CALL(*mock_gsc_utils, Reboot()).WillByDefault(Return(true));
    }
    // Mock |WriteProtectUtils|.
    auto mock_write_protect_utils =
        std::make_unique<NiceMock<MockWriteProtectUtils>>();
    ON_CALL(*mock_write_protect_utils, DisableSoftwareWriteProtection())
        .WillByDefault(Return(args.disable_swwp_succeeded));

    return base::MakeRefCounted<WriteProtectDisableCompleteStateHandler>(
        json_store_, daemon_callback_, std::move(mock_gsc_utils),
        std::move(mock_write_protect_utils));
  }

 protected:
  // Variables for TaskRunner.
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
};

TEST_F(WriteProtectDisableCompleteStateHandlerTest, InitializeState_Skipped) {
  // Set up environment for skipping disabling WP.
  EXPECT_TRUE(json_store_->SetValue(
      kWpDisableMethod, WpDisableMethod_Name(RMAD_WP_DISABLE_METHOD_SKIPPED)));

  auto handler = CreateStateHandler({});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  EXPECT_EQ(handler->GetState().wp_disable_complete().action(),
            WriteProtectDisableCompleteState::RMAD_WP_DISABLE_COMPLETE_NO_OP);
}

TEST_F(WriteProtectDisableCompleteStateHandlerTest, InitializeState_Rsu) {
  // Set up environment for using RSU to disable WP.
  EXPECT_TRUE(json_store_->SetValue(
      kWpDisableMethod, WpDisableMethod_Name(RMAD_WP_DISABLE_METHOD_RSU)));

  auto handler = CreateStateHandler({});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  EXPECT_EQ(handler->GetState().wp_disable_complete().action(),
            WriteProtectDisableCompleteState::RMAD_WP_DISABLE_COMPLETE_NO_OP);
}

TEST_F(WriteProtectDisableCompleteStateHandlerTest,
       InitializeState_PhysicalAssembleDevice) {
  // Set up environment for using physical method to disable WP and turn on
  // factory mode.
  EXPECT_TRUE(json_store_->SetValue(
      kWpDisableMethod,
      WpDisableMethod_Name(RMAD_WP_DISABLE_METHOD_PHYSICAL_ASSEMBLE_DEVICE)));

  auto handler = CreateStateHandler({});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  EXPECT_EQ(handler->GetState().wp_disable_complete().action(),
            WriteProtectDisableCompleteState::
                RMAD_WP_DISABLE_COMPLETE_ASSEMBLE_DEVICE);
}

TEST_F(WriteProtectDisableCompleteStateHandlerTest,
       InitializeState_PhysicalKeepDeviceOpen) {
  // Set up environment for using physical method to disable WP and doesn't turn
  // on factory mode.
  EXPECT_TRUE(json_store_->SetValue(
      kWpDisableMethod,
      WpDisableMethod_Name(RMAD_WP_DISABLE_METHOD_PHYSICAL_KEEP_DEVICE_OPEN)));

  auto handler = CreateStateHandler({});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  EXPECT_EQ(handler->GetState().wp_disable_complete().action(),
            WriteProtectDisableCompleteState::
                RMAD_WP_DISABLE_COMPLETE_KEEP_DEVICE_OPEN);
}

TEST_F(WriteProtectDisableCompleteStateHandlerTest, InitializeState_Failed) {
  // |kWpDisableMethod| not set.
  auto handler = CreateStateHandler({});
  EXPECT_EQ(handler->InitializeState(),
            RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED);
}

TEST_F(WriteProtectDisableCompleteStateHandlerTest,
       GetNextStateCase_GscReboot) {
  bool reboot_called = false;
  // Set up environment for using RSU to disable WP.
  EXPECT_TRUE(json_store_->SetValue(
      kWpDisableMethod, WpDisableMethod_Name(RMAD_WP_DISABLE_METHOD_RSU)));

  auto handler = CreateStateHandler({.reboot_called = &reboot_called});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  RmadState state;
  state.set_allocated_wp_disable_complete(new WriteProtectDisableCompleteState);

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_EXPECT_REBOOT);
  EXPECT_EQ(state_case, RmadState::StateCase::kWpDisableComplete);

  // |kGscRebooted| is not set yet.
  bool gsc_rebooted;
  EXPECT_FALSE(json_store_->GetValue(kGscRebooted, &gsc_rebooted));

  // GSC reboot is called after |kRebootDelay| seconds.
  EXPECT_FALSE(reboot_called);
  task_environment_.FastForwardBy(
      WriteProtectDisableCompleteStateHandler::kRebootDelay);
  EXPECT_TRUE(reboot_called);

  // |kGscRebooted| is set.
  EXPECT_TRUE(json_store_->GetValue(kGscRebooted, &gsc_rebooted));
  EXPECT_TRUE(gsc_rebooted);
}

TEST_F(WriteProtectDisableCompleteStateHandlerTest,
       GetNextStateCase_MissingState) {
  // Set up environment for using RSU to disable WP.
  EXPECT_TRUE(json_store_->SetValue(
      kWpDisableMethod, WpDisableMethod_Name(RMAD_WP_DISABLE_METHOD_RSU)));

  auto handler = CreateStateHandler({});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // No WriteProtectDisableCompleteState.
  RmadState state;

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kWpDisableComplete);
}

TEST_F(WriteProtectDisableCompleteStateHandlerTest,
       TryGetNextStateCaseAtBoot_GscNotRebooted) {
  // Set up environment for using RSU to disable WP.
  EXPECT_TRUE(json_store_->SetValue(
      kWpDisableMethod, WpDisableMethod_Name(RMAD_WP_DISABLE_METHOD_RSU)));

  auto handler = CreateStateHandler({});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // GSC has not rebooted. Do not transition.
  auto [error, state_case] = handler->TryGetNextStateCaseAtBoot();
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kWpDisableComplete);
}

TEST_F(WriteProtectDisableCompleteStateHandlerTest,
       TryGetNextStateCaseAtBoot_GscRebooted) {
  // Set up environment for using RSU to disable WP.
  EXPECT_TRUE(json_store_->SetValue(
      kWpDisableMethod, WpDisableMethod_Name(RMAD_WP_DISABLE_METHOD_RSU)));
  // GSC has rebooted.
  EXPECT_TRUE(json_store_->SetValue(kGscRebooted, true));

  auto handler = CreateStateHandler({});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // GSC has rebooted. Transition to the next state.
  auto [error, state_case] = handler->TryGetNextStateCaseAtBoot();
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateRoFirmware);
}

TEST_F(WriteProtectDisableCompleteStateHandlerTest,
       iTryGetNextStateCaseAtBoot_DisableSwwpFailed) {
  // Set up environment for using RSU to disable WP.
  EXPECT_TRUE(json_store_->SetValue(
      kWpDisableMethod, WpDisableMethod_Name(RMAD_WP_DISABLE_METHOD_RSU)));
  // GSC has rebooted.
  EXPECT_TRUE(json_store_->SetValue(kGscRebooted, true));

  auto handler = CreateStateHandler({.disable_swwp_succeeded = false});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto [error, state_case] = handler->TryGetNextStateCaseAtBoot();
  EXPECT_EQ(error, RMAD_ERROR_WP_ENABLED);
  EXPECT_EQ(state_case, RmadState::StateCase::kWpDisableComplete);
}

}  // namespace rmad
