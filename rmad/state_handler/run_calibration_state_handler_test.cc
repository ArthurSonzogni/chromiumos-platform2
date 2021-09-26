// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/run_calibration_state_handler.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <base/test/task_environment.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rmad/state_handler/state_handler_test_common.h"
#include "rmad/utils/calibration_utils.h"
#include "rmad/utils/mock_sensor_calibration_utils.h"

using testing::_;
using testing::Assign;
using testing::DoAll;
using testing::InSequence;
using testing::Return;
using testing::SetArgPointee;
using testing::StrictMock;
using testing::WithArg;

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

  scoped_refptr<RunCalibrationStateHandler> CreateStateHandler(
      bool base_acc_calibration,
      const std::vector<double>& base_acc_progress,
      bool lid_acc_calibration,
      const std::vector<double>& lid_acc_progress,
      bool base_gyro_calibration,
      const std::vector<double>& base_gyro_progress) {
    std::unique_ptr<StrictMock<MockSensorCalibrationUtils>> base_acc_utils =
        std::make_unique<StrictMock<MockSensorCalibrationUtils>>("base", "acc");
    std::unique_ptr<StrictMock<MockSensorCalibrationUtils>> lid_acc_utils =
        std::make_unique<StrictMock<MockSensorCalibrationUtils>>("lid", "acc");
    std::unique_ptr<StrictMock<MockSensorCalibrationUtils>> base_gyro_utils =
        std::make_unique<StrictMock<MockSensorCalibrationUtils>>("base",
                                                                 "gyro");

    {
      InSequence seq;
      if (base_acc_calibration) {
        EXPECT_CALL(*base_acc_utils, Calibrate()).WillOnce(Return(true));
      }
      for (auto progress : base_acc_progress) {
        EXPECT_CALL(*base_acc_utils, GetProgress(_))
            .WillOnce(DoAll(SetArgPointee<0>(progress), Return(true)));
      }
    }
    {
      InSequence seq;
      if (lid_acc_calibration) {
        EXPECT_CALL(*lid_acc_utils, Calibrate()).WillOnce(Return(true));
      }
      for (auto progress : lid_acc_progress) {
        EXPECT_CALL(*lid_acc_utils, GetProgress(_))
            .WillOnce(DoAll(SetArgPointee<0>(progress), Return(true)));
      }
    }
    {
      InSequence seq;
      if (base_gyro_calibration) {
        EXPECT_CALL(*base_gyro_utils, Calibrate()).WillOnce(Return(true));
      }
      for (auto progress : base_gyro_progress) {
        EXPECT_CALL(*base_gyro_utils, GetProgress(_))
            .WillOnce(DoAll(SetArgPointee<0>(progress), Return(true)));
      }
    }

    // To PostTask into TaskRunner, we ignore the return value of Calibrate.
    // However, it will cause mock leaks in unittest, we use StrictMock and
    // EXPEXT_CALL to ensure the results, and then we Use AllowLeak to prevent
    // warnings.
    testing::Mock::AllowLeak(base_acc_utils.get());
    testing::Mock::AllowLeak(lid_acc_utils.get());
    testing::Mock::AllowLeak(base_gyro_utils.get());

    auto handler = base::MakeRefCounted<RunCalibrationStateHandler>(
        json_store_, std::move(base_acc_utils), std::move(lid_acc_utils),
        std::move(base_gyro_utils));

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

  void QueueProgress(CalibrationComponentStatus progress) {
    progress_history_.push_back(progress);
  }

  void QueueOverallStatus(CalibrationOverallStatus overall_status) {
    overall_status_history_.push_back(overall_status);
  }

 protected:
  StrictMock<SignalSender> signal_calibration_component_sender_;
  StrictMock<SignalSender> signal_calibration_overall_sender_;

  // Variables for TaskRunner.
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::ThreadPoolExecutionMode::ASYNC,
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};

  void SetUp() override {
    StateHandlerTest::SetUp();
    base_acc_setup_instruction_ = GetCalibrationSetupInstruction(
        RmadComponent::RMAD_COMPONENT_BASE_ACCELEROMETER);
    lid_acc_setup_instruction_ = GetCalibrationSetupInstruction(
        RmadComponent::RMAD_COMPONENT_LID_ACCELEROMETER);

    EXPECT_NE(base_acc_setup_instruction_,
              RMAD_CALIBRATION_INSTRUCTION_UNKNOWN);
    EXPECT_NE(lid_acc_setup_instruction_, RMAD_CALIBRATION_INSTRUCTION_UNKNOWN);
    EXPECT_NE(base_acc_setup_instruction_, lid_acc_setup_instruction_);

    EXPECT_CALL(signal_calibration_overall_sender_,
                SendCalibrationOverallSignal(_))
        .WillRepeatedly(DoAll(
            WithArg<0>(Invoke(
                this, &RunCalibrationStateHandlerTest::QueueOverallStatus)),
            Return(true)));

    EXPECT_CALL(signal_calibration_component_sender_,
                SendCalibrationProgressSignal(_))
        .WillRepeatedly(
            DoAll(WithArg<0>(Invoke(
                      this, &RunCalibrationStateHandlerTest::QueueProgress)),
                  Return(true)));
  }

  CalibrationSetupInstruction base_acc_setup_instruction_;
  CalibrationSetupInstruction lid_acc_setup_instruction_;
  std::vector<CalibrationComponentStatus> progress_history_;
  std::vector<CalibrationOverallStatus> overall_status_history_;
};

