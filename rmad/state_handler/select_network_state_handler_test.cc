// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>

#include <base/memory/scoped_refptr.h>
#include <gtest/gtest.h>

#include "rmad/constants.h"
#include "rmad/state_handler/select_network_state_handler.h"
#include "rmad/state_handler/state_handler_test_common.h"

namespace rmad {

class SelectNetworkStateHandlerTest : public StateHandlerTest {
 public:
  scoped_refptr<SelectNetworkStateHandler> CreateStateHandler() {
    return base::MakeRefCounted<SelectNetworkStateHandler>(json_store_);
  }
};

TEST_F(SelectNetworkStateHandlerTest, InitializeState_Success) {
  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
}

TEST_F(SelectNetworkStateHandlerTest, GetNextStateCase_Success_Connected) {
  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto select_network = std::make_unique<SelectNetworkState>();
  select_network->set_connection_state(
      SelectNetworkState::RMAD_NETWORK_CONNECTED);
  RmadState state;
  state.set_allocated_select_network(select_network.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateChrome);

  bool connected;
  EXPECT_TRUE(json_store_->GetValue(kNetworkConnected, &connected));
  EXPECT_TRUE(connected);
}

TEST_F(SelectNetworkStateHandlerTest, GetNextStateCase_Success_Disconnected) {
  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto select_network = std::make_unique<SelectNetworkState>();
  select_network->set_connection_state(
      SelectNetworkState::RMAD_NETWORK_USER_DECLINED);
  RmadState state;
  state.set_allocated_select_network(select_network.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateChrome);

  bool connected;
  EXPECT_TRUE(json_store_->GetValue(kNetworkConnected, &connected));
  EXPECT_FALSE(connected);
}

TEST_F(SelectNetworkStateHandlerTest, GetNextStateCase_MissingState) {
  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // No SelectNetworkState.
  RmadState state;

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kSelectNetwork);
}

TEST_F(SelectNetworkStateHandlerTest, GetNextStateCase_MissingArgs) {
  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto select_network = std::make_unique<SelectNetworkState>();
  select_network->set_connection_state(
      SelectNetworkState::RMAD_NETWORK_UNKNOWN);
  RmadState state;
  state.set_allocated_select_network(select_network.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_MISSING);
  EXPECT_EQ(state_case, RmadState::StateCase::kSelectNetwork);
}

}  // namespace rmad
