// Copyright 2022 The Chromium OS Authors. All rights reserved.
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
#include "rmad/state_handler/wipe_selection_state_handler.h"
#include "rmad/utils/mock_cr50_utils.h"

using testing::NiceMock;
using testing::Return;

namespace rmad {

class WipeSelectionStateHandlerTest : public StateHandlerTest {
 public:
  scoped_refptr<WipeSelectionStateHandler> CreateStateHandler(
      bool factory_mode_enabled) {
    // Mock |Cr50Utils|.
    auto mock_cr50_utils = std::make_unique<NiceMock<MockCr50Utils>>();
    ON_CALL(*mock_cr50_utils, IsFactoryModeEnabled())
        .WillByDefault(Return(factory_mode_enabled));

    return base::MakeRefCounted<WipeSelectionStateHandler>(
        json_store_, std::move(mock_cr50_utils));
  }

  void CheckJsonStoreWipeDevice(bool expected_wipe_device) {
    bool wipe_device;
    EXPECT_TRUE(json_store_->GetValue(kWipeDevice, &wipe_device));
    EXPECT_EQ(wipe_device, expected_wipe_device);
  }

  void CheckJsonStoreWpDisableSkipped(bool expected_skipped) {
    bool wp_disable_skipped;
    int wp_disable_method;

    if (expected_skipped) {
      EXPECT_TRUE(
          json_store_->GetValue(kWpDisableSkipped, &wp_disable_skipped));
      EXPECT_TRUE(wp_disable_skipped);
      EXPECT_TRUE(json_store_->GetValue(kWriteProtectDisableMethod,
                                        &wp_disable_method));
      EXPECT_EQ(wp_disable_method,
                static_cast<int>(WriteProtectDisableMethod::SKIPPED));
    } else {
      EXPECT_FALSE(
          json_store_->GetValue(kWpDisableSkipped, &wp_disable_skipped));
      EXPECT_FALSE(json_store_->GetValue(kWriteProtectDisableMethod,
                                         &wp_disable_method));
    }
  }
};

TEST_F(WipeSelectionStateHandlerTest, InitializeState_Success) {
  json_store_->SetValue(kSameOwner, true);
  json_store_->SetValue(kWpDisableRequired, true);
  json_store_->SetValue(kCcdBlocked, true);

  auto handler = CreateStateHandler(false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
}

TEST_F(WipeSelectionStateHandlerTest, InitializeState_MissingVars_SameOwner) {
  // No kSameOwner in |json_store_|.

  auto handler = CreateStateHandler(false);
  EXPECT_EQ(handler->InitializeState(),
            RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED);
}

TEST_F(WipeSelectionStateHandlerTest,
       InitializeState_MissingVars_WpDisableRequired) {
  // No kWpDisableRequired in |json_store_|.
  json_store_->SetValue(kSameOwner, true);

  auto handler = CreateStateHandler(false);
  EXPECT_EQ(handler->InitializeState(),
            RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED);
}

TEST_F(WipeSelectionStateHandlerTest, InitializeState_MissingVars_CcdBlocked) {
  // No kCcdBlocked in |json_store_|.
  json_store_->SetValue(kSameOwner, true);
  json_store_->SetValue(kWpDisableRequired, true);

  auto handler = CreateStateHandler(false);
  EXPECT_EQ(handler->InitializeState(),
            RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED);
}

TEST_F(WipeSelectionStateHandlerTest,
       InitializeState_WrongCondition_DifferentOwner) {
  json_store_->SetValue(kSameOwner, false);

  auto handler = CreateStateHandler(false);
  EXPECT_EQ(handler->InitializeState(),
            RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED);
}

// Definition of Case 1 ~ Case 5 are described in
// wipe_selection_state_handler.cc
TEST_F(WipeSelectionStateHandlerTest,
       GetNextStateCase_Case1_FactoryModeDisabled) {
  json_store_->SetValue(kSameOwner, true);
  json_store_->SetValue(kWpDisableRequired, true);
  json_store_->SetValue(kCcdBlocked, true);

  auto handler = CreateStateHandler(false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  RmadState state;
  state.mutable_wipe_selection()->set_wipe_device(true);

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kWpDisableRsu);

  CheckJsonStoreWipeDevice(true);
  CheckJsonStoreWpDisableSkipped(false);
}

TEST_F(WipeSelectionStateHandlerTest,
       GetNextStateCase_Case1_FactoryModeEnabled) {
  json_store_->SetValue(kSameOwner, true);
  json_store_->SetValue(kWpDisableRequired, true);
  json_store_->SetValue(kCcdBlocked, true);

  auto handler = CreateStateHandler(true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  RmadState state;
  state.mutable_wipe_selection()->set_wipe_device(true);

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kWpDisableComplete);

  CheckJsonStoreWipeDevice(true);
  CheckJsonStoreWpDisableSkipped(true);
}

TEST_F(WipeSelectionStateHandlerTest,
       GetNextStateCase_Case2_FactoryModeDisabled) {
  json_store_->SetValue(kSameOwner, true);
  json_store_->SetValue(kWpDisableRequired, true);
  json_store_->SetValue(kCcdBlocked, true);

  auto handler = CreateStateHandler(false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  RmadState state;
  state.mutable_wipe_selection()->set_wipe_device(false);

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kWpDisablePhysical);

  CheckJsonStoreWipeDevice(false);
  CheckJsonStoreWpDisableSkipped(false);
}

TEST_F(WipeSelectionStateHandlerTest,
       GetNextStateCase_Case2_FactoryModeEnabled) {
  json_store_->SetValue(kSameOwner, true);
  json_store_->SetValue(kWpDisableRequired, true);
  json_store_->SetValue(kCcdBlocked, true);

  auto handler = CreateStateHandler(true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  RmadState state;
  state.mutable_wipe_selection()->set_wipe_device(false);

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kWpDisableComplete);

  CheckJsonStoreWipeDevice(false);
  CheckJsonStoreWpDisableSkipped(true);
}

TEST_F(WipeSelectionStateHandlerTest,
       GetNextStateCase_Case3_FactoryModeDisabled) {
  json_store_->SetValue(kSameOwner, true);
  json_store_->SetValue(kWpDisableRequired, true);
  json_store_->SetValue(kCcdBlocked, false);

  auto handler = CreateStateHandler(false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  RmadState state;
  state.mutable_wipe_selection()->set_wipe_device(true);

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kWpDisableMethod);

  CheckJsonStoreWipeDevice(true);
  CheckJsonStoreWpDisableSkipped(false);
}

TEST_F(WipeSelectionStateHandlerTest,
       GetNextStateCase_Case3_FactoryModeEnabled) {
  json_store_->SetValue(kSameOwner, true);
  json_store_->SetValue(kWpDisableRequired, true);
  json_store_->SetValue(kCcdBlocked, false);

  auto handler = CreateStateHandler(true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  RmadState state;
  state.mutable_wipe_selection()->set_wipe_device(true);

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kWpDisableComplete);

  CheckJsonStoreWipeDevice(true);
  CheckJsonStoreWpDisableSkipped(true);
}

TEST_F(WipeSelectionStateHandlerTest,
       GetNextStateCase_Case4_FactoryModeDisabled) {
  json_store_->SetValue(kSameOwner, true);
  json_store_->SetValue(kWpDisableRequired, true);
  json_store_->SetValue(kCcdBlocked, false);

  auto handler = CreateStateHandler(false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  RmadState state;
  state.mutable_wipe_selection()->set_wipe_device(false);

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kWpDisablePhysical);

  CheckJsonStoreWipeDevice(false);
  CheckJsonStoreWpDisableSkipped(false);
}

TEST_F(WipeSelectionStateHandlerTest,
       GetNextStateCase_Case4_FactoryModeEnabled) {
  json_store_->SetValue(kSameOwner, true);
  json_store_->SetValue(kWpDisableRequired, true);
  json_store_->SetValue(kCcdBlocked, false);

  auto handler = CreateStateHandler(true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  RmadState state;
  state.mutable_wipe_selection()->set_wipe_device(false);

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kWpDisableComplete);

  CheckJsonStoreWipeDevice(false);
  CheckJsonStoreWpDisableSkipped(true);
}

TEST_F(WipeSelectionStateHandlerTest, GetNextStateCase_Case5) {
  json_store_->SetValue(kSameOwner, true);
  json_store_->SetValue(kWpDisableRequired, false);

  auto handler = CreateStateHandler(false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  RmadState state;
  state.mutable_wipe_selection()->set_wipe_device(true);

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kFinalize);

  CheckJsonStoreWipeDevice(true);
}

TEST_F(WipeSelectionStateHandlerTest, GetNextStateCase_MissingState) {
  json_store_->SetValue(kSameOwner, true);
  json_store_->SetValue(kWpDisableRequired, false);

  auto handler = CreateStateHandler(false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // No WipeSelectionState.
  RmadState state;

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kWipeSelection);
}

TEST_F(WipeSelectionStateHandlerTest, TryGetNextStateCaseAtBoot) {
  json_store_->SetValue(kSameOwner, true);
  json_store_->SetValue(kWpDisableRequired, false);

  auto handler = CreateStateHandler(false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto [error, state_case] = handler->TryGetNextStateCaseAtBoot();
  EXPECT_EQ(error, RMAD_ERROR_TRANSITION_FAILED);
  EXPECT_EQ(state_case, RmadState::StateCase::kWipeSelection);
}

}  // namespace rmad