TEST_F(RunCalibrationStateHandlerTest, InitializeState_Success) {
  auto handler = CreateStateHandler(false, {}, false, {}, false, {});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  task_environment_.RunUntilIdle();
  EXPECT_EQ(overall_status_history_.size(), 1);
  EXPECT_EQ(overall_status_history_[0],
            RMAD_CALIBRATION_OVERALL_INITIALIZATION_FAILED);
}

TEST_F(RunCalibrationStateHandlerTest,
       GetNextStateCase_NeedCalibration_NeedAnotherRound) {
  std::map<std::string, std::map<std::string, std::string>>
      current_calibration_map;
  const std::map<std::string, std::map<std::string, std::string>>
      predefined_calibration_map = {
          {CalibrationSetupInstruction_Name(base_acc_setup_instruction_),
           {{RmadComponent_Name(
                 RmadComponent::RMAD_COMPONENT_BASE_ACCELEROMETER),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_WAITING)},
            {RmadComponent_Name(RmadComponent::RMAD_COMPONENT_GYROSCOPE),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_WAITING)}}},
          {CalibrationSetupInstruction_Name(lid_acc_setup_instruction_),
           {{RmadComponent_Name(
                 RmadComponent::RMAD_COMPONENT_LID_ACCELEROMETER),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_WAITING)}}}};
  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationMap, predefined_calibration_map));

  auto handler =
      CreateStateHandler(true, {0.5, 1.0}, false, {}, true, {0.5, 1.0});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  task_environment_.FastForwardBy(RunCalibrationStateHandler::kPollInterval);
  EXPECT_EQ(progress_history_.size(), 2);
  EXPECT_EQ(progress_history_[0].progress(), 0.5);
  EXPECT_EQ(progress_history_[0].status(),
            CalibrationComponentStatus::RMAD_CALIBRATION_IN_PROGRESS);
  EXPECT_EQ(progress_history_[0].component(),
            RmadComponent::RMAD_COMPONENT_BASE_ACCELEROMETER);
  EXPECT_EQ(progress_history_[1].progress(), 0.5);
  EXPECT_EQ(progress_history_[1].status(),
            CalibrationComponentStatus::RMAD_CALIBRATION_IN_PROGRESS);
  EXPECT_EQ(progress_history_[1].component(),
            RmadComponent::RMAD_COMPONENT_GYROSCOPE);

  EXPECT_TRUE(json_store_->GetValue(kCalibrationMap, &current_calibration_map));
  const std::map<std::string, std::map<std::string, std::string>>
      target_calibration_map_one_interval = {
          {CalibrationSetupInstruction_Name(base_acc_setup_instruction_),
           {{RmadComponent_Name(
                 RmadComponent::RMAD_COMPONENT_BASE_ACCELEROMETER),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_IN_PROGRESS)},
            {RmadComponent_Name(RmadComponent::RMAD_COMPONENT_GYROSCOPE),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_IN_PROGRESS)}}},
          {CalibrationSetupInstruction_Name(lid_acc_setup_instruction_),
           {{RmadComponent_Name(
                 RmadComponent::RMAD_COMPONENT_LID_ACCELEROMETER),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_WAITING)}}}};
  EXPECT_EQ(current_calibration_map, target_calibration_map_one_interval);

  task_environment_.FastForwardBy(RunCalibrationStateHandler::kPollInterval);
  EXPECT_EQ(progress_history_.size(), 4);
  EXPECT_EQ(progress_history_[2].progress(), 1.0);
  EXPECT_EQ(progress_history_[2].status(),
            CalibrationComponentStatus::RMAD_CALIBRATION_COMPLETE);
  EXPECT_EQ(progress_history_[2].component(),
            RmadComponent::RMAD_COMPONENT_BASE_ACCELEROMETER);
  EXPECT_EQ(progress_history_[3].progress(), 1.0);
  EXPECT_EQ(progress_history_[3].status(),
            CalibrationComponentStatus::RMAD_CALIBRATION_COMPLETE);
  EXPECT_EQ(progress_history_[3].component(),
            RmadComponent::RMAD_COMPONENT_GYROSCOPE);
  EXPECT_TRUE(json_store_->GetValue(kCalibrationMap, &current_calibration_map));

  const std::map<std::string, std::map<std::string, std::string>>
      target_calibration_map = {
          {CalibrationSetupInstruction_Name(base_acc_setup_instruction_),
           {{RmadComponent_Name(
                 RmadComponent::RMAD_COMPONENT_BASE_ACCELEROMETER),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_COMPLETE)},
            {RmadComponent_Name(RmadComponent::RMAD_COMPONENT_GYROSCOPE),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_COMPLETE)}}},
          {CalibrationSetupInstruction_Name(lid_acc_setup_instruction_),
           {{RmadComponent_Name(
                 RmadComponent::RMAD_COMPONENT_LID_ACCELEROMETER),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_WAITING)}}}};
  EXPECT_EQ(current_calibration_map, target_calibration_map);

  task_environment_.RunUntilIdle();
  EXPECT_EQ(overall_status_history_.size(), 1);
  EXPECT_EQ(overall_status_history_[0],
            RMAD_CALIBRATION_OVERALL_CURRENT_ROUND_COMPLETE);

  RmadState state;
  state.set_allocated_run_calibration(new RunCalibrationState);
  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kSetupCalibration);
}

