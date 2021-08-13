// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/run_calibration_state_handler.h"

#include <map>
#include <memory>
#include <string>

#include <base/test/task_environment.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rmad/state_handler/state_handler_test_common.h"
#include "rmad/utils/calibration_utils.h"

using testing::_;
using testing::Assign;
using testing::DoAll;
using testing::IsFalse;
using testing::Return;
using testing::StrictMock;

namespace rmad {

class RunCalibrationStateHandlerTest : public StateHandlerTest {
 public:
  // Helper class to mock the callback function to send signal.
  class SignalSender {
   public:
    MOCK_METHOD(bool,
                SendCalibrationOverallSignal,
                (CalibrationOverallStatus),
                (const));

    MOCK_METHOD(bool,
                SendCalibrationProgressSignal,
                (CalibrationComponentStatus),
                (const));
  };

  scoped_refptr<RunCalibrationStateHandler> CreateStateHandler() {
    auto handler =
        base::MakeRefCounted<RunCalibrationStateHandler>(json_store_);

    auto callback_overall = std::make_unique<
        base::RepeatingCallback<bool(CalibrationOverallStatus)>>(
        base::BindRepeating(
            &SignalSender::SendCalibrationOverallSignal,
            base::Unretained(&signal_calibration_overall_sender_)));
    handler->RegisterSignalSender(std::move(callback_overall));

    auto callback_component = std::make_unique<
        base::RepeatingCallback<bool(CalibrationComponentStatus)>>(
        base::BindRepeating(
            &SignalSender::SendCalibrationProgressSignal,
            base::Unretained(&signal_calibration_component_sender_)));
    handler->RegisterSignalSender(std::move(callback_component));

    return handler;
  }

 protected:
  StrictMock<SignalSender> signal_calibration_component_sender_;
  StrictMock<SignalSender> signal_calibration_overall_sender_;

  // Variables for TaskRunner.
  base::test::SingleThreadTaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};

  void SetUp() override {
    StateHandlerTest::SetUp();
    base_acc_setup_instruction = GetCalibrationSetupInstruction(
        RmadComponent::RMAD_COMPONENT_BASE_ACCELEROMETER);
    lid_acc_setup_instruction = GetCalibrationSetupInstruction(
        RmadComponent::RMAD_COMPONENT_LID_ACCELEROMETER);

    EXPECT_NE(base_acc_setup_instruction, RMAD_CALIBRATION_INSTRUCTION_UNKNOWN);
    EXPECT_NE(lid_acc_setup_instruction, RMAD_CALIBRATION_INSTRUCTION_UNKNOWN);
    EXPECT_NE(base_acc_setup_instruction, lid_acc_setup_instruction);
  }

  CalibrationSetupInstruction base_acc_setup_instruction;
  CalibrationSetupInstruction lid_acc_setup_instruction;
};

TEST_F(RunCalibrationStateHandlerTest, InitializeState_Success) {
  bool signal_sent = false;
  EXPECT_CALL(signal_calibration_overall_sender_,
              SendCalibrationOverallSignal(
                  RMAD_CALIBRATION_OVERALL_INITIALIZATION_FAILED))
      .WillOnce(DoAll(Assign(&signal_sent, true), Return(true)));

  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  EXPECT_TRUE(signal_sent);
}

TEST_F(RunCalibrationStateHandlerTest,
       GetNextStateCase_NeedCalibration_Success) {
  const std::map<std::string, std::map<std::string, std::string>>
      predefined_calibration_map = {
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

  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationMap, predefined_calibration_map));

  bool signal_sent = false;
  EXPECT_CALL(signal_calibration_component_sender_,
              SendCalibrationProgressSignal(_))
      .WillRepeatedly(DoAll(Assign(&signal_sent, true), Return(true)));

  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  task_environment_.FastForwardBy(RunCalibrationStateHandler::kPollInterval);
  EXPECT_TRUE(signal_sent);

  std::map<std::string, std::map<std::string, std::string>>
      current_calibration_map;
  EXPECT_TRUE(json_store_->GetValue(kCalibrationMap, &current_calibration_map));

  const std::map<std::string, std::map<std::string, std::string>>
      target_calibration_map = {
          {CalibrationSetupInstruction_Name(base_acc_setup_instruction),
           {{RmadComponent_Name(
                 RmadComponent::RMAD_COMPONENT_BASE_ACCELEROMETER),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_IN_PROGRESS)},
            {RmadComponent_Name(RmadComponent::RMAD_COMPONENT_GYROSCOPE),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_IN_PROGRESS)}}},
          {CalibrationSetupInstruction_Name(lid_acc_setup_instruction),
           {{RmadComponent_Name(
                 RmadComponent::RMAD_COMPONENT_LID_ACCELEROMETER),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_WAITING)}}}};

  EXPECT_EQ(current_calibration_map, target_calibration_map);
}

