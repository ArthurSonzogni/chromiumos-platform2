// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>

#include <base/memory/scoped_refptr.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rmad/constants.h"
#include "rmad/state_handler/state_handler_test_common.h"
#include "rmad/state_handler/write_protect_disable_method_state_handler.h"
#include "rmad/utils/mock_cr50_utils.h"

using testing::NiceMock;
using testing::Return;

namespace rmad {

class WriteProtectDisableMethodStateHandlerTest : public StateHandlerTest {
 public:
  scoped_refptr<WriteProtectDisableMethodStateHandler> CreateStateHandler(
      bool factory_mode_enabled) {
    // Mock |Cr59Utils|.
    auto mock_cr50_utils = std::make_unique<NiceMock<MockCr50Utils>>();
    ON_CALL(*mock_cr50_utils, IsFactoryModeEnabled())
        .WillByDefault(Return(factory_mode_enabled));

    return base::MakeRefCounted<WriteProtectDisableMethodStateHandler>(
        json_store_, std::move(mock_cr50_utils));
  }
};

TEST_F(WriteProtectDisableMethodStateHandlerTest, InitializeState_Success) {
  json_store_->SetValue(kSameOwner, true);
  json_store_->SetValue(kWpDisableRequired, true);
  json_store_->SetValue(kCcdBlocked, false);
  json_store_->SetValue(kWipeDevice, true);

  auto handler = CreateStateHandler(false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
}

TEST_F(WriteProtectDisableMethodStateHandlerTest,
       InitializeState_MissingVars_SameOwner) {
  // No kSameOwner in |json_store_|.

  auto handler = CreateStateHandler(false);
  EXPECT_EQ(handler->InitializeState(),
            RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED);
}

TEST_F(WriteProtectDisableMethodStateHandlerTest,
       InitializeState_MissingVars_WpDisableRequired) {
  // No kWpDisableRequired in |json_store_|.
  json_store_->SetValue(kSameOwner, true);

  auto handler = CreateStateHandler(false);
  EXPECT_EQ(handler->InitializeState(),
            RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED);
}

TEST_F(WriteProtectDisableMethodStateHandlerTest,
       InitializeState_MissingVars_WipeDevice) {
  // No kWipeDevice in |json_store_|.
  json_store_->SetValue(kSameOwner, true);
  json_store_->SetValue(kWpDisableRequired, true);

  auto handler = CreateStateHandler(false);
  EXPECT_EQ(handler->InitializeState(),
            RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED);
}

TEST_F(WriteProtectDisableMethodStateHandlerTest,
       InitializeState_MissingVars_CcdBlocked) {
  // No kCcdBlocked in |json_store_|.
  json_store_->SetValue(kSameOwner, true);
  json_store_->SetValue(kWpDisableRequired, true);
  json_store_->SetValue(kWipeDevice, true);

  auto handler = CreateStateHandler(false);
  EXPECT_EQ(handler->InitializeState(),
            RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED);
}

TEST_F(WriteProtectDisableMethodStateHandlerTest,
       InitializeState_WrongCondition_WpDisableNotRequired) {
  json_store_->SetValue(kSameOwner, true);
  json_store_->SetValue(kWpDisableRequired, false);
  json_store_->SetValue(kWipeDevice, true);

  auto handler = CreateStateHandler(false);
  EXPECT_EQ(handler->InitializeState(),
            RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED);
}

TEST_F(WriteProtectDisableMethodStateHandlerTest,
       InitializeState_WrongCondition_CcdBlocked) {
  json_store_->SetValue(kSameOwner, true);
  json_store_->SetValue(kWpDisableRequired, true);
  json_store_->SetValue(kCcdBlocked, true);
  json_store_->SetValue(kWipeDevice, true);

  auto handler = CreateStateHandler(false);
  EXPECT_EQ(handler->InitializeState(),
            RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED);
}

TEST_F(WriteProtectDisableMethodStateHandlerTest,
       InitializeState_WrongCondition_NoWipeDevice) {
  json_store_->SetValue(kSameOwner, true);
  json_store_->SetValue(kWpDisableRequired, true);
  json_store_->SetValue(kCcdBlocked, false);
  json_store_->SetValue(kWipeDevice, false);

  auto handler = CreateStateHandler(false);
  EXPECT_EQ(handler->InitializeState(),
            RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED);
}

TEST_F(WriteProtectDisableMethodStateHandlerTest,
       InitializeState_WrongCondition_FactoryModeEnabled) {
  json_store_->SetValue(kSameOwner, true);
  json_store_->SetValue(kWpDisableRequired, true);
  json_store_->SetValue(kCcdBlocked, false);
  json_store_->SetValue(kWipeDevice, true);

  auto handler = CreateStateHandler(true);
  EXPECT_EQ(handler->InitializeState(),
            RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED);
}

TEST_F(WriteProtectDisableMethodStateHandlerTest,
       GetNextStateCase_Success_RSU) {
  json_store_->SetValue(kSameOwner, true);
  json_store_->SetValue(kWpDisableRequired, true);
  json_store_->SetValue(kCcdBlocked, false);
  json_store_->SetValue(kWipeDevice, true);

  auto handler = CreateStateHandler(false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  RmadState state;
  state.mutable_wp_disable_method()->set_disable_method(
      WriteProtectDisableMethodState::RMAD_WP_DISABLE_RSU);

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kWpDisableRsu);
}

TEST_F(WriteProtectDisableMethodStateHandlerTest,
       GetNextStateCase_Success_Physical) {
  json_store_->SetValue(kSameOwner, true);
  json_store_->SetValue(kWpDisableRequired, true);
  json_store_->SetValue(kCcdBlocked, false);
  json_store_->SetValue(kWipeDevice, true);

  auto handler = CreateStateHandler(false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  RmadState state;
  state.mutable_wp_disable_method()->set_disable_method(
      WriteProtectDisableMethodState::RMAD_WP_DISABLE_PHYSICAL);

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kWpDisablePhysical);
}

TEST_F(WriteProtectDisableMethodStateHandlerTest,
       GetNextStateCase_MissingState) {
  json_store_->SetValue(kSameOwner, true);
  json_store_->SetValue(kWpDisableRequired, true);
  json_store_->SetValue(kCcdBlocked, false);
  json_store_->SetValue(kWipeDevice, true);

  auto handler = CreateStateHandler(false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // No WriteProtectDisableMethodState.
  RmadState state;

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kWpDisableMethod);
}

TEST_F(WriteProtectDisableMethodStateHandlerTest,
       GetNextStateCase_MissingArgs) {
  json_store_->SetValue(kSameOwner, true);
  json_store_->SetValue(kWpDisableRequired, true);
  json_store_->SetValue(kCcdBlocked, false);
  json_store_->SetValue(kWipeDevice, true);

  auto handler = CreateStateHandler(false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  RmadState state;
  state.mutable_wp_disable_method()->set_disable_method(
      WriteProtectDisableMethodState::RMAD_WP_DISABLE_UNKNOWN);

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_MISSING);
  EXPECT_EQ(state_case, RmadState::StateCase::kWpDisableMethod);
}

}  // namespace rmad