TEST_F(RunCalibrationStateHandlerTest,
       GetNextStateCase_NeedCalibration_OverallComplete) {
  std::map<std::string, std::map<std::string, std::string>>
      current_calibration_map;
  const std::map<std::string, std::map<std::string, std::string>>
      predefined_calibration_map = {
          {CalibrationSetupInstruction_Name(base_acc_setup_instruction_),
           {{RmadComponent_Name(
                 RmadComponent::RMAD_COMPONENT_BASE_ACCELEROMETER),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_FAILED)},
            {RmadComponent_Name(RmadComponent::RMAD_COMPONENT_GYROSCOPE),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_SKIP)}}},
          {CalibrationSetupInstruction_Name(lid_acc_setup_instruction_),
           {{RmadComponent_Name(
                 RmadComponent::RMAD_COMPONENT_LID_ACCELEROMETER),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_COMPLETE)}}}};
  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationMap, predefined_calibration_map));

  auto handler = CreateStateHandler(true, {0.5, 1.0}, false, {}, false, {});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  task_environment_.FastForwardBy(RunCalibrationStateHandler::kPollInterval);
  EXPECT_EQ(progress_history_.size(), 1);
  EXPECT_EQ(progress_history_[0].progress(), 0.5);
  EXPECT_EQ(progress_history_[0].status(),
            CalibrationComponentStatus::RMAD_CALIBRATION_IN_PROGRESS);
  EXPECT_EQ(progress_history_[0].component(),
            RmadComponent::RMAD_COMPONENT_BASE_ACCELEROMETER);

  EXPECT_TRUE(json_store_->GetValue(kCalibrationMap, &current_calibration_map));
  const std::map<std::string, std::map<std::string, std::string>>
      target_calibration_map_one_interval = {
          {CalibrationSetupInstruction_Name(base_acc_setup_instruction_),
           {{RmadComponent_Name(
                 RmadComponent::RMAD_COMPONENT_BASE_ACCELEROMETER),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_IN_PROGRESS)},
            {RmadComponent_Name(RmadComponent::RMAD_COMPONENT_GYROSCOPE),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_SKIP)}}},
          {CalibrationSetupInstruction_Name(lid_acc_setup_instruction_),
           {{RmadComponent_Name(
                 RmadComponent::RMAD_COMPONENT_LID_ACCELEROMETER),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_COMPLETE)}}}};
  EXPECT_EQ(current_calibration_map, target_calibration_map_one_interval);

  task_environment_.FastForwardBy(RunCalibrationStateHandler::kPollInterval);
  EXPECT_EQ(progress_history_.size(), 2);
  EXPECT_EQ(progress_history_[1].progress(), 1.0);
  EXPECT_EQ(progress_history_[1].status(),
            CalibrationComponentStatus::RMAD_CALIBRATION_COMPLETE);
  EXPECT_EQ(progress_history_[1].component(),
            RmadComponent::RMAD_COMPONENT_BASE_ACCELEROMETER);

  EXPECT_TRUE(json_store_->GetValue(kCalibrationMap, &current_calibration_map));
  const std::map<std::string, std::map<std::string, std::string>>
      target_calibration_map = {
          {CalibrationSetupInstruction_Name(base_acc_setup_instruction_),
           {{RmadComponent_Name(
                 RmadComponent::RMAD_COMPONENT_BASE_ACCELEROMETER),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_COMPLETE)},
            {RmadComponent_Name(RmadComponent::RMAD_COMPONENT_GYROSCOPE),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_SKIP)}}},
          {CalibrationSetupInstruction_Name(lid_acc_setup_instruction_),
           {{RmadComponent_Name(
                 RmadComponent::RMAD_COMPONENT_LID_ACCELEROMETER),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_COMPLETE)}}}};
  EXPECT_EQ(current_calibration_map, target_calibration_map);

  task_environment_.RunUntilIdle();
  EXPECT_EQ(overall_status_history_.size(), 1);
  EXPECT_EQ(overall_status_history_[0], RMAD_CALIBRATION_OVERALL_COMPLETE);

  RmadState state;
  state.set_allocated_run_calibration(new RunCalibrationState);
  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kProvisionDevice);
}