TEST_F(RunCalibrationStateHandlerTest,
       GetNextStateCase_RetryCalibration_Success) {
  const std::map<std::string, std::map<std::string, std::string>>
      predefined_calibration_map = {
          {CalibrationSetupInstruction_Name(base_acc_setup_instruction),
           {{RmadComponent_Name(
                 RmadComponent::RMAD_COMPONENT_BASE_ACCELEROMETER),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_FAILED)},
            {RmadComponent_Name(RmadComponent::RMAD_COMPONENT_GYROSCOPE),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_SKIP)}}},
          {CalibrationSetupInstruction_Name(lid_acc_setup_instruction),
           {{RmadComponent_Name(
                 RmadComponent::RMAD_COMPONENT_LID_ACCELEROMETER),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_COMPLETE)}}}};

  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationMap, predefined_calibration_map));

  bool signal_sent = false;
  EXPECT_CALL(signal_calibration_component_sender_,
              SendCalibrationProgressSignal(_))
      .WillRepeatedly(DoAll(Assign(&signal_sent, true), Return(true)));

  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  task_environment_.FastForwardBy(RunCalibrationStateHandler::kPollInterval);
  EXPECT_TRUE(signal_sent);

  std::map<std::string, std::map<std::string, std::string>>
      current_calibration_map;
  EXPECT_TRUE(json_store_->GetValue(kCalibrationMap, &current_calibration_map));

  const std::map<std::string, std::map<std::string, std::string>>
      target_calibration_map = {
          {CalibrationSetupInstruction_Name(base_acc_setup_instruction),
           {{RmadComponent_Name(
                 RmadComponent::RMAD_COMPONENT_BASE_ACCELEROMETER),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_IN_PROGRESS)},
            {RmadComponent_Name(RmadComponent::RMAD_COMPONENT_GYROSCOPE),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_SKIP)}}},
          {CalibrationSetupInstruction_Name(lid_acc_setup_instruction),
           {{RmadComponent_Name(
                 RmadComponent::RMAD_COMPONENT_LID_ACCELEROMETER),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_COMPLETE)}}}};

  EXPECT_EQ(current_calibration_map, target_calibration_map);
}

TEST_F(RunCalibrationStateHandlerTest,
       GetNextStateCase_NoNeedCalibration_Success) {
  const std::map<std::string, std::map<std::string, std::string>>
      predefined_calibration_map = {
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

  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationMap, predefined_calibration_map));

  bool signal_sent = false;
  EXPECT_CALL(signal_calibration_overall_sender_,
              SendCalibrationOverallSignal(RMAD_CALIBRATION_OVERALL_COMPLETE))
      .WillOnce(DoAll(Assign(&signal_sent, true), Return(true)));

  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  task_environment_.FastForwardBy(RunCalibrationStateHandler::kPollInterval);
  EXPECT_TRUE(signal_sent);

  std::map<std::string, std::map<std::string, std::string>>
      current_calibration_map;
  EXPECT_TRUE(json_store_->GetValue(kCalibrationMap, &current_calibration_map));

  const std::map<std::string, std::map<std::string, std::string>>
      target_calibration_map = {
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

  EXPECT_EQ(current_calibration_map, target_calibration_map);

  RmadState state;
  state.set_allocated_run_calibration(new RunCalibrationState);

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kProvisionDevice);
}

TEST_F(RunCalibrationStateHandlerTest, GetNextStateCase_MissingState) {
  const std::map<std::string, std::map<std::string, std::string>>
      predefined_calibration_map = {
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

  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationMap, predefined_calibration_map));

  bool signal_sent = false;
  EXPECT_CALL(signal_calibration_component_sender_,
              SendCalibrationProgressSignal(_))
      .WillRepeatedly(DoAll(Assign(&signal_sent, true), Return(true)));

  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  task_environment_.FastForwardBy(RunCalibrationStateHandler::kPollInterval);
  EXPECT_TRUE(signal_sent);

  std::map<std::string, std::map<std::string, std::string>>
      current_calibration_map;
  EXPECT_TRUE(json_store_->GetValue(kCalibrationMap, &current_calibration_map));

  const std::map<std::string, std::map<std::string, std::string>>
      target_calibration_map = {
          {CalibrationSetupInstruction_Name(base_acc_setup_instruction),
           {{RmadComponent_Name(
                 RmadComponent::RMAD_COMPONENT_BASE_ACCELEROMETER),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_IN_PROGRESS)},
            {RmadComponent_Name(RmadComponent::RMAD_COMPONENT_GYROSCOPE),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_IN_PROGRESS)}}},
          {CalibrationSetupInstruction_Name(lid_acc_setup_instruction),
           {{RmadComponent_Name(
                 RmadComponent::RMAD_COMPONENT_LID_ACCELEROMETER),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_WAITING)}}}};

  EXPECT_EQ(current_calibration_map, target_calibration_map);

  // No RunCalibrationState.
  RmadState state;

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kRunCalibration);
}

