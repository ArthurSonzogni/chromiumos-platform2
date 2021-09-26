// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/setup_calibration_state_handler.h"

#include <map>
#include <memory>
#include <string>

#include <base/strings/string_number_conversions.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rmad/state_handler/state_handler_test_common.h"
#include "rmad/utils/calibration_utils.h"

using testing::_;
using testing::Assign;
using testing::DoAll;
using testing::Return;
using testing::StrictMock;

namespace rmad {

class SetupCalibrationStateHandlerTest : public StateHandlerTest {
 public:
  // Helper class to mock the callback function to send signal.
  class SignalSender {
   public:
    MOCK_METHOD(bool,
                SendCalibrationSetupSignal,
                (CalibrationSetupInstruction),
                (const));
  };

  scoped_refptr<SetupCalibrationStateHandler> CreateStateHandler() {
    auto handler =
        base::MakeRefCounted<SetupCalibrationStateHandler>(json_store_);

    return handler;
  }
};

TEST_F(SetupCalibrationStateHandlerTest, InitializeState_Success) {
  const std::map<std::string, std::map<std::string, std::string>>
      predefined_calibration_map = {
          {CalibrationSetupInstruction_Name(
               RMAD_CALIBRATION_INSTRUCTION_PLACE_BASE_ON_FLAT_SURFACE),
           {{RmadComponent_Name(
                 RmadComponent::RMAD_COMPONENT_BASE_ACCELEROMETER),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_WAITING)},
            {RmadComponent_Name(RmadComponent::RMAD_COMPONENT_GYROSCOPE),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_WAITING)}}},
          {CalibrationSetupInstruction_Name(
               RMAD_CALIBRATION_INSTRUCTION_PLACE_LID_ON_FLAT_SURFACE),
           {{RmadComponent_Name(
                 RmadComponent::RMAD_COMPONENT_LID_ACCELEROMETER),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_WAITING)}}}};
  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationMap, predefined_calibration_map));

  auto handler = CreateStateHandler();

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
}

TEST_F(SetupCalibrationStateHandlerTest, InitializeState_Failed) {
  auto handler = CreateStateHandler();

  EXPECT_EQ(handler->InitializeState(),
            RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED);
}

TEST_F(SetupCalibrationStateHandlerTest, GetNextStateCase_Success) {
  const std::map<std::string, std::map<std::string, std::string>>
      predefined_calibration_map = {
          {CalibrationSetupInstruction_Name(
               RMAD_CALIBRATION_INSTRUCTION_PLACE_BASE_ON_FLAT_SURFACE),
           {{RmadComponent_Name(
                 RmadComponent::RMAD_COMPONENT_BASE_ACCELEROMETER),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_WAITING)},
            {RmadComponent_Name(RmadComponent::RMAD_COMPONENT_GYROSCOPE),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_WAITING)}}},
          {CalibrationSetupInstruction_Name(
               RMAD_CALIBRATION_INSTRUCTION_PLACE_LID_ON_FLAT_SURFACE),
           {{RmadComponent_Name(
                 RmadComponent::RMAD_COMPONENT_LID_ACCELEROMETER),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_WAITING)}}}};
  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationMap, predefined_calibration_map));

  auto handler = CreateStateHandler();

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  RmadState state;
  auto setup_calibration_state = std::make_unique<SetupCalibrationState>();
  setup_calibration_state->set_instruction(
      RMAD_CALIBRATION_INSTRUCTION_PLACE_BASE_ON_FLAT_SURFACE);
  state.set_allocated_setup_calibration(setup_calibration_state.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kRunCalibration);
}

TEST_F(SetupCalibrationStateHandlerTest, GetNextStateCase_MissingState) {
  const std::map<std::string, std::map<std::string, std::string>>
      predefined_calibration_map = {
          {CalibrationSetupInstruction_Name(
               RMAD_CALIBRATION_INSTRUCTION_PLACE_BASE_ON_FLAT_SURFACE),
           {{RmadComponent_Name(
                 RmadComponent::RMAD_COMPONENT_BASE_ACCELEROMETER),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_WAITING)},
            {RmadComponent_Name(RmadComponent::RMAD_COMPONENT_GYROSCOPE),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_WAITING)}}},
          {CalibrationSetupInstruction_Name(
               RMAD_CALIBRATION_INSTRUCTION_PLACE_LID_ON_FLAT_SURFACE),
           {{RmadComponent_Name(
                 RmadComponent::RMAD_COMPONENT_LID_ACCELEROMETER),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_WAITING)}}}};
  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationMap, predefined_calibration_map));

  auto handler = CreateStateHandler();

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // No SetupCalibrationState.
  RmadState state;

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kSetupCalibration);
}

TEST_F(SetupCalibrationStateHandlerTest,
       GetNextStateCaseWithSetup_ReadOnlyInstructionChanged) {
  const std::map<std::string, std::map<std::string, std::string>>
      predefined_calibration_map = {
          {CalibrationSetupInstruction_Name(
               RMAD_CALIBRATION_INSTRUCTION_PLACE_BASE_ON_FLAT_SURFACE),
           {{RmadComponent_Name(
                 RmadComponent::RMAD_COMPONENT_BASE_ACCELEROMETER),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_WAITING)},
            {RmadComponent_Name(RmadComponent::RMAD_COMPONENT_GYROSCOPE),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_WAITING)}}},
          {CalibrationSetupInstruction_Name(
               RMAD_CALIBRATION_INSTRUCTION_PLACE_LID_ON_FLAT_SURFACE),
           {{RmadComponent_Name(
                 RmadComponent::RMAD_COMPONENT_LID_ACCELEROMETER),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_WAITING)}}}};
  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationMap, predefined_calibration_map));

  auto handler = CreateStateHandler();

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  RmadState state;
  auto setup_calibration_state = std::make_unique<SetupCalibrationState>();
  setup_calibration_state->set_instruction(
      RMAD_CALIBRATION_INSTRUCTION_PLACE_LID_ON_FLAT_SURFACE);
  state.set_allocated_setup_calibration(setup_calibration_state.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kSetupCalibration);
}

}  // namespace rmad