TEST_F(RunCalibrationStateHandlerTest, GetNextStateCase_NoNeedCalibration) {
  std::map<std::string, std::map<std::string, std::string>>
      current_calibration_map;
  const std::map<std::string, std::map<std::string, std::string>>
      predefined_calibration_map = {
          {CalibrationSetupInstruction_Name(base_acc_setup_instruction_),
           {{RmadComponent_Name(
                 RmadComponent::RMAD_COMPONENT_BASE_ACCELEROMETER),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_COMPLETE)},
            {RmadComponent_Name(RmadComponent::RMAD_COMPONENT_GYROSCOPE),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_SKIP)}}},
          {CalibrationSetupInstruction_Name(lid_acc_setup_instruction_),
           {{RmadComponent_Name(
                 RmadComponent::RMAD_COMPONENT_LID_ACCELEROMETER),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_COMPLETE)}}}};
  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationMap, predefined_calibration_map));

  auto handler = CreateStateHandler(false, {}, false, {}, false, {});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  task_environment_.RunUntilIdle();
  EXPECT_EQ(progress_history_.size(), 0);

  EXPECT_TRUE(json_store_->GetValue(kCalibrationMap, &current_calibration_map));
  const std::map<std::string, std::map<std::string, std::string>>
      target_calibration_map = {
          {CalibrationSetupInstruction_Name(base_acc_setup_instruction_),
           {{RmadComponent_Name(
                 RmadComponent::RMAD_COMPONENT_BASE_ACCELEROMETER),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_COMPLETE)},
            {RmadComponent_Name(RmadComponent::RMAD_COMPONENT_GYROSCOPE),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_SKIP)}}},
          {CalibrationSetupInstruction_Name(lid_acc_setup_instruction_),
           {{RmadComponent_Name(
                 RmadComponent::RMAD_COMPONENT_LID_ACCELEROMETER),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_COMPLETE)}}}};
  EXPECT_EQ(current_calibration_map, target_calibration_map);

  EXPECT_EQ(overall_status_history_.size(), 1);
  EXPECT_EQ(overall_status_history_[0], RMAD_CALIBRATION_OVERALL_COMPLETE);

  RmadState state;
  state.set_allocated_run_calibration(new RunCalibrationState);
  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kProvisionDevice);
}

