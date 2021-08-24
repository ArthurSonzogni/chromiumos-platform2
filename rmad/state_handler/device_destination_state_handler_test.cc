// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/memory/scoped_refptr.h>
#include <gtest/gtest.h>

#include "rmad/constants.h"
#include "rmad/state_handler/device_destination_state_handler.h"
#include "rmad/state_handler/state_handler_test_common.h"
#include "rmad/utils/mock_cr50_utils.h"

using testing::NiceMock;
using testing::Return;

namespace rmad {

using ComponentRepairStatus = ComponentsRepairState::ComponentRepairStatus;

class DeviceDestinationStateHandlerTest : public StateHandlerTest {
 public:
  scoped_refptr<DeviceDestinationStateHandler> CreateStateHandler(
      bool factory_mode_enabled) {
    // Mock |Cr50Utils|.
    auto mock_cr50_utils = std::make_unique<NiceMock<MockCr50Utils>>();
    ON_CALL(*mock_cr50_utils, IsFactoryModeEnabled())
        .WillByDefault(Return(factory_mode_enabled));

    return base::MakeRefCounted<DeviceDestinationStateHandler>(
        json_store_, std::move(mock_cr50_utils));
  }
};

TEST_F(DeviceDestinationStateHandlerTest, InitializeState_Success) {
  auto handler = CreateStateHandler(false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
}

TEST_F(DeviceDestinationStateHandlerTest,
       GetNextStateCase_Success_Same_NeedCalibration_FactoryModeEnabled) {
  auto handler = CreateStateHandler(true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  json_store_->SetValue(
      kReplacedComponentNames,
      std::vector<RmadComponent>{RMAD_COMPONENT_MAINBOARD_REWORK});

  auto device_destination = std::make_unique<DeviceDestinationState>();
  device_destination->set_destination(
      DeviceDestinationState::RMAD_DESTINATION_SAME);
  RmadState state;
  state.set_allocated_device_destination(device_destination.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kWpDisableComplete);

  bool same_owner;
  EXPECT_TRUE(json_store_->GetValue(kSameOwner, &same_owner));
  EXPECT_TRUE(same_owner);
}

TEST_F(DeviceDestinationStateHandlerTest,
       GetNextStateCase_Success_Same_NeedCalibration_FactoryModeDisabled) {
  auto handler = CreateStateHandler(false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  json_store_->SetValue(
      kReplacedComponentNames,
      std::vector<RmadComponent>{RMAD_COMPONENT_MAINBOARD_REWORK});

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

TEST_F(DeviceDestinationStateHandlerTest,
       GetNextStateCase_Success_Same_NoCalibration_FactoryModeEnabled) {
  auto handler = CreateStateHandler(true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  json_store_->SetValue(
      kReplacedComponentNames,
      std::vector<std::string>{RmadComponent_Name(RMAD_COMPONENT_BATTERY)});

  auto device_destination = std::make_unique<DeviceDestinationState>();
  device_destination->set_destination(
      DeviceDestinationState::RMAD_DESTINATION_SAME);
  RmadState state;
  state.set_allocated_device_destination(device_destination.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kFinalize);

  bool same_owner;
  EXPECT_TRUE(json_store_->GetValue(kSameOwner, &same_owner));
  EXPECT_TRUE(same_owner);
}

TEST_F(DeviceDestinationStateHandlerTest,
       GetNextStateCase_Success_Same_NoCalibration_FactoryModeDisabled) {
  auto handler = CreateStateHandler(false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  json_store_->SetValue(
      kReplacedComponentNames,
      std::vector<std::string>{RmadComponent_Name(RMAD_COMPONENT_BATTERY)});

  auto device_destination = std::make_unique<DeviceDestinationState>();
  device_destination->set_destination(
      DeviceDestinationState::RMAD_DESTINATION_SAME);
  RmadState state;
  state.set_allocated_device_destination(device_destination.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kFinalize);

  bool same_owner;
  EXPECT_TRUE(json_store_->GetValue(kSameOwner, &same_owner));
  EXPECT_TRUE(same_owner);
}

TEST_F(DeviceDestinationStateHandlerTest,
       GetNextStateCase_Success_Different_NeedCalibration_FactoryModeEnabled) {
  auto handler = CreateStateHandler(true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  json_store_->SetValue(kReplacedComponentNames,
                        std::vector<std::string>{RmadComponent_Name(
                            RMAD_COMPONENT_MAINBOARD_REWORK)});

  auto device_destination = std::make_unique<DeviceDestinationState>();
  device_destination->set_destination(
      DeviceDestinationState::RMAD_DESTINATION_DIFFERENT);
  RmadState state;
  state.set_allocated_device_destination(device_destination.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kWpDisableComplete);

  bool same_owner;
  EXPECT_TRUE(json_store_->GetValue(kSameOwner, &same_owner));
  EXPECT_FALSE(same_owner);
}

TEST_F(DeviceDestinationStateHandlerTest,
       GetNextStateCase_Success_Different_NeedCalibration_FactoryModeDisabled) {
  auto handler = CreateStateHandler(false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  json_store_->SetValue(kReplacedComponentNames,
                        std::vector<std::string>{RmadComponent_Name(
                            RMAD_COMPONENT_MAINBOARD_REWORK)});

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

TEST_F(DeviceDestinationStateHandlerTest,
       GetNextStateCase_Success_Different_NoCalibration_FactoryModeEnabled) {
  auto handler = CreateStateHandler(true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  json_store_->SetValue(kReplacedComponentNames,
                        std::vector<std::string>{RmadComponent_Name(
                            RMAD_COMPONENT_MAINBOARD_REWORK)});

  auto device_destination = std::make_unique<DeviceDestinationState>();
  device_destination->set_destination(
      DeviceDestinationState::RMAD_DESTINATION_DIFFERENT);
  RmadState state;
  state.set_allocated_device_destination(device_destination.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kWpDisableComplete);

  bool same_owner;
  EXPECT_TRUE(json_store_->GetValue(kSameOwner, &same_owner));
  EXPECT_FALSE(same_owner);
}

TEST_F(DeviceDestinationStateHandlerTest,
       GetNextStateCase_Success_Different_NoCalibration_FactoryModeDisabled) {
  auto handler = CreateStateHandler(false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  json_store_->SetValue(kReplacedComponentNames,
                        std::vector<std::string>{RmadComponent_Name(
                            RMAD_COMPONENT_MAINBOARD_REWORK)});

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
  auto handler = CreateStateHandler(false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // No DeviceDestinationState.
  RmadState state;

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kDeviceDestination);
}

TEST_F(DeviceDestinationStateHandlerTest, GetNextStateCase_MissingArgs) {
  auto handler = CreateStateHandler(false);
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
