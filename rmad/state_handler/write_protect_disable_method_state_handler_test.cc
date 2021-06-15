// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>

#include <base/memory/scoped_refptr.h>
#include <gtest/gtest.h>

#include "rmad/state_handler/state_handler_test_common.h"
#include "rmad/state_handler/write_protect_disable_method_state_handler.h"

namespace rmad {

class WriteProtectDisableMethodStateHandlerTest : public StateHandlerTest {
 public:
  scoped_refptr<WriteProtectDisableMethodStateHandler> CreateStateHandler() {
    return base::MakeRefCounted<WriteProtectDisableMethodStateHandler>(
        json_store_);
  }
};

TEST_F(WriteProtectDisableMethodStateHandlerTest, InitializeState_Success) {
  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
}

TEST_F(WriteProtectDisableMethodStateHandlerTest,
       GetNextStateCase_Success_RSU) {
  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto wp_disable_method = std::make_unique<WriteProtectDisableMethodState>();
  wp_disable_method->set_disable_method(
      WriteProtectDisableMethodState::RMAD_WP_DISABLE_RSU);
  RmadState state;
  state.set_allocated_wp_disable_method(wp_disable_method.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kWpDisableRsu);
}

TEST_F(WriteProtectDisableMethodStateHandlerTest,
       GetNextStateCase_Success_Physical) {
  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto wp_disable_method = std::make_unique<WriteProtectDisableMethodState>();
  wp_disable_method->set_disable_method(
      WriteProtectDisableMethodState::RMAD_WP_DISABLE_PHYSICAL);
  RmadState state;
  state.set_allocated_wp_disable_method(wp_disable_method.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kWpDisablePhysical);
}

TEST_F(WriteProtectDisableMethodStateHandlerTest,
       GetNextStateCase_MissingState) {
  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // No WriteProtectDisableMethodState.
  RmadState state;

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kWpDisableMethod);
}

TEST_F(WriteProtectDisableMethodStateHandlerTest,
       GetNextStateCase_MissingArgs) {
  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto wp_disable_method = std::make_unique<WriteProtectDisableMethodState>();
  wp_disable_method->set_disable_method(
      WriteProtectDisableMethodState::RMAD_WP_DISABLE_UNKNOWN);
  RmadState state;
  state.set_allocated_wp_disable_method(wp_disable_method.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_MISSING);
  EXPECT_EQ(state_case, RmadState::StateCase::kWpDisableMethod);
}

}  // namespace rmad