TEST_F(RunCalibrationStateHandlerTest, GetNextStateCase_MissingState) {
  std::map<std::string, std::map<std::string, std::string>>
      current_calibration_map;
  const std::map<std::string, std::map<std::string, std::string>>
      predefined_calibration_map = {
          {CalibrationSetupInstruction_Name(base_acc_setup_instruction_),
           {{RmadComponent_Name(
                 RmadComponent::RMAD_COMPONENT_BASE_ACCELEROMETER),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_WAITING)},
            {RmadComponent_Name(RmadComponent::RMAD_COMPONENT_GYROSCOPE),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_WAITING)}}},
          {CalibrationSetupInstruction_Name(lid_acc_setup_instruction_),
           {{RmadComponent_Name(
                 RmadComponent::RMAD_COMPONENT_LID_ACCELEROMETER),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_WAITING)}}}};
  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationMap, predefined_calibration_map));

  auto handler =
      CreateStateHandler(true, {0.5, 1.0}, false, {}, true, {0.5, 1.0});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  task_environment_.FastForwardBy(RunCalibrationStateHandler::kPollInterval);
  EXPECT_EQ(progress_history_.size(), 2);
  EXPECT_EQ(progress_history_[0].progress(), 0.5);
  EXPECT_EQ(progress_history_[0].status(),
            CalibrationComponentStatus::RMAD_CALIBRATION_IN_PROGRESS);
  EXPECT_EQ(progress_history_[0].component(),
            RmadComponent::RMAD_COMPONENT_BASE_ACCELEROMETER);
  EXPECT_EQ(progress_history_[1].progress(), 0.5);
  EXPECT_EQ(progress_history_[1].status(),
            CalibrationComponentStatus::RMAD_CALIBRATION_IN_PROGRESS);
  EXPECT_EQ(progress_history_[1].component(),
            RmadComponent::RMAD_COMPONENT_GYROSCOPE);

  EXPECT_TRUE(json_store_->GetValue(kCalibrationMap, &current_calibration_map));
  const std::map<std::string, std::map<std::string, std::string>>
      target_calibration_map_one_interval = {
          {CalibrationSetupInstruction_Name(base_acc_setup_instruction_),
           {{RmadComponent_Name(
                 RmadComponent::RMAD_COMPONENT_BASE_ACCELEROMETER),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_IN_PROGRESS)},
            {RmadComponent_Name(RmadComponent::RMAD_COMPONENT_GYROSCOPE),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_IN_PROGRESS)}}},
          {CalibrationSetupInstruction_Name(lid_acc_setup_instruction_),
           {{RmadComponent_Name(
                 RmadComponent::RMAD_COMPONENT_LID_ACCELEROMETER),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_WAITING)}}}};
  EXPECT_EQ(current_calibration_map, target_calibration_map_one_interval);

  task_environment_.FastForwardBy(RunCalibrationStateHandler::kPollInterval);
  EXPECT_EQ(progress_history_.size(), 4);
  EXPECT_EQ(progress_history_[2].progress(), 1.0);
  EXPECT_EQ(progress_history_[2].status(),
            CalibrationComponentStatus::RMAD_CALIBRATION_COMPLETE);
  EXPECT_EQ(progress_history_[2].component(),
            RmadComponent::RMAD_COMPONENT_BASE_ACCELEROMETER);
  EXPECT_EQ(progress_history_[3].progress(), 1.0);
  EXPECT_EQ(progress_history_[3].status(),
            CalibrationComponentStatus::RMAD_CALIBRATION_COMPLETE);
  EXPECT_EQ(progress_history_[3].component(),
            RmadComponent::RMAD_COMPONENT_GYROSCOPE);

  EXPECT_TRUE(json_store_->GetValue(kCalibrationMap, &current_calibration_map));
  const std::map<std::string, std::map<std::string, std::string>>
      target_calibration_map = {
          {CalibrationSetupInstruction_Name(base_acc_setup_instruction_),
           {{RmadComponent_Name(
                 RmadComponent::RMAD_COMPONENT_BASE_ACCELEROMETER),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_COMPLETE)},
            {RmadComponent_Name(RmadComponent::RMAD_COMPONENT_GYROSCOPE),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_COMPLETE)}}},
          {CalibrationSetupInstruction_Name(lid_acc_setup_instruction_),
           {{RmadComponent_Name(
                 RmadComponent::RMAD_COMPONENT_LID_ACCELEROMETER),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_WAITING)}}}};
  EXPECT_EQ(current_calibration_map, target_calibration_map);

  task_environment_.RunUntilIdle();
  EXPECT_EQ(overall_status_history_.size(), 1);
  EXPECT_EQ(overall_status_history_[0],
            RMAD_CALIBRATION_OVERALL_CURRENT_ROUND_COMPLETE);

  // No RunCalibrationState.
  RmadState state;

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kRunCalibration);
}

