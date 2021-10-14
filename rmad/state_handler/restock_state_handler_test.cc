// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include <base/memory/scoped_refptr.h>
#include <base/test/task_environment.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rmad/state_handler/restock_state_handler.h"
#include "rmad/state_handler/state_handler_test_common.h"
#include "rmad/system/mock_power_manager_client.h"

using testing::_;
using testing::Assign;
using testing::DoAll;
using testing::NiceMock;
using testing::Return;

namespace rmad {

class RestockStateHandlerTest : public StateHandlerTest {
 public:
  scoped_refptr<RestockStateHandler> CreateStateHandler(
      bool* shutdown_called = nullptr) {
    auto mock_power_manager_client =
        std::make_unique<NiceMock<MockPowerManagerClient>>();
    if (shutdown_called) {
      ON_CALL(*mock_power_manager_client, Shutdown())
          .WillByDefault(DoAll(Assign(shutdown_called, true), Return(true)));
    } else {
      ON_CALL(*mock_power_manager_client, Shutdown())
          .WillByDefault(Return(true));
    }
    return base::MakeRefCounted<RestockStateHandler>(
        json_store_, std::move(mock_power_manager_client));
  }

 protected:
  // Variables for TaskRunner.
  base::test::SingleThreadTaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
};

TEST_F(RestockStateHandlerTest, InitializeState_Success) {
  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
}

TEST_F(RestockStateHandlerTest, GetNextStateCase_Success_Shutdown) {
  bool shutdown_called = false;
  auto handler = CreateStateHandler(&shutdown_called);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto restock = std::make_unique<RestockState>();
  restock->set_choice(RestockState::RMAD_RESTOCK_SHUTDOWN_AND_RESTOCK);
  RmadState state;
  state.set_allocated_restock(restock.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_EXPECT_SHUTDOWN);
  EXPECT_EQ(state_case, RmadState::StateCase::kRestock);
  EXPECT_FALSE(shutdown_called);

  // Shutdown is called after a delay.
  task_environment_.FastForwardBy(RestockStateHandler::kShutdownDelay);
  EXPECT_TRUE(shutdown_called);

  // Test behavior for next bootup
  shutdown_called = false;
  // GetNextStateCase is called to automatically transition, we should test the
  // behavior here.
  auto [error_next_bootup, state_case_next_bootup] =
      handler->GetNextStateCase(handler->GetState());
  EXPECT_EQ(error_next_bootup, RMAD_ERROR_REQUEST_ARGS_MISSING);
  EXPECT_EQ(state_case_next_bootup, RmadState::StateCase::kRestock);

  // Shutdown should not be called again at the next bootup.
  task_environment_.FastForwardUntilNoTasksRemain();
  EXPECT_FALSE(shutdown_called);
}

TEST_F(RestockStateHandlerTest, GetNextStateCase_Success_Continue) {
  bool shutdown_called = false;
  auto handler = CreateStateHandler(&shutdown_called);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto restock = std::make_unique<RestockState>();
  restock->set_choice(RestockState::RMAD_RESTOCK_CONTINUE_RMA);
  RmadState state;
  state.set_allocated_restock(restock.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
  EXPECT_FALSE(shutdown_called);

  // Nothing should happen.
  task_environment_.FastForwardBy(RestockStateHandler::kShutdownDelay);
  EXPECT_FALSE(shutdown_called);
}

TEST_F(RestockStateHandlerTest, GetNextStateCase_Success_Shutdown_Shutdown) {
  bool shutdown_called = false;
  auto handler = CreateStateHandler(&shutdown_called);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto restock = std::make_unique<RestockState>();
  restock->set_choice(RestockState::RMAD_RESTOCK_SHUTDOWN_AND_RESTOCK);
  RmadState state;
  state.set_allocated_restock(restock.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_EXPECT_SHUTDOWN);
  EXPECT_EQ(state_case, RmadState::StateCase::kRestock);
  EXPECT_FALSE(shutdown_called);

  // Shutdown is called after a delay.
  task_environment_.FastForwardBy(RestockStateHandler::kShutdownDelay);
  EXPECT_TRUE(shutdown_called);

  // Test behavior for next bootup
  shutdown_called = false;
  // GetNextStateCase is called to automatically transition, we should test the
  // behavior here.
  auto [error_next_bootup, state_case_next_bootup] =
      handler->GetNextStateCase(handler->GetState());
  EXPECT_EQ(error_next_bootup, RMAD_ERROR_REQUEST_ARGS_MISSING);
  EXPECT_EQ(state_case_next_bootup, RmadState::StateCase::kRestock);

  // Shutdown should not be called again at the next bootup.
  task_environment_.FastForwardUntilNoTasksRemain();
  EXPECT_FALSE(shutdown_called);

  // Test behavior for next call
  shutdown_called = false;
  auto [error_next_call, state_case_next_call] =
      handler->GetNextStateCase(state);
  EXPECT_EQ(error_next_call, RMAD_ERROR_EXPECT_SHUTDOWN);
  EXPECT_EQ(state_case_next_call, RmadState::StateCase::kRestock);
  EXPECT_FALSE(shutdown_called);

  // Shutdown is called after a delay.
  task_environment_.FastForwardBy(RestockStateHandler::kShutdownDelay);
  EXPECT_TRUE(shutdown_called);
}

TEST_F(RestockStateHandlerTest, GetNextStateCase_Success_Shutdown_Continue) {
  bool shutdown_called = false;
  auto handler = CreateStateHandler(&shutdown_called);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto restock = std::make_unique<RestockState>();
  restock->set_choice(RestockState::RMAD_RESTOCK_SHUTDOWN_AND_RESTOCK);
  RmadState state;
  state.set_allocated_restock(restock.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_EXPECT_SHUTDOWN);
  EXPECT_EQ(state_case, RmadState::StateCase::kRestock);
  EXPECT_FALSE(shutdown_called);

  // Shutdown is called after a delay.
  task_environment_.FastForwardBy(RestockStateHandler::kShutdownDelay);
  EXPECT_TRUE(shutdown_called);

  // Test behavior for next bootup
  shutdown_called = false;
  // GetNextStateCase is called to automatically transition, we should test the
  // behavior here.
  auto [error_next_bootup, state_case_next_bootup] =
      handler->GetNextStateCase(handler->GetState());
  EXPECT_EQ(error_next_bootup, RMAD_ERROR_REQUEST_ARGS_MISSING);
  EXPECT_EQ(state_case_next_bootup, RmadState::StateCase::kRestock);

  // Shutdown should not be called again at the next bootup.
  task_environment_.FastForwardUntilNoTasksRemain();
  EXPECT_FALSE(shutdown_called);

  // Test behavior for next call
  shutdown_called = false;
  restock = std::make_unique<RestockState>();
  restock->set_choice(RestockState::RMAD_RESTOCK_CONTINUE_RMA);
  state.set_allocated_restock(restock.release());

  auto [error_next_call, state_case_next_call] =
      handler->GetNextStateCase(state);
  EXPECT_EQ(error_next_call, RMAD_ERROR_OK);
  EXPECT_EQ(state_case_next_call, RmadState::StateCase::kUpdateDeviceInfo);
  EXPECT_FALSE(shutdown_called);

  // Nothing should happen.
  task_environment_.FastForwardUntilNoTasksRemain();
  EXPECT_FALSE(shutdown_called);
}

TEST_F(RestockStateHandlerTest, GetNextStateCase_MissingState) {
  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // No RestockState.
  RmadState state;

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kRestock);
}

TEST_F(RestockStateHandlerTest, GetNextStateCase_MissingArgs) {
  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto restock = std::make_unique<RestockState>();
  restock->set_choice(RestockState::RMAD_RESTOCK_UNKNOWN);
  RmadState state;
  state.set_allocated_restock(restock.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_MISSING);
  EXPECT_EQ(state_case, RmadState::StateCase::kRestock);
}

}  // namespace rmad
