// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>

#include <base/memory/scoped_refptr.h>
#include <gtest/gtest.h>

#include "rmad/state_handler/state_handler_test_common.h"
#include "rmad/state_handler/welcome_screen_state_handler.h"
#include "rmad/utils/json_store.h"

namespace rmad {

class WelcomeScreenStateHandlerTest : public StateHandlerTest {
 public:
  scoped_refptr<WelcomeScreenStateHandler> CreateStateHandler() {
    return base::MakeRefCounted<WelcomeScreenStateHandler>(json_store_);
  }
};

TEST_F(WelcomeScreenStateHandlerTest, InitializeState_Success) {
  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
}

TEST_F(WelcomeScreenStateHandlerTest, GetNextStateCase_Success) {
  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto welcome = std::make_unique<WelcomeState>();
  welcome->set_choice(WelcomeState::RMAD_CHOICE_FINALIZE_REPAIR);
  RmadState state;
  state.set_allocated_welcome(welcome.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kSelectNetwork);
}

TEST_F(WelcomeScreenStateHandlerTest, GetNextStateCase_MissingState) {
  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // No WelcomeScreenState.
  RmadState state;

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kWelcome);
}

TEST_F(WelcomeScreenStateHandlerTest, GetNextStateCase_MissingArgs) {
  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto welcome = std::make_unique<WelcomeState>();
  welcome->set_choice(WelcomeState::RMAD_CHOICE_UNKNOWN);
  RmadState state;
  state.set_allocated_welcome(welcome.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_MISSING);
  EXPECT_EQ(state_case, RmadState::StateCase::kWelcome);
}

TEST_F(WelcomeScreenStateHandlerTest, GetNextStateCase_Cancel) {
  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto welcome = std::make_unique<WelcomeState>();
  welcome->set_choice(WelcomeState::RMAD_CHOICE_CANCEL);
  RmadState state;
  state.set_allocated_welcome(welcome.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_RMA_NOT_REQUIRED);
  EXPECT_EQ(state_case, RmadState::StateCase::STATE_NOT_SET);
}

}  // namespace rmad