TEST_F(RunCalibrationStateHandlerTest, GetNextStateCase_NotFinished) {
  std::map<std::string, std::map<std::string, std::string>>
      current_calibration_map;
  const std::map<std::string, std::map<std::string, std::string>>
      predefined_calibration_map = {
          {CalibrationSetupInstruction_Name(base_acc_setup_instruction_),
           {{RmadComponent_Name(
                 RmadComponent::RMAD_COMPONENT_BASE_ACCELEROMETER),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_WAITING)},
            {RmadComponent_Name(RmadComponent::RMAD_COMPONENT_GYROSCOPE),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_WAITING)}}},
          {CalibrationSetupInstruction_Name(lid_acc_setup_instruction_),
           {{RmadComponent_Name(
                 RmadComponent::RMAD_COMPONENT_LID_ACCELEROMETER),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_WAITING)}}}};
  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationMap, predefined_calibration_map));

  auto handler =
      CreateStateHandler(true, {0.5, 1.0}, false, {}, true, {0.5, 1.0});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  task_environment_.FastForwardBy(RunCalibrationStateHandler::kPollInterval);
  EXPECT_EQ(progress_history_.size(), 2);
  EXPECT_EQ(progress_history_[0].progress(), 0.5);
  EXPECT_EQ(progress_history_[0].status(),
            CalibrationComponentStatus::RMAD_CALIBRATION_IN_PROGRESS);
  EXPECT_EQ(progress_history_[0].component(),
            RmadComponent::RMAD_COMPONENT_BASE_ACCELEROMETER);
  EXPECT_EQ(progress_history_[1].progress(), 0.5);
  EXPECT_EQ(progress_history_[1].status(),
            CalibrationComponentStatus::RMAD_CALIBRATION_IN_PROGRESS);
  EXPECT_EQ(progress_history_[1].component(),
            RmadComponent::RMAD_COMPONENT_GYROSCOPE);

  EXPECT_TRUE(json_store_->GetValue(kCalibrationMap, &current_calibration_map));
  const std::map<std::string, std::map<std::string, std::string>>
      target_calibration_map_one_interval = {
          {CalibrationSetupInstruction_Name(base_acc_setup_instruction_),
           {{RmadComponent_Name(
                 RmadComponent::RMAD_COMPONENT_BASE_ACCELEROMETER),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_IN_PROGRESS)},
            {RmadComponent_Name(RmadComponent::RMAD_COMPONENT_GYROSCOPE),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_IN_PROGRESS)}}},
          {CalibrationSetupInstruction_Name(lid_acc_setup_instruction_),
           {{RmadComponent_Name(
                 RmadComponent::RMAD_COMPONENT_LID_ACCELEROMETER),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_WAITING)}}}};
  EXPECT_EQ(current_calibration_map, target_calibration_map_one_interval);

  RmadState state;
  state.set_allocated_run_calibration(new RunCalibrationState);

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_CALIBRATION_FAILED);
  EXPECT_EQ(state_case, RmadState::StateCase::kCheckCalibration);
}

