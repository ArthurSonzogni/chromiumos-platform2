// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>

#include <base/memory/scoped_refptr.h>
#include <gtest/gtest.h>

#include "rmad/state_handler/state_handler_test_common.h"
#include "rmad/state_handler/update_chrome_state_handler.h"

namespace rmad {

class UpdateChromeStateHandlerTest : public StateHandlerTest {
 public:
  scoped_refptr<UpdateChromeStateHandler> CreateStateHandler() {
    return base::MakeRefCounted<UpdateChromeStateHandler>(json_store_);
  }
};

TEST_F(UpdateChromeStateHandlerTest, InitializeState_Success) {
  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
}

TEST_F(UpdateChromeStateHandlerTest, GetNextStateCase_Success_Complete) {
  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto update_chrome = std::make_unique<UpdateChromeState>();
  update_chrome->set_update(UpdateChromeState::RMAD_UPDATE_STATE_COMPLETE);
  RmadState state;
  state.set_allocated_update_chrome(update_chrome.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kComponentsRepair);
}

TEST_F(UpdateChromeStateHandlerTest, GetNextStateCase_Success_Skip) {
  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto update_chrome = std::make_unique<UpdateChromeState>();
  update_chrome->set_update(UpdateChromeState::RMAD_UPDATE_STATE_SKIP);
  RmadState state;
  state.set_allocated_update_chrome(update_chrome.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kComponentsRepair);
}

TEST_F(UpdateChromeStateHandlerTest, GetNextStateCase_MissingState) {
  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // No UpdateChromeState.
  RmadState state;

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateChrome);
}

TEST_F(UpdateChromeStateHandlerTest, GetNextStateCase_MissingArgs) {
  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto update_chrome = std::make_unique<UpdateChromeState>();
  update_chrome->set_update(UpdateChromeState::RMAD_UPDATE_STATE_UNKNOWN);
  RmadState state;
  state.set_allocated_update_chrome(update_chrome.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_MISSING);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateChrome);
}

TEST_F(UpdateChromeStateHandlerTest, GetNextStateCase_WaitUpdate) {
  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto update_chrome = std::make_unique<UpdateChromeState>();
  update_chrome->set_update(UpdateChromeState::RMAD_UPDATE_STATE_UPDATE);
  RmadState state;
  state.set_allocated_update_chrome(update_chrome.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_TRANSITION_FAILED);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateChrome);
}

}  // namespace rmad
