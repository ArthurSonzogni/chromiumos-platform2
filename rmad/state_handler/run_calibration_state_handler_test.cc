// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <map>
#include <memory>
#include <string>

#include <base/strings/string_number_conversions.h>
#include <base/test/task_environment.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rmad/constants.h"
#include "rmad/state_handler/run_calibration_state_handler.h"
#include "rmad/state_handler/state_handler_test_common.h"

using CalibrationStatus = rmad::CheckCalibrationState::CalibrationStatus;
using testing::_;
using testing::Assign;
using testing::DoAll;
using testing::IsFalse;
using testing::Return;
using testing::StrictMock;

namespace rmad {

MATCHER_P(MatchesCalibrationStatus, calibration_status, "") {
  return arg.name() == calibration_status.name() &&
         arg.status() == calibration_status.status();
}

class RunCalibrationStateHandlerTest : public StateHandlerTest {
 public:
  // Helper class to mock the callback function to send signal.
  class SignalSender {
   public:
    MOCK_METHOD(bool,
                SendCalibrationProgressSignal,
                (CheckCalibrationState::CalibrationStatus, double),
                (const));
  };

  scoped_refptr<RunCalibrationStateHandler> CreateStateHandler() {
    auto handler =
        base::MakeRefCounted<RunCalibrationStateHandler>(json_store_);
    auto callback = std::make_unique<base::RepeatingCallback<bool(
        CheckCalibrationState::CalibrationStatus, double)>>(
        base::BindRepeating(&SignalSender::SendCalibrationProgressSignal,
                            base::Unretained(&signal_sender_)));
    handler->RegisterSignalSender(std::move(callback));
    return handler;
  }

 protected:
  StrictMock<SignalSender> signal_sender_;

  // Variables for TaskRunner.
  base::test::SingleThreadTaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};

  void SetUp() override {
    StateHandlerTest::SetUp();
    base_acc_priority = -1;
    lid_acc_priority = -1;

    for (auto calibration_priority : kComponentsCalibrationPriority) {
      if (calibration_priority[0] ==
          CalibrationStatus::RMAD_CALIBRATION_COMPONENT_BASE_ACCELEROMETER) {
        base_acc_priority = calibration_priority[1];
        break;
      }
    }
    for (auto calibration_priority : kComponentsCalibrationPriority) {
      if (calibration_priority[0] ==
          CalibrationStatus::RMAD_CALIBRATION_COMPONENT_LID_ACCELEROMETER) {
        lid_acc_priority = calibration_priority[1];
        break;
      }
    }

    EXPECT_GE(base_acc_priority, 0);
    EXPECT_GE(lid_acc_priority, 0);
    EXPECT_NE(base_acc_priority, lid_acc_priority);
  }

  int base_acc_priority;
  int lid_acc_priority;
};

TEST_F(RunCalibrationStateHandlerTest, InitializeState_Success) {
  CalibrationStatus unknown_failed_signal;
  unknown_failed_signal.set_name(
      CalibrationStatus::RMAD_CALIBRATION_COMPONENT_UNKNOWN);
  unknown_failed_signal.set_status(CalibrationStatus::RMAD_CALIBRATE_FAILED);
  bool signal_sent = false;
  EXPECT_CALL(signal_sender_,
              SendCalibrationProgressSignal(
                  MatchesCalibrationStatus(unknown_failed_signal), _))
      .WillOnce(DoAll(Assign(&signal_sent, true), Return(true)));

  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  EXPECT_TRUE(signal_sent);
}

