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
};

TEST_F(RunCalibrationStateHandlerTest, InitializeState_Success) {
  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
}

TEST_F(RunCalibrationStateHandlerTest,
       GetNextStateCase_NeedCalibration_Success) {
  const std::map<std::string, std::string> predefined_calibration_map = {
      {base::NumberToString(
           CalibrationStatus::RMAD_CALIBRATION_COMPONENT_ACCELEROMETER),
       base::NumberToString(CalibrationStatus::RMAD_CALIBRATE_WAITING)},
      {base::NumberToString(
           CalibrationStatus::RMAD_CALIBRATION_COMPONENT_GYROSCOPE),
       base::NumberToString(CalibrationStatus::RMAD_CALIBRATE_WAITING)}};
  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationMap, predefined_calibration_map));

  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  bool signal_sent = false;
  EXPECT_CALL(signal_sender_, SendCalibrationProgressSignal(_, _))
      .WillRepeatedly(DoAll(Assign(&signal_sent, true), Return(true)));
  task_environment_.FastForwardBy(RunCalibrationStateHandler::kPollInterval);
  EXPECT_TRUE(signal_sent);

  std::map<std::string, std::string> current_calibration_map;
  EXPECT_TRUE(json_store_->GetValue(kCalibrationMap, &current_calibration_map));

  const std::map<std::string, std::string> target_calibration_map = {
      {base::NumberToString(
           CalibrationStatus::RMAD_CALIBRATION_COMPONENT_ACCELEROMETER),
       base::NumberToString(CalibrationStatus::RMAD_CALIBRATE_IN_PROGRESS)},
      {base::NumberToString(
           CalibrationStatus::RMAD_CALIBRATION_COMPONENT_GYROSCOPE),
       base::NumberToString(CalibrationStatus::RMAD_CALIBRATE_IN_PROGRESS)}};

  EXPECT_EQ(current_calibration_map, target_calibration_map);
}

TEST_F(RunCalibrationStateHandlerTest,
       GetNextStateCase_RetryCalibration_Success) {
  const std::map<std::string, std::string> predefined_calibration_map = {
      {base::NumberToString(
           CalibrationStatus::RMAD_CALIBRATION_COMPONENT_ACCELEROMETER),
       base::NumberToString(CalibrationStatus::RMAD_CALIBRATE_FAILED)},
      {base::NumberToString(
           CalibrationStatus::RMAD_CALIBRATION_COMPONENT_GYROSCOPE),
       base::NumberToString(CalibrationStatus::RMAD_CALIBRATE_SKIP)}};
  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationMap, predefined_calibration_map));

  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  bool signal_sent = false;
  EXPECT_CALL(signal_sender_, SendCalibrationProgressSignal(_, _))
      .WillRepeatedly(DoAll(Assign(&signal_sent, true), Return(true)));
  task_environment_.FastForwardBy(RunCalibrationStateHandler::kPollInterval);
  EXPECT_TRUE(signal_sent);

  std::map<std::string, std::string> current_calibration_map;
  EXPECT_TRUE(json_store_->GetValue(kCalibrationMap, &current_calibration_map));

  const std::map<std::string, std::string> target_calibration_map = {
      {base::NumberToString(
           CalibrationStatus::RMAD_CALIBRATION_COMPONENT_ACCELEROMETER),
       base::NumberToString(CalibrationStatus::RMAD_CALIBRATE_IN_PROGRESS)},
      {base::NumberToString(
           CalibrationStatus::RMAD_CALIBRATION_COMPONENT_GYROSCOPE),
       base::NumberToString(CalibrationStatus::RMAD_CALIBRATE_SKIP)}};

  EXPECT_EQ(current_calibration_map, target_calibration_map);
}

TEST_F(RunCalibrationStateHandlerTest,
       GetNextStateCase_NoNeedCalibration_Success) {
  const std::map<std::string, std::string> predefined_calibration_map = {
      {base::NumberToString(
           CalibrationStatus::RMAD_CALIBRATION_COMPONENT_ACCELEROMETER),
       base::NumberToString(CalibrationStatus::RMAD_CALIBRATE_COMPLETE)},
      {base::NumberToString(
           CalibrationStatus::RMAD_CALIBRATION_COMPONENT_GYROSCOPE),
       base::NumberToString(CalibrationStatus::RMAD_CALIBRATE_SKIP)}};
  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationMap, predefined_calibration_map));

  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  task_environment_.FastForwardBy(RunCalibrationStateHandler::kPollInterval);

  std::map<std::string, std::string> current_calibration_map;
  EXPECT_TRUE(json_store_->GetValue(kCalibrationMap, &current_calibration_map));

  const std::map<std::string, std::string> target_calibration_map = {
      {base::NumberToString(
           CalibrationStatus::RMAD_CALIBRATION_COMPONENT_ACCELEROMETER),
       base::NumberToString(CalibrationStatus::RMAD_CALIBRATE_COMPLETE)},
      {base::NumberToString(
           CalibrationStatus::RMAD_CALIBRATION_COMPONENT_GYROSCOPE),
       base::NumberToString(CalibrationStatus::RMAD_CALIBRATE_SKIP)}};

  EXPECT_EQ(current_calibration_map, target_calibration_map);

  RmadState state;
  state.set_allocated_run_calibration(new RunCalibrationState);

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kProvisionDevice);
}

TEST_F(RunCalibrationStateHandlerTest, GetNextStateCase_MissingState) {
  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // No RunCalibrationState.
  RmadState state;

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kRunCalibration);
}

TEST_F(RunCalibrationStateHandlerTest, GetNextStateCase_UnknownComponent) {
  const std::map<std::string, std::string> predefined_calibration_map = {
      {base::NumberToString(
           CalibrationStatus::RMAD_CALIBRATION_COMPONENT_ACCELEROMETER),
       base::NumberToString(CalibrationStatus::RMAD_CALIBRATE_WAITING)},
      {base::NumberToString(
           CalibrationStatus::RMAD_CALIBRATION_COMPONENT_GYROSCOPE),
       base::NumberToString(CalibrationStatus::RMAD_CALIBRATE_WAITING)}};
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

TEST_F(RunCalibrationStateHandlerTest, GetNextStateCase_UnknownStatus) {
  const std::map<std::string, std::string> predefined_calibration_map = {
      {base::NumberToString(
           CalibrationStatus::RMAD_CALIBRATION_COMPONENT_ACCELEROMETER),
       base::NumberToString(CalibrationStatus::RMAD_CALIBRATE_WAITING)},
      {base::NumberToString(
           CalibrationStatus::RMAD_CALIBRATION_COMPONENT_GYROSCOPE),
       base::NumberToString(CalibrationStatus::RMAD_CALIBRATE_WAITING)}};
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

}  // namespace rmad
