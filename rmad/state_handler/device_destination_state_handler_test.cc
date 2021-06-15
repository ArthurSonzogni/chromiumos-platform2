// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>

#include <base/memory/scoped_refptr.h>
#include <gtest/gtest.h>

#include "rmad/constants.h"
#include "rmad/state_handler/device_destination_state_handler.h"
#include "rmad/state_handler/state_handler_test_common.h"

namespace rmad {

class DeviceDestinationStateHandlerTest : public StateHandlerTest {
 public:
  scoped_refptr<DeviceDestinationStateHandler> CreateStateHandler() {
    return base::MakeRefCounted<DeviceDestinationStateHandler>(json_store_);
  }
};

TEST_F(DeviceDestinationStateHandlerTest, InitializeState_Success) {
  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
}

TEST_F(DeviceDestinationStateHandlerTest, GetNextStateCase_Success_Same) {
  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto device_destination = std::make_unique<DeviceDestinationState>();
  device_destination->set_destination(
      DeviceDestinationState::RMAD_DESTINATION_SAME);
  RmadState state;
  state.set_allocated_device_destination(device_destination.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kWpDisableMethod);

  bool same_owner;
  EXPECT_TRUE(json_store_->GetValue(kSameOwner, &same_owner));
  EXPECT_TRUE(same_owner);
}

TEST_F(DeviceDestinationStateHandlerTest, GetNextStateCase_Success_Different) {
  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto device_destination = std::make_unique<DeviceDestinationState>();
  device_destination->set_destination(
      DeviceDestinationState::RMAD_DESTINATION_DIFFERENT);
  RmadState state;
  state.set_allocated_device_destination(device_destination.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kWpDisableMethod);

  bool same_owner;
  EXPECT_TRUE(json_store_->GetValue(kSameOwner, &same_owner));
  EXPECT_FALSE(same_owner);
}

TEST_F(DeviceDestinationStateHandlerTest, GetNextStateCase_MissingState) {
  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // No DeviceDestinationState.
  RmadState state;

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kDeviceDestination);
}

TEST_F(DeviceDestinationStateHandlerTest, GetNextStateCase_MissingArgs) {
  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto device_destination = std::make_unique<DeviceDestinationState>();
  device_destination->set_destination(
      DeviceDestinationState::RMAD_DESTINATION_UNKNOWN);
  RmadState state;
  state.set_allocated_device_destination(device_destination.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_MISSING);
  EXPECT_EQ(state_case, RmadState::StateCase::kDeviceDestination);
}

}  // namespace rmad