TEST_F(RunCalibrationStateHandlerTest,
       GetNextStateCase_NeedCalibration_Success) {
  const std::map<std::string, std::map<std::string, std::string>>
      predefined_calibration_map = {
          {base::NumberToString(base_acc_priority),
           {{CalibrationStatus::Component_Name(
                 CalibrationStatus::
                     RMAD_CALIBRATION_COMPONENT_BASE_ACCELEROMETER),
             CalibrationStatus::Status_Name(
                 CalibrationStatus::RMAD_CALIBRATE_WAITING)},
            {CalibrationStatus::Component_Name(
                 CalibrationStatus::RMAD_CALIBRATION_COMPONENT_GYROSCOPE),
             CalibrationStatus::Status_Name(
                 CalibrationStatus::RMAD_CALIBRATE_WAITING)}}},
          {base::NumberToString(lid_acc_priority),
           {{CalibrationStatus::Component_Name(
                 CalibrationStatus::
                     RMAD_CALIBRATION_COMPONENT_LID_ACCELEROMETER),
             CalibrationStatus::Status_Name(
                 CalibrationStatus::RMAD_CALIBRATE_WAITING)}}}};

  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationMap, predefined_calibration_map));

  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  bool signal_sent = false;
  EXPECT_CALL(signal_sender_, SendCalibrationProgressSignal(_, _))
      .WillRepeatedly(DoAll(Assign(&signal_sent, true), Return(true)));
  task_environment_.FastForwardBy(RunCalibrationStateHandler::kPollInterval);
  EXPECT_TRUE(signal_sent);

  std::map<std::string, std::map<std::string, std::string>>
      current_calibration_map;
  EXPECT_TRUE(json_store_->GetValue(kCalibrationMap, &current_calibration_map));

  const std::map<std::string, std::map<std::string, std::string>>
      target_calibration_map = {
          {base::NumberToString(base_acc_priority),
           {{CalibrationStatus::Component_Name(
                 CalibrationStatus::
                     RMAD_CALIBRATION_COMPONENT_BASE_ACCELEROMETER),
             CalibrationStatus::Status_Name(
                 CalibrationStatus::RMAD_CALIBRATE_IN_PROGRESS)},
            {CalibrationStatus::Component_Name(
                 CalibrationStatus::RMAD_CALIBRATION_COMPONENT_GYROSCOPE),
             CalibrationStatus::Status_Name(
                 CalibrationStatus::RMAD_CALIBRATE_IN_PROGRESS)}}},
          {base::NumberToString(lid_acc_priority),
           {{CalibrationStatus::Component_Name(
                 CalibrationStatus::
                     RMAD_CALIBRATION_COMPONENT_LID_ACCELEROMETER),
             CalibrationStatus::Status_Name(
                 CalibrationStatus::RMAD_CALIBRATE_WAITING)}}}};

  EXPECT_EQ(current_calibration_map, target_calibration_map);
}

TEST_F(RunCalibrationStateHandlerTest,
       GetNextStateCase_RetryCalibration_Success) {
  const std::map<std::string, std::map<std::string, std::string>>
      predefined_calibration_map = {
          {base::NumberToString(base_acc_priority),
           {{CalibrationStatus::Component_Name(
                 CalibrationStatus::
                     RMAD_CALIBRATION_COMPONENT_BASE_ACCELEROMETER),
             CalibrationStatus::Status_Name(
                 CalibrationStatus::RMAD_CALIBRATE_FAILED)},
            {CalibrationStatus::Component_Name(
                 CalibrationStatus::RMAD_CALIBRATION_COMPONENT_GYROSCOPE),
             CalibrationStatus::Status_Name(
                 CalibrationStatus::RMAD_CALIBRATE_SKIP)}}},
          {base::NumberToString(lid_acc_priority),
           {{CalibrationStatus::Component_Name(
                 CalibrationStatus::
                     RMAD_CALIBRATION_COMPONENT_LID_ACCELEROMETER),
             CalibrationStatus::Status_Name(
                 CalibrationStatus::RMAD_CALIBRATE_COMPLETE)}}}};

  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationMap, predefined_calibration_map));

  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  bool signal_sent = false;
  EXPECT_CALL(signal_sender_, SendCalibrationProgressSignal(_, _))
      .WillRepeatedly(DoAll(Assign(&signal_sent, true), Return(true)));
  task_environment_.FastForwardBy(RunCalibrationStateHandler::kPollInterval);
  EXPECT_TRUE(signal_sent);

  std::map<std::string, std::map<std::string, std::string>>
      current_calibration_map;
  EXPECT_TRUE(json_store_->GetValue(kCalibrationMap, &current_calibration_map));

  const std::map<std::string, std::map<std::string, std::string>>
      target_calibration_map = {
          {base::NumberToString(base_acc_priority),
           {{CalibrationStatus::Component_Name(
                 CalibrationStatus::
                     RMAD_CALIBRATION_COMPONENT_BASE_ACCELEROMETER),
             CalibrationStatus::Status_Name(
                 CalibrationStatus::RMAD_CALIBRATE_IN_PROGRESS)},
            {CalibrationStatus::Component_Name(
                 CalibrationStatus::RMAD_CALIBRATION_COMPONENT_GYROSCOPE),
             CalibrationStatus::Status_Name(
                 CalibrationStatus::RMAD_CALIBRATE_SKIP)}}},
          {base::NumberToString(lid_acc_priority),
           {{CalibrationStatus::Component_Name(
                 CalibrationStatus::
                     RMAD_CALIBRATION_COMPONENT_LID_ACCELEROMETER),
             CalibrationStatus::Status_Name(
                 CalibrationStatus::RMAD_CALIBRATE_COMPLETE)}}}};

  EXPECT_EQ(current_calibration_map, target_calibration_map);
}