TEST_F(RunCalibrationStateHandlerTest, GetNextStateCase_NotFinished) {
  const std::map<std::string, std::map<std::string, std::string>>
      predefined_calibration_map = {
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

  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationMap, predefined_calibration_map));

  bool signal_sent = false;
  EXPECT_CALL(signal_calibration_component_sender_,
              SendCalibrationProgressSignal(_))
      .WillRepeatedly(DoAll(Assign(&signal_sent, true), Return(true)));

  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  task_environment_.FastForwardBy(RunCalibrationStateHandler::kPollInterval);
  EXPECT_TRUE(signal_sent);

  std::map<std::string, std::map<std::string, std::string>>
      current_calibration_map;
  EXPECT_TRUE(json_store_->GetValue(kCalibrationMap, &current_calibration_map));

  const std::map<std::string, std::map<std::string, std::string>>
      target_calibration_map = {
          {CalibrationSetupInstruction_Name(base_acc_setup_instruction),
           {{RmadComponent_Name(
                 RmadComponent::RMAD_COMPONENT_BASE_ACCELEROMETER),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_IN_PROGRESS)},
            {RmadComponent_Name(RmadComponent::RMAD_COMPONENT_GYROSCOPE),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_IN_PROGRESS)}}},
          {CalibrationSetupInstruction_Name(lid_acc_setup_instruction),
           {{RmadComponent_Name(
                 RmadComponent::RMAD_COMPONENT_LID_ACCELEROMETER),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_WAITING)}}}};

  RmadState state;
  state.set_allocated_run_calibration(new RunCalibrationState);

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_CALIBRATION_FAILED);
  EXPECT_EQ(state_case, RmadState::StateCase::kCheckCalibration);
}

TEST_F(RunCalibrationStateHandlerTest, GetNextStateCase_UnknownComponent) {
  const std::map<std::string, std::map<std::string, std::string>>
      predefined_calibration_map = {
          {CalibrationSetupInstruction_Name(base_acc_setup_instruction),
           {{RmadComponent_Name(RmadComponent::RMAD_COMPONENT_UNKNOWN),
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

  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationMap, predefined_calibration_map));

  bool signal_sent = false;
  EXPECT_CALL(signal_calibration_overall_sender_,
              SendCalibrationOverallSignal(
                  RMAD_CALIBRATION_OVERALL_INITIALIZATION_FAILED))
      .WillOnce(DoAll(Assign(&signal_sent, true), Return(true)));

  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  EXPECT_TRUE(signal_sent);

  RmadState state;
  state.set_allocated_run_calibration(new RunCalibrationState);

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_CALIBRATION_COMPONENT_MISSING);
  EXPECT_EQ(state_case, RmadState::StateCase::kCheckCalibration);
}

TEST_F(RunCalibrationStateHandlerTest, GetNextStateCase_InvalidComponent) {
  const std::map<std::string, std::map<std::string, std::string>>
      predefined_calibration_map = {
          {CalibrationSetupInstruction_Name(base_acc_setup_instruction),
           {{RmadComponent_Name(RmadComponent::RMAD_COMPONENT_DRAM),
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

  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationMap, predefined_calibration_map));

  bool signal_sent = false;
  EXPECT_CALL(signal_calibration_overall_sender_,
              SendCalibrationOverallSignal(
                  RMAD_CALIBRATION_OVERALL_INITIALIZATION_FAILED))
      .WillOnce(DoAll(Assign(&signal_sent, true), Return(true)));

  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  EXPECT_TRUE(signal_sent);

  RmadState state;
  state.set_allocated_run_calibration(new RunCalibrationState);

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_CALIBRATION_COMPONENT_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kCheckCalibration);
}

TEST_F(RunCalibrationStateHandlerTest, GetNextStateCase_UnknownStatus) {
  const std::map<std::string, std::map<std::string, std::string>>
      predefined_calibration_map = {
          {CalibrationSetupInstruction_Name(base_acc_setup_instruction),
           {{RmadComponent_Name(
                 RmadComponent::RMAD_COMPONENT_BASE_ACCELEROMETER),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_UNKNOWN)},
            {RmadComponent_Name(RmadComponent::RMAD_COMPONENT_GYROSCOPE),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_WAITING)}}},
          {CalibrationSetupInstruction_Name(lid_acc_setup_instruction),
           {{RmadComponent_Name(
                 RmadComponent::RMAD_COMPONENT_LID_ACCELEROMETER),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_WAITING)}}}};

  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationMap, predefined_calibration_map));

  bool signal_sent = false;
  EXPECT_CALL(signal_calibration_overall_sender_,
              SendCalibrationOverallSignal(
                  RMAD_CALIBRATION_OVERALL_INITIALIZATION_FAILED))
      .WillOnce(DoAll(Assign(&signal_sent, true), Return(true)));

  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  EXPECT_TRUE(signal_sent);

  RmadState state;
  state.set_allocated_run_calibration(new RunCalibrationState);

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_CALIBRATION_STATUS_MISSING);
  EXPECT_EQ(state_case, RmadState::StateCase::kCheckCalibration);
}

}  // namespace rmad
