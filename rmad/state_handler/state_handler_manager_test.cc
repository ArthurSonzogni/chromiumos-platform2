// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <base/files/file_path.h>
#include <base/memory/scoped_refptr.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rmad/state_handler/mock_state_handler.h"
#include "rmad/state_handler/state_handler_manager.h"
#include "rmad/utils/json_store.h"

namespace rmad {

using testing::Return;
using testing::StrictMock;

class StateHandlerManagerTest : public testing::Test {
 public:
  StateHandlerManagerTest()
      : json_store_(base::FilePath()), state_handler_manager_(&json_store_) {}

  scoped_refptr<BaseStateHandler> CreateMockStateHandler(RmadState state,
                                                         RmadState next_state) {
    auto handler =
        base::MakeRefCounted<StrictMock<MockStateHandler>>(&json_store_);
    EXPECT_CALL(*handler, GetState()).WillRepeatedly(Return(state));
    EXPECT_CALL(*handler, GetNextState()).WillRepeatedly(Return(next_state));
    return handler;
  }

 protected:
  JsonStore json_store_;
  StateHandlerManager state_handler_manager_;
};

TEST_F(StateHandlerManagerTest, GetStateHandler) {
  auto handler1 = CreateMockStateHandler(STATE_RMA_NOT_REQUIRED, STATE_UNKNOWN);
  auto handler2 = CreateMockStateHandler(STATE_WELCOME_SCREEN, STATE_UNKNOWN);
  state_handler_manager_.RegisterStateHandler(handler1);
  state_handler_manager_.RegisterStateHandler(handler2);

  scoped_refptr<BaseStateHandler> nonexistent_handler =
      state_handler_manager_.GetStateHandler(STATE_UNKNOWN);
  EXPECT_FALSE(nonexistent_handler.get());

  scoped_refptr<BaseStateHandler> retrieve_handler =
      state_handler_manager_.GetStateHandler(STATE_WELCOME_SCREEN);
  EXPECT_TRUE(retrieve_handler.get());
  EXPECT_EQ(STATE_WELCOME_SCREEN, retrieve_handler->GetState());
  EXPECT_EQ(STATE_UNKNOWN, retrieve_handler->GetNextState());
}

TEST_F(StateHandlerManagerTest, RegisterStateHandlerCollision) {
  auto handler1 = CreateMockStateHandler(STATE_RMA_NOT_REQUIRED, STATE_UNKNOWN);
  auto handler2 =
      CreateMockStateHandler(STATE_RMA_NOT_REQUIRED, STATE_WELCOME_SCREEN);
  state_handler_manager_.RegisterStateHandler(handler1);
  EXPECT_DEATH(state_handler_manager_.RegisterStateHandler(handler2),
               "Registered handlers should have unique RmadStates.");
}

}  // namespace rmad