TEST_F(RunCalibrationStateHandlerTest,
       GetNextStateCase_NoNeedCalibration_Success) {
  const std::map<std::string, std::map<std::string, std::string>>
      predefined_calibration_map = {
          {base::NumberToString(base_acc_priority),
           {{CalibrationStatus::Component_Name(
                 CalibrationStatus::
                     RMAD_CALIBRATION_COMPONENT_BASE_ACCELEROMETER),
             CalibrationStatus::Status_Name(
                 CalibrationStatus::RMAD_CALIBRATE_COMPLETE)},
            {CalibrationStatus::Component_Name(
                 CalibrationStatus::RMAD_CALIBRATION_COMPONENT_GYROSCOPE),
             CalibrationStatus::Status_Name(
                 CalibrationStatus::RMAD_CALIBRATE_SKIP)}}},
          {base::NumberToString(lid_acc_priority),
           {{CalibrationStatus::Component_Name(
                 CalibrationStatus::
                     RMAD_CALIBRATION_COMPONENT_LID_ACCELEROMETER),
             CalibrationStatus::Status_Name(
                 CalibrationStatus::RMAD_CALIBRATE_COMPLETE)}}}};

  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationMap, predefined_calibration_map));

  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  bool signal_sent = false;
  EXPECT_CALL(signal_sender_, SendCalibrationProgressSignal(_, _))
      .WillRepeatedly(DoAll(Assign(&signal_sent, true), Return(true)));
  task_environment_.FastForwardBy(RunCalibrationStateHandler::kPollInterval);
  EXPECT_FALSE(signal_sent);

  std::map<std::string, std::map<std::string, std::string>>
      current_calibration_map;
  EXPECT_TRUE(json_store_->GetValue(kCalibrationMap, &current_calibration_map));

  const std::map<std::string, std::map<std::string, std::string>>
      target_calibration_map = {
          {base::NumberToString(base_acc_priority),
           {{CalibrationStatus::Component_Name(
                 CalibrationStatus::
                     RMAD_CALIBRATION_COMPONENT_BASE_ACCELEROMETER),
             CalibrationStatus::Status_Name(
                 CalibrationStatus::RMAD_CALIBRATE_COMPLETE)},
            {CalibrationStatus::Component_Name(
                 CalibrationStatus::RMAD_CALIBRATION_COMPONENT_GYROSCOPE),
             CalibrationStatus::Status_Name(
                 CalibrationStatus::RMAD_CALIBRATE_SKIP)}}},
          {base::NumberToString(lid_acc_priority),
           {{CalibrationStatus::Component_Name(
                 CalibrationStatus::
                     RMAD_CALIBRATION_COMPONENT_LID_ACCELEROMETER),
             CalibrationStatus::Status_Name(
                 CalibrationStatus::RMAD_CALIBRATE_COMPLETE)}}}};

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
          {base::NumberToString(base_acc_priority),
           {{CalibrationStatus::Component_Name(
                 CalibrationStatus::
                     RMAD_CALIBRATION_COMPONENT_BASE_ACCELEROMETER),
             CalibrationStatus::Status_Name(
                 CalibrationStatus::RMAD_CALIBRATE_WAITING)},
            {CalibrationStatus::Component_Name(
                 CalibrationStatus::RMAD_CALIBRATION_COMPONENT_GYROSCOPE),
             CalibrationStatus::Status_Name(
                 CalibrationStatus::RMAD_CALIBRATE_WAITING)}}},
          {base::NumberToString(lid_acc_priority),
           {{CalibrationStatus::Component_Name(
                 CalibrationStatus::
                     RMAD_CALIBRATION_COMPONENT_LID_ACCELEROMETER),
             CalibrationStatus::Status_Name(
                 CalibrationStatus::RMAD_CALIBRATE_WAITING)}}}};

  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationMap, predefined_calibration_map));

  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // No RunCalibrationState.
  RmadState state;

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kRunCalibration);
}

TEST_F(RunCalibrationStateHandlerTest, GetNextStateCase_NotFinished) {
  const std::map<std::string, std::map<std::string, std::string>>
      predefined_calibration_map = {
          {base::NumberToString(base_acc_priority),
           {{CalibrationStatus::Component_Name(
                 CalibrationStatus::
                     RMAD_CALIBRATION_COMPONENT_BASE_ACCELEROMETER),
             CalibrationStatus::Status_Name(
                 CalibrationStatus::RMAD_CALIBRATE_WAITING)},
            {CalibrationStatus::Component_Name(
                 CalibrationStatus::RMAD_CALIBRATION_COMPONENT_GYROSCOPE),
             CalibrationStatus::Status_Name(
                 CalibrationStatus::RMAD_CALIBRATE_WAITING)}}},
          {base::NumberToString(lid_acc_priority),
           {{CalibrationStatus::Component_Name(
                 CalibrationStatus::
                     RMAD_CALIBRATION_COMPONENT_LID_ACCELEROMETER),
             CalibrationStatus::Status_Name(
                 CalibrationStatus::RMAD_CALIBRATE_WAITING)}}}};

  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationMap, predefined_calibration_map));

  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  RmadState state;
  state.set_allocated_run_calibration(new RunCalibrationState);

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_CALIBRATION_FAILED);
  EXPECT_EQ(state_case, RmadState::StateCase::kCheckCalibration);
}

