// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/check_calibration_state_handler.h"

#include <map>
#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "rmad/state_handler/state_handler_test_common.h"

namespace rmad {

class CheckCalibrationStateHandlerTest : public StateHandlerTest {
 public:
  scoped_refptr<CheckCalibrationStateHandler> CreateStateHandler() {
    return base::MakeRefCounted<CheckCalibrationStateHandler>(json_store_);
  }

 protected:
  void SetUp() override {
    StateHandlerTest::SetUp();
    base_acc_setup_instruction = GetCalibrationSetupInstruction(
        RmadComponent::RMAD_COMPONENT_BASE_ACCELEROMETER);
    EXPECT_GT(base_acc_setup_instruction, RMAD_CALIBRATION_INSTRUCTION_UNKNOWN);

    lid_acc_setup_instruction = GetCalibrationSetupInstruction(
        RmadComponent::RMAD_COMPONENT_LID_ACCELEROMETER);
    EXPECT_GT(lid_acc_setup_instruction, RMAD_CALIBRATION_INSTRUCTION_UNKNOWN);

    EXPECT_NE(base_acc_setup_instruction, lid_acc_setup_instruction);
  }

  CalibrationSetupInstruction base_acc_setup_instruction;
  CalibrationSetupInstruction lid_acc_setup_instruction;
};

TEST_F(CheckCalibrationStateHandlerTest, InitializeState_Success) {
  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
}

TEST_F(CheckCalibrationStateHandlerTest,
       GetNextStateCase_NeedCalibration_Success) {
  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  std::unique_ptr<CheckCalibrationState> check_calibration =
      std::make_unique<CheckCalibrationState>();
  auto base_accelerometer = check_calibration->add_calibration_components();
  base_accelerometer->set_component(
      RmadComponent::RMAD_COMPONENT_BASE_ACCELEROMETER);
  base_accelerometer->set_status(
      CalibrationComponentStatus::RMAD_CALIBRATION_WAITING);
  auto lid_accelerometer = check_calibration->add_calibration_components();
  lid_accelerometer->set_component(
      RmadComponent::RMAD_COMPONENT_LID_ACCELEROMETER);
  lid_accelerometer->set_status(
      CalibrationComponentStatus::RMAD_CALIBRATION_WAITING);
  auto gyroscope = check_calibration->add_calibration_components();
  gyroscope->set_component(RmadComponent::RMAD_COMPONENT_GYROSCOPE);
  gyroscope->set_status(CalibrationComponentStatus::RMAD_CALIBRATION_WAITING);

  RmadState state;
  state.set_allocated_check_calibration(check_calibration.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kSetupCalibration);

  std::map<std::string, std::map<std::string, std::string>>
      priority_calibration_map;
  EXPECT_TRUE(
      json_store_->GetValue(kCalibrationMap, &priority_calibration_map));

  const std::map<std::string, std::map<std::string, std::string>>
      target_priority_calibration_map = {
          {CalibrationSetupInstruction_Name(base_acc_setup_instruction),
           {{RmadComponent_Name(
                 RmadComponent::RMAD_COMPONENT_BASE_ACCELEROMETER),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_WAITING)},
            {RmadComponent_Name(RmadComponent::RMAD_COMPONENT_GYROSCOPE),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_WAITING)}}},
          {CalibrationSetupInstruction_Name(lid_acc_setup_instruction),
           {{RmadComponent_Name(
                 RmadComponent::RMAD_COMPONENT_LID_ACCELEROMETER),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_WAITING)}}}};

  EXPECT_EQ(priority_calibration_map, target_priority_calibration_map);
}

TEST_F(CheckCalibrationStateHandlerTest,
       GetNextStateCase_RetryCalibration_Success) {
  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  std::unique_ptr<CheckCalibrationState> check_calibration =
      std::make_unique<CheckCalibrationState>();
  auto base_accelerometer = check_calibration->add_calibration_components();
  base_accelerometer->set_component(
      RmadComponent::RMAD_COMPONENT_BASE_ACCELEROMETER);
  base_accelerometer->set_status(
      CalibrationComponentStatus::RMAD_CALIBRATION_FAILED);
  auto lid_accelerometer = check_calibration->add_calibration_components();
  lid_accelerometer->set_component(
      RmadComponent::RMAD_COMPONENT_LID_ACCELEROMETER);
  lid_accelerometer->set_status(
      CalibrationComponentStatus::RMAD_CALIBRATION_IN_PROGRESS);
  auto gyroscope = check_calibration->add_calibration_components();
  gyroscope->set_component(RmadComponent::RMAD_COMPONENT_GYROSCOPE);
  gyroscope->set_status(CalibrationComponentStatus::RMAD_CALIBRATION_COMPLETE);

  RmadState state;
  state.set_allocated_check_calibration(check_calibration.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kSetupCalibration);

  std::map<std::string, std::map<std::string, std::string>>
      priority_calibration_map;
  EXPECT_TRUE(
      json_store_->GetValue(kCalibrationMap, &priority_calibration_map));

  const std::map<std::string, std::map<std::string, std::string>>
      target_priority_calibration_map = {
          {CalibrationSetupInstruction_Name(base_acc_setup_instruction),
           {{RmadComponent_Name(
                 RmadComponent::RMAD_COMPONENT_BASE_ACCELEROMETER),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_FAILED)},
            {RmadComponent_Name(RmadComponent::RMAD_COMPONENT_GYROSCOPE),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_COMPLETE)}}},
          {CalibrationSetupInstruction_Name(lid_acc_setup_instruction),
           {{RmadComponent_Name(
                 RmadComponent::RMAD_COMPONENT_LID_ACCELEROMETER),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_IN_PROGRESS)}}}};

  EXPECT_EQ(priority_calibration_map, target_priority_calibration_map);
}

