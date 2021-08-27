// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>

#include <base/memory/scoped_refptr.h>
#include <gtest/gtest.h>

#include "rmad/constants.h"
#include "rmad/state_handler/state_handler_test_common.h"
#include "rmad/state_handler/write_protect_disable_complete_state_handler.h"
#include "rmad/utils/mock_cr50_utils.h"

using testing::_;
using testing::NiceMock;
using testing::Return;

namespace rmad {

class WriteProtectDisableCompleteStateHandlerTest : public StateHandlerTest {
 public:
  scoped_refptr<WriteProtectDisableCompleteStateHandler> CreateStateHandler(
      bool factory_mode_enabled, bool wp_disable_skipped) {
    auto mock_cr50_utils = std::make_unique<NiceMock<MockCr50Utils>>();
    ON_CALL(*mock_cr50_utils, IsFactoryModeEnabled())
        .WillByDefault(Return(factory_mode_enabled));

    EXPECT_TRUE(json_store_->SetValue(kWpDisableSkipped, wp_disable_skipped));

    return base::MakeRefCounted<WriteProtectDisableCompleteStateHandler>(
        json_store_, std::move(mock_cr50_utils));
  }
};

TEST_F(WriteProtectDisableCompleteStateHandlerTest,
       InitializeState_FactoryModeEnabled_WpDisableSkipped) {
  auto handler = CreateStateHandler(true, true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  EXPECT_EQ(handler->GetState().wp_disable_complete().keep_device_open(),
            false);
  EXPECT_EQ(handler->GetState().wp_disable_complete().wp_disable_skipped(),
            true);
}

TEST_F(WriteProtectDisableCompleteStateHandlerTest,
       InitializeState_FactoryModeEnabled_WpDisableNotSkipped) {
  auto handler = CreateStateHandler(true, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  EXPECT_EQ(handler->GetState().wp_disable_complete().keep_device_open(),
            false);
  EXPECT_EQ(handler->GetState().wp_disable_complete().wp_disable_skipped(),
            false);
}

TEST_F(WriteProtectDisableCompleteStateHandlerTest,
       InitializeState_FactoryModeDisabled_WpDisableSkipped) {
  // Should not happen in real use case.
  auto handler = CreateStateHandler(false, true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  EXPECT_EQ(handler->GetState().wp_disable_complete().keep_device_open(), true);
  EXPECT_EQ(handler->GetState().wp_disable_complete().wp_disable_skipped(),
            true);
}

TEST_F(WriteProtectDisableCompleteStateHandlerTest,
       InitializeState_FactoryModeDisabled_WpDisableNotSkipped) {
  auto handler = CreateStateHandler(false, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  EXPECT_EQ(handler->GetState().wp_disable_complete().keep_device_open(), true);
  EXPECT_EQ(handler->GetState().wp_disable_complete().wp_disable_skipped(),
            false);
}

TEST_F(WriteProtectDisableCompleteStateHandlerTest, GetNextStateCase_Success) {
  auto handler = CreateStateHandler(true, true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  RmadState state;
  state.set_allocated_wp_disable_complete(new WriteProtectDisableCompleteState);

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateRoFirmware);
}

TEST_F(WriteProtectDisableCompleteStateHandlerTest,
       GetNextStateCase_MissingState) {
  auto handler = CreateStateHandler(false, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // No WriteProtectDisableCompleteState.
  RmadState state;

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kWpDisableComplete);
}

}  // namespace rmad
