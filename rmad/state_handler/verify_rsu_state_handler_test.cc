// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>

#include <base/memory/scoped_refptr.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rmad/state_handler/state_handler_test_common.h"
#include "rmad/state_handler/verify_rsu_state_handler.h"
#include "rmad/utils/mock_cr50_utils.h"
#include "rmad/utils/mock_crossystem_utils.h"

using testing::_;
using testing::DoAll;
using testing::NiceMock;
using testing::Return;
using testing::SetArgPointee;

namespace rmad {

class VerifyRsuStateHandlerTest : public StateHandlerTest {
 public:
  scoped_refptr<VerifyRsuStateHandler> CreateStateHandler(
      bool factory_mode_enabled, int wp_status) {
    // Mock |Cr50Utils|.
    auto mock_cr50_utils = std::make_unique<NiceMock<MockCr50Utils>>();
    ON_CALL(*mock_cr50_utils, IsFactoryModeEnabled())
        .WillByDefault(Return(factory_mode_enabled));
    // Mock |CrosSystemUtils|.
    auto mock_crossystem_utils =
        std::make_unique<NiceMock<MockCrosSystemUtils>>();
    ON_CALL(*mock_crossystem_utils, GetInt(_, _))
        .WillByDefault(DoAll(SetArgPointee<1>(wp_status), Return(true)));

    return base::MakeRefCounted<VerifyRsuStateHandler>(
        json_store_, std::move(mock_cr50_utils),
        std::move(mock_crossystem_utils));
  }
};

TEST_F(VerifyRsuStateHandlerTest, InitializeState_Success) {
  auto handler = CreateStateHandler(true, 0);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
}

TEST_F(VerifyRsuStateHandlerTest, GetNextStateCase_Success) {
  auto handler = CreateStateHandler(true, 0);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  RmadState state;
  state.set_allocated_verify_rsu(new VerifyRsuState);

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kWpDisableComplete);
}

TEST_F(VerifyRsuStateHandlerTest, GetNextStateCase_MissingState) {
  auto handler = CreateStateHandler(true, 0);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // No VerifyRsuState.
  RmadState state;

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kVerifyRsu);
}

TEST_F(VerifyRsuStateHandlerTest, GetNextStateCase_FactoryModeDisabled) {
  auto handler = CreateStateHandler(false, 0);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  RmadState state;
  state.set_allocated_verify_rsu(new VerifyRsuState);

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_TRANSITION_FAILED);
  EXPECT_EQ(state_case, RmadState::StateCase::kVerifyRsu);
}

TEST_F(VerifyRsuStateHandlerTest, GetNextStateCase_WriteProtectEnabled) {
  auto handler = CreateStateHandler(true, 1);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  RmadState state;
  state.set_allocated_verify_rsu(new VerifyRsuState);

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_TRANSITION_FAILED);
  EXPECT_EQ(state_case, RmadState::StateCase::kVerifyRsu);
}

}  // namespace rmad