TEST_F(CheckCalibrationStateHandlerTest,
       GetNextStateCase_NoNeedCalibration_Success) {
  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  std::unique_ptr<CheckCalibrationState> check_calibration =
      std::make_unique<CheckCalibrationState>();
  auto base_accelerometer = check_calibration->add_calibration_components();
  base_accelerometer->set_component(
      RmadComponent::RMAD_COMPONENT_BASE_ACCELEROMETER);
  base_accelerometer->set_status(
      CalibrationComponentStatus::RMAD_CALIBRATION_COMPLETE);
  auto lid_accelerometer = check_calibration->add_calibration_components();
  lid_accelerometer->set_component(
      RmadComponent::RMAD_COMPONENT_LID_ACCELEROMETER);
  lid_accelerometer->set_status(
      CalibrationComponentStatus::RMAD_CALIBRATION_COMPLETE);
  auto gyroscope = check_calibration->add_calibration_components();
  gyroscope->set_component(RmadComponent::RMAD_COMPONENT_GYROSCOPE);
  gyroscope->set_status(CalibrationComponentStatus::RMAD_CALIBRATION_SKIP);

  RmadState state;
  state.set_allocated_check_calibration(check_calibration.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kProvisionDevice);

  std::map<std::string, std::map<std::string, std::string>>
      priority_calibration_map;
  EXPECT_TRUE(
      json_store_->GetValue(kCalibrationMap, &priority_calibration_map));

  const std::map<std::string, std::map<std::string, std::string>>
      target_priority_calibration_map = {
          {CalibrationSetupInstruction_Name(base_acc_setup_instruction),
           {{RmadComponent_Name(
                 RmadComponent::RMAD_COMPONENT_BASE_ACCELEROMETER),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_COMPLETE)},
            {RmadComponent_Name(RmadComponent::RMAD_COMPONENT_GYROSCOPE),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_SKIP)}}},
          {CalibrationSetupInstruction_Name(lid_acc_setup_instruction),
           {{RmadComponent_Name(
                 RmadComponent::RMAD_COMPONENT_LID_ACCELEROMETER),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_COMPLETE)}}}};

  EXPECT_EQ(priority_calibration_map, target_priority_calibration_map);
}

TEST_F(CheckCalibrationStateHandlerTest, GetNextStateCase_MissingState) {
  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // No CheckCalibrationState.
  RmadState state;

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kCheckCalibration);
}

TEST_F(CheckCalibrationStateHandlerTest, GetNextStateCase_UnknownComponent) {
  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  std::unique_ptr<CheckCalibrationState> check_calibration =
      std::make_unique<CheckCalibrationState>();
  auto unknown = check_calibration->add_calibration_components();
  unknown->set_component(RmadComponent::RMAD_COMPONENT_UNKNOWN);
  unknown->set_status(CalibrationComponentStatus::RMAD_CALIBRATION_WAITING);

  RmadState state;
  state.set_allocated_check_calibration(check_calibration.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_MISSING);
  EXPECT_EQ(state_case, RmadState::StateCase::kCheckCalibration);
}

TEST_F(CheckCalibrationStateHandlerTest, GetNextStateCase_UnknownStatus) {
  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  std::unique_ptr<CheckCalibrationState> check_calibration =
      std::make_unique<CheckCalibrationState>();
  auto base_accelerometer = check_calibration->add_calibration_components();
  base_accelerometer->set_component(
      RmadComponent::RMAD_COMPONENT_BASE_ACCELEROMETER);
  base_accelerometer->set_status(
      CalibrationComponentStatus::RMAD_CALIBRATION_UNKNOWN);

  RmadState state;
  state.set_allocated_check_calibration(check_calibration.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_MISSING);
  EXPECT_EQ(state_case, RmadState::StateCase::kCheckCalibration);
}

}  // namespace rmad