TEST_F(RunCalibrationStateHandlerTest, GetNextStateCase_UnknownComponent) {
  const std::map<std::string, std::map<std::string, std::string>>
      predefined_calibration_map = {
          {CalibrationSetupInstruction_Name(base_acc_setup_instruction_),
           {{RmadComponent_Name(RmadComponent::RMAD_COMPONENT_UNKNOWN),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_WAITING)},
            {RmadComponent_Name(RmadComponent::RMAD_COMPONENT_GYROSCOPE),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_WAITING)}}},
          {CalibrationSetupInstruction_Name(lid_acc_setup_instruction_),
           {{RmadComponent_Name(
                 RmadComponent::RMAD_COMPONENT_LID_ACCELEROMETER),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_WAITING)}}}};
  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationMap, predefined_calibration_map));

  auto handler = CreateStateHandler(false, {}, false, {}, false, {});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  task_environment_.RunUntilIdle();
  EXPECT_EQ(overall_status_history_.size(), 1);
  EXPECT_EQ(overall_status_history_[0],
            RMAD_CALIBRATION_OVERALL_INITIALIZATION_FAILED);

  RmadState state;
  state.set_allocated_run_calibration(new RunCalibrationState);

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_CALIBRATION_COMPONENT_MISSING);
  EXPECT_EQ(state_case, RmadState::StateCase::kCheckCalibration);
}

TEST_F(RunCalibrationStateHandlerTest, GetNextStateCase_InvalidComponent) {
  const std::map<std::string, std::map<std::string, std::string>>
      predefined_calibration_map = {
          {CalibrationSetupInstruction_Name(base_acc_setup_instruction_),
           {{RmadComponent_Name(RmadComponent::RMAD_COMPONENT_DRAM),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_WAITING)},
            {RmadComponent_Name(RmadComponent::RMAD_COMPONENT_GYROSCOPE),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_WAITING)}}},
          {CalibrationSetupInstruction_Name(lid_acc_setup_instruction_),
           {{RmadComponent_Name(
                 RmadComponent::RMAD_COMPONENT_LID_ACCELEROMETER),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_WAITING)}}}};
  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationMap, predefined_calibration_map));

  auto handler = CreateStateHandler(false, {}, false, {}, false, {});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  task_environment_.RunUntilIdle();
  EXPECT_EQ(overall_status_history_.size(), 1);
  EXPECT_EQ(overall_status_history_[0],
            RMAD_CALIBRATION_OVERALL_INITIALIZATION_FAILED);

  RmadState state;
  state.set_allocated_run_calibration(new RunCalibrationState);

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_CALIBRATION_COMPONENT_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kCheckCalibration);
}

TEST_F(RunCalibrationStateHandlerTest, GetNextStateCase_UnknownStatus) {
  const std::map<std::string, std::map<std::string, std::string>>
      predefined_calibration_map = {
          {CalibrationSetupInstruction_Name(base_acc_setup_instruction_),
           {{RmadComponent_Name(
                 RmadComponent::RMAD_COMPONENT_BASE_ACCELEROMETER),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_UNKNOWN)},
            {RmadComponent_Name(RmadComponent::RMAD_COMPONENT_GYROSCOPE),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_WAITING)}}},
          {CalibrationSetupInstruction_Name(lid_acc_setup_instruction_),
           {{RmadComponent_Name(
                 RmadComponent::RMAD_COMPONENT_LID_ACCELEROMETER),
             CalibrationComponentStatus::CalibrationStatus_Name(
                 CalibrationComponentStatus::RMAD_CALIBRATION_WAITING)}}}};
  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationMap, predefined_calibration_map));

  auto handler = CreateStateHandler(false, {}, false, {}, false, {});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  task_environment_.RunUntilIdle();
  EXPECT_EQ(overall_status_history_.size(), 1);
  EXPECT_EQ(overall_status_history_[0],
            RMAD_CALIBRATION_OVERALL_INITIALIZATION_FAILED);

  RmadState state;
  state.set_allocated_run_calibration(new RunCalibrationState);

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_CALIBRATION_STATUS_MISSING);
  EXPECT_EQ(state_case, RmadState::StateCase::kCheckCalibration);
}

}  // namespace rmad
