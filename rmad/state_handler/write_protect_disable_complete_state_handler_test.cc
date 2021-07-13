// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>

#include <base/memory/scoped_refptr.h>
#include <gtest/gtest.h>

#include "rmad/state_handler/state_handler_test_common.h"
#include "rmad/state_handler/write_protect_disable_complete_state_handler.h"
#include "rmad/system/mock_cryptohome_client.h"
#include "rmad/utils/mock_cr50_utils.h"

using testing::_;
using testing::NiceMock;
using testing::Return;

namespace rmad {

class WriteProtectDisableCompleteStateHandlerTest : public StateHandlerTest {
 public:
  scoped_refptr<WriteProtectDisableCompleteStateHandler> CreateStateHandler(
      bool factory_mode_enabled, bool has_fwmp) {
    auto mock_cr50_utils = std::make_unique<NiceMock<MockCr50Utils>>();
    ON_CALL(*mock_cr50_utils, IsFactoryModeEnabled())
        .WillByDefault(Return(factory_mode_enabled));
    auto mock_cryptohome_client =
        std::make_unique<NiceMock<MockCryptohomeClient>>();
    ON_CALL(*mock_cryptohome_client, HasFwmp()).WillByDefault(Return(has_fwmp));

    return base::MakeRefCounted<WriteProtectDisableCompleteStateHandler>(
        json_store_, std::move(mock_cr50_utils),
        std::move(mock_cryptohome_client));
  }
};

TEST_F(WriteProtectDisableCompleteStateHandlerTest, InitializeState_Success) {
  auto handler1 = CreateStateHandler(true, true);
  EXPECT_EQ(handler1->InitializeState(), RMAD_ERROR_OK);
  EXPECT_EQ(handler1->GetState().wp_disable_complete().keep_device_open(),
            false);
  EXPECT_EQ(
      handler1->GetState().wp_disable_complete().can_enable_factory_mode(),
      false);

  auto handler2 = CreateStateHandler(true, false);
  EXPECT_EQ(handler2->InitializeState(), RMAD_ERROR_OK);
  EXPECT_EQ(handler2->GetState().wp_disable_complete().keep_device_open(),
            false);
  EXPECT_EQ(
      handler2->GetState().wp_disable_complete().can_enable_factory_mode(),
      false);

  auto handler3 = CreateStateHandler(false, true);
  EXPECT_EQ(handler3->InitializeState(), RMAD_ERROR_OK);
  EXPECT_EQ(handler3->GetState().wp_disable_complete().keep_device_open(),
            true);
  EXPECT_EQ(
      handler3->GetState().wp_disable_complete().can_enable_factory_mode(),
      false);

  auto handler4 = CreateStateHandler(false, false);
  EXPECT_EQ(handler4->InitializeState(), RMAD_ERROR_OK);
  EXPECT_EQ(handler4->GetState().wp_disable_complete().keep_device_open(),
            true);
  EXPECT_EQ(
      handler4->GetState().wp_disable_complete().can_enable_factory_mode(),
      true);
}

TEST_F(WriteProtectDisableCompleteStateHandlerTest, GetNextStateCase_Success) {
  auto handler = CreateStateHandler(false, true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto wp_disable_complete =
      std::make_unique<WriteProtectDisableCompleteState>();
  wp_disable_complete->set_can_enable_factory_mode(false);
  RmadState state;
  state.set_allocated_wp_disable_complete(wp_disable_complete.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateRoFirmware);
}

TEST_F(WriteProtectDisableCompleteStateHandlerTest,
       GetNextStateCase_EnableFactoryMode) {
  auto handler = CreateStateHandler(false, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto wp_disable_complete =
      std::make_unique<WriteProtectDisableCompleteState>();
  wp_disable_complete->set_can_enable_factory_mode(true);
  wp_disable_complete->set_enable_factory_mode(true);
  RmadState state;
  state.set_allocated_wp_disable_complete(wp_disable_complete.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_EXPECT_REBOOT);
  EXPECT_EQ(state_case, RmadState::StateCase::kWpDisableComplete);
}

TEST_F(WriteProtectDisableCompleteStateHandlerTest,
       GetNextStateCase_MissingState) {
  auto handler = CreateStateHandler(false, true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // No WriteProtectDisableCompleteState.
  RmadState state;

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kWpDisableComplete);
}

TEST_F(WriteProtectDisableCompleteStateHandlerTest,
       GetNextStateCase_MismatchArgs) {
  auto handler = CreateStateHandler(false, true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto wp_disable_complete =
      std::make_unique<WriteProtectDisableCompleteState>();
  // |can_enable_factory_mode| should be false.
  wp_disable_complete->set_can_enable_factory_mode(true);
  RmadState state;
  state.set_allocated_wp_disable_complete(wp_disable_complete.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kWpDisableComplete);
}

TEST_F(WriteProtectDisableCompleteStateHandlerTest,
       GetNextStateCase_InvalidArgs) {
  auto handler = CreateStateHandler(true, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto wp_disable_complete =
      std::make_unique<WriteProtectDisableCompleteState>();
  wp_disable_complete->set_can_enable_factory_mode(false);
  // |can_enable_factory_mode| is false, so |enable_factory_mode| cannot be
  // true.
  wp_disable_complete->set_enable_factory_mode(true);
  RmadState state;
  state.set_allocated_wp_disable_complete(wp_disable_complete.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_VIOLATION);
  EXPECT_EQ(state_case, RmadState::StateCase::kWpDisableComplete);
}

}  // namespace rmad