TEST_F(RunCalibrationStateHandlerTest, GetNextStateCase_UnknownComponent) {
  const std::map<std::string, std::map<std::string, std::string>>
      predefined_calibration_map = {
          {base::NumberToString(base_acc_priority),
           {{CalibrationStatus::Component_Name(
                 CalibrationStatus::RMAD_CALIBRATION_COMPONENT_UNKNOWN),
             CalibrationStatus::Status_Name(
                 CalibrationStatus::RMAD_CALIBRATE_WAITING)},
            {CalibrationStatus::Component_Name(
                 CalibrationStatus::RMAD_CALIBRATION_COMPONENT_GYROSCOPE),
             CalibrationStatus::Status_Name(
                 CalibrationStatus::RMAD_CALIBRATE_WAITING)}}},
          {base::NumberToString(lid_acc_priority),
           {{CalibrationStatus::Component_Name(
                 CalibrationStatus::
                     RMAD_CALIBRATION_COMPONENT_LID_ACCELEROMETER),
             CalibrationStatus::Status_Name(
                 CalibrationStatus::RMAD_CALIBRATE_WAITING)}}}};

  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationMap, predefined_calibration_map));

  CalibrationStatus unknown_failed_signal;
  unknown_failed_signal.set_name(
      CalibrationStatus::RMAD_CALIBRATION_COMPONENT_UNKNOWN);
  unknown_failed_signal.set_status(CalibrationStatus::RMAD_CALIBRATE_FAILED);
  bool signal_sent = false;
  EXPECT_CALL(signal_sender_,
              SendCalibrationProgressSignal(
                  MatchesCalibrationStatus(unknown_failed_signal), _))
      .WillOnce(DoAll(Assign(&signal_sent, true), Return(true)));

  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  EXPECT_TRUE(signal_sent);

  RmadState state;
  state.set_allocated_run_calibration(new RunCalibrationState);

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_CALIBRATION_FAILED);
  EXPECT_EQ(state_case, RmadState::StateCase::kCheckCalibration);
}

TEST_F(RunCalibrationStateHandlerTest, GetNextStateCase_UnknownStatus) {
  const std::map<std::string, std::map<std::string, std::string>>
      predefined_calibration_map = {
          {base::NumberToString(base_acc_priority),
           {{CalibrationStatus::Component_Name(
                 CalibrationStatus::
                     RMAD_CALIBRATION_COMPONENT_BASE_ACCELEROMETER),
             CalibrationStatus::Status_Name(
                 CalibrationStatus::RMAD_CALIBRATE_UNKNOWN)},
            {CalibrationStatus::Component_Name(
                 CalibrationStatus::RMAD_CALIBRATION_COMPONENT_GYROSCOPE),
             CalibrationStatus::Status_Name(
                 CalibrationStatus::RMAD_CALIBRATE_WAITING)}}},
          {base::NumberToString(lid_acc_priority),
           {{CalibrationStatus::Component_Name(
                 CalibrationStatus::
                     RMAD_CALIBRATION_COMPONENT_LID_ACCELEROMETER),
             CalibrationStatus::Status_Name(
                 CalibrationStatus::RMAD_CALIBRATE_WAITING)}}}};

  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationMap, predefined_calibration_map));

  CalibrationStatus unknown_failed_signal;
  unknown_failed_signal.set_name(
      CalibrationStatus::RMAD_CALIBRATION_COMPONENT_UNKNOWN);
  unknown_failed_signal.set_status(CalibrationStatus::RMAD_CALIBRATE_FAILED);
  bool signal_sent = false;
  EXPECT_CALL(signal_sender_,
              SendCalibrationProgressSignal(
                  MatchesCalibrationStatus(unknown_failed_signal), _))
      .WillOnce(DoAll(Assign(&signal_sent, true), Return(true)));

  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  EXPECT_TRUE(signal_sent);

  RmadState state;
  state.set_allocated_run_calibration(new RunCalibrationState);

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_CALIBRATION_FAILED);
  EXPECT_EQ(state_case, RmadState::StateCase::kCheckCalibration);
}

}  // namespace rmad
