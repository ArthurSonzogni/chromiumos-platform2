// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/base_state_handler.h"

#include <set>
#include <string>
#include <vector>

#include <base/files/file_util.h>
#include <base/test/task_environment.h>
#include <gtest/gtest.h>

#include "rmad/constants.h"
#include "rmad/metrics/metrics_constants.h"
#include "rmad/state_handler/state_handler_test_common.h"

namespace {

const std::vector<rmad::RmadState::StateCase> kAllStateCases = {
    rmad::RmadState::kWelcome,           rmad::RmadState::kComponentsRepair,
    rmad::RmadState::kDeviceDestination, rmad::RmadState::kWpDisableMethod,
    rmad::RmadState::kWpDisableRsu,      rmad::RmadState::kVerifyRsu,
    rmad::RmadState::kWpDisablePhysical, rmad::RmadState::kWpDisableComplete,
    rmad::RmadState::kUpdateRoFirmware,  rmad::RmadState::kRestock,
    rmad::RmadState::kUpdateDeviceInfo,  rmad::RmadState::kCheckCalibration,
    rmad::RmadState::kSetupCalibration,  rmad::RmadState::kRunCalibration,
    rmad::RmadState::kProvisionDevice,   rmad::RmadState::kWpEnablePhysical,
    rmad::RmadState::kFinalize,          rmad::RmadState::kRepairComplete,
    rmad::RmadState::STATE_NOT_SET};

constexpr double kDelayTimeInSec = 1.0;

}  // namespace

namespace rmad {

class TestBaseStateHandler : public BaseStateHandler {
 public:
  explicit TestBaseStateHandler(scoped_refptr<JsonStore> json_store)
      : BaseStateHandler(json_store) {}

  RmadState::StateCase GetStateCase() const override {
    return RmadState::STATE_NOT_SET;
  }

  SET_REPEATABLE

  RmadErrorCode InitializeState() override { return RMAD_ERROR_OK; }

  GetNextStateCaseReply GetNextStateCase(const RmadState& state) override {
    return {.error = RMAD_ERROR_OK, .state_case = RmadState::STATE_NOT_SET};
  }

  RmadState& GetStateInternalForTest() { return state_; }

 protected:
  ~TestBaseStateHandler() override = default;
};

class TestUnrepeatableBaseStateHandler : public BaseStateHandler {
 public:
  explicit TestUnrepeatableBaseStateHandler(scoped_refptr<JsonStore> json_store)
      : BaseStateHandler(json_store) {}

  RmadState::StateCase GetStateCase() const override {
    return RmadState::STATE_NOT_SET;
  }

  SET_UNREPEATABLE

  RmadErrorCode InitializeState() override { return RMAD_ERROR_OK; }

  GetNextStateCaseReply GetNextStateCase(const RmadState& state) override {
    return {.error = RMAD_ERROR_OK, .state_case = RmadState::STATE_NOT_SET};
  }

 protected:
  ~TestUnrepeatableBaseStateHandler() override = default;
};

class BaseStateHandlerTest : public StateHandlerTest {
 public:
  scoped_refptr<TestBaseStateHandler> CreateStateHandler() {
    return base::MakeRefCounted<TestBaseStateHandler>(json_store_);
  }

  scoped_refptr<TestUnrepeatableBaseStateHandler>
  CreateUnrepeatableStateHandler() {
    return base::MakeRefCounted<TestUnrepeatableBaseStateHandler>(json_store_);
  }

 protected:
  // Variables for TaskRunner.
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::ThreadPoolExecutionMode::ASYNC,
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};

  void SetUp() override { StateHandlerTest::SetUp(); }
};

TEST_F(BaseStateHandlerTest, CleanUpState_Success) {
  auto handler = CreateStateHandler();
  handler->CleanUpState();
}

TEST_F(BaseStateHandlerTest, RegisterSignalSender_Success) {
  auto handler = CreateStateHandler();
  handler->RegisterSignalSender(
      std::make_unique<base::RepeatingCallback<bool(bool)>>());
  handler->RegisterSignalSender(
      std::make_unique<
          rmad::BaseStateHandler::HardwareVerificationResultSignalCallback>());
  handler->RegisterSignalSender(
      std::make_unique<
          rmad::BaseStateHandler::CalibrationOverallSignalCallback>());
  handler->RegisterSignalSender(
      std::make_unique<
          rmad::BaseStateHandler::CalibrationComponentSignalCallback>());
  handler->RegisterSignalSender(
      std::make_unique<rmad::BaseStateHandler::ProvisionSignalCallback>());
  handler->RegisterSignalSender(
      std::make_unique<rmad::BaseStateHandler::FinalizeSignalCallback>());
}

TEST_F(BaseStateHandlerTest, IsRepeatable_RepeatableSuccess) {
  auto handler = CreateStateHandler();
  EXPECT_TRUE(handler->IsRepeatable());
}

TEST_F(BaseStateHandlerTest, IsRepeatable_UnrepeatableSuccess) {
  auto handler = CreateUnrepeatableStateHandler();
  EXPECT_FALSE(handler->IsRepeatable());
}

TEST_F(BaseStateHandlerTest, StoreState_EmptySuccess) {
  auto handler = CreateStateHandler();
  EXPECT_TRUE(handler->StoreState());
}

TEST_F(BaseStateHandlerTest, StoreState_WelcomeSuccess) {
  auto handler = CreateStateHandler();
  EXPECT_FALSE(handler->GetStateInternalForTest().has_welcome());
  handler->GetStateInternalForTest().set_allocated_welcome(new WelcomeState);
  EXPECT_TRUE(handler->GetStateInternalForTest().has_welcome());
  EXPECT_TRUE(handler->StoreState());
}

TEST_F(BaseStateHandlerTest, RetrieveState_EmptyFailed) {
  auto handler = CreateStateHandler();
  EXPECT_FALSE(handler->RetrieveState());
}

TEST_F(BaseStateHandlerTest, RetrieveState_EmptyStateSuccess) {
  auto handler = CreateStateHandler();
  EXPECT_TRUE(handler->StoreState());

  auto handler2 = CreateStateHandler();
  EXPECT_TRUE(handler2->RetrieveState());
}

TEST_F(BaseStateHandlerTest, RetrieveState_WelcomeStateSuccess) {
  auto handler = CreateStateHandler();
  EXPECT_FALSE(handler->GetStateInternalForTest().has_welcome());
  handler->GetStateInternalForTest().set_allocated_welcome(new WelcomeState);
  EXPECT_TRUE(handler->GetStateInternalForTest().has_welcome());
  EXPECT_TRUE(handler->StoreState());

  auto handler2 = CreateStateHandler();
  EXPECT_TRUE(handler2->RetrieveState());
  EXPECT_TRUE(handler2->GetStateInternalForTest().has_welcome());
}

TEST_F(BaseStateHandlerTest, StoreErrorCode_Success) {
  std::vector<std::string> target_occurred_errors;
  for (int i = RmadErrorCode_MIN; i <= RmadErrorCode_MAX; i++) {
    auto handler = CreateStateHandler();
    RmadErrorCode error_code = static_cast<RmadErrorCode>(i);
    EXPECT_TRUE(handler->StoreErrorCode(error_code));

    if (std::find(kExpectedErrorCodes.begin(), kExpectedErrorCodes.end(),
                  error_code) == kExpectedErrorCodes.end()) {
      target_occurred_errors.push_back(RmadErrorCode_Name(error_code));
    }
  }

  std::vector<std::string> occurred_errors;
  json_store_->GetValue(kOccurredErrors, &occurred_errors);
  EXPECT_EQ(occurred_errors, target_occurred_errors);
}

TEST_F(BaseStateHandlerTest, StoreErrorCode_Failed) {
  base::SetPosixFilePermissions(GetStateFilePath(), 0444);

  for (int i = RmadErrorCode_MIN; i <= RmadErrorCode_MAX; i++) {
    auto handler = CreateStateHandler();
    RmadErrorCode error_code = static_cast<RmadErrorCode>(i);
    if (std::find(kExpectedErrorCodes.begin(), kExpectedErrorCodes.end(),
                  error_code) == kExpectedErrorCodes.end()) {
      EXPECT_FALSE(handler->StoreErrorCode(error_code));
    } else {
      EXPECT_TRUE(handler->StoreErrorCode(error_code));
    }
  }
}

TEST_F(BaseStateHandlerTest, StoreAdditionalActivity_NothingSuccess) {
  auto handler = CreateStateHandler();
  EXPECT_TRUE(handler->StoreAdditionalActivity(AdditionalActivity::NOTHING));
}

TEST_F(BaseStateHandlerTest, StoreAdditionalActivity_Success) {
  std::vector<int> target_additional_activities;

  for (AdditionalActivity activity : kValidAdditionalActivities) {
    auto handler = CreateStateHandler();
    if (std::find(kExpectedPowerCycleActivities.begin(),
                  kExpectedPowerCycleActivities.end(),
                  activity) != kExpectedPowerCycleActivities.end()) {
      EXPECT_TRUE(json_store_->SetValue(kSetupTimestamp,
                                        base::Time::Now().ToDoubleT()));
      task_environment_.FastForwardBy(
          base::TimeDelta::FromSeconds(kDelayTimeInSec));

      double pre_running_time = 0.0;
      json_store_->GetValue(kRunningTime, &pre_running_time);

      EXPECT_TRUE(handler->StoreAdditionalActivity(activity));

      double running_time;
      EXPECT_TRUE(json_store_->GetValue(kRunningTime, &running_time));
      EXPECT_EQ(running_time - pre_running_time, kDelayTimeInSec);
    } else {
      EXPECT_TRUE(handler->StoreAdditionalActivity(activity));
    }

    if (activity != AdditionalActivity::NOTHING) {
      target_additional_activities.push_back(static_cast<int>(activity));
    }
  }

  std::vector<int> additional_activities;
  json_store_->GetValue(kAdditionalActivities, &additional_activities);
  EXPECT_EQ(additional_activities, target_additional_activities);
}

TEST_F(BaseStateHandlerTest, StoreAdditionalActivity_JsonFailed) {
  EXPECT_TRUE(
      json_store_->SetValue(kSetupTimestamp, base::Time::Now().ToDoubleT()));
  base::SetPosixFilePermissions(GetStateFilePath(), 0444);

  for (AdditionalActivity activity : kValidAdditionalActivities) {
    auto handler = CreateStateHandler();
    EXPECT_FALSE(handler->StoreAdditionalActivity(activity));
  }
}

TEST_F(BaseStateHandlerTest, StoreAdditionalActivity_RunningTimeFailed) {
  for (AdditionalActivity activity : kValidAdditionalActivities) {
    auto handler = CreateStateHandler();
    // If it does power cycle, it needs to calculate the running time.
    if (std::find(kExpectedPowerCycleActivities.begin(),
                  kExpectedPowerCycleActivities.end(),
                  activity) != kExpectedPowerCycleActivities.end()) {
      EXPECT_FALSE(handler->StoreAdditionalActivity(activity));
    } else {
      EXPECT_TRUE(handler->StoreAdditionalActivity(activity));
    }
  }
}

TEST_F(BaseStateHandlerTest, NextStateCaseWrapper_Sucesss) {
  std::vector<std::string> target_occurred_errors;
  std::vector<int> target_additional_activities;

  RmadState::StateCase state_case = RmadState::kWelcome;

  for (int i = RmadErrorCode_MIN; i <= RmadErrorCode_MAX; i++) {
    RmadErrorCode error_code = static_cast<RmadErrorCode>(i);
    auto handler = CreateStateHandler();

    BaseStateHandler::GetNextStateCaseReply reply =
        handler->NextStateCaseWrapper(state_case, error_code,
                                      AdditionalActivity::NOTHING);
    EXPECT_EQ(reply.state_case, state_case);
    EXPECT_EQ(reply.error, error_code);

    if (std::find(kExpectedErrorCodes.begin(), kExpectedErrorCodes.end(),
                  error_code) == kExpectedErrorCodes.end()) {
      target_occurred_errors.push_back(RmadErrorCode_Name(error_code));
    }
  }

  for (AdditionalActivity activity : kValidAdditionalActivities) {
    auto handler = CreateStateHandler();

    if (std::find(kExpectedPowerCycleActivities.begin(),
                  kExpectedPowerCycleActivities.end(),
                  activity) != kExpectedPowerCycleActivities.end()) {
      EXPECT_TRUE(json_store_->SetValue(kSetupTimestamp,
                                        base::Time::Now().ToDoubleT()));
      task_environment_.FastForwardBy(
          base::TimeDelta::FromSeconds(kDelayTimeInSec));

      double pre_running_time = 0.0;
      json_store_->GetValue(kRunningTime, &pre_running_time);

      BaseStateHandler::GetNextStateCaseReply reply =
          handler->NextStateCaseWrapper(state_case, RMAD_ERROR_OK, activity);
      EXPECT_EQ(reply.state_case, state_case);
      EXPECT_EQ(reply.error, RMAD_ERROR_OK);

      double running_time;
      EXPECT_TRUE(json_store_->GetValue(kRunningTime, &running_time));
      EXPECT_EQ(running_time - pre_running_time, kDelayTimeInSec);
    } else {
      BaseStateHandler::GetNextStateCaseReply reply =
          handler->NextStateCaseWrapper(state_case, RMAD_ERROR_OK, activity);
      EXPECT_EQ(reply.state_case, state_case);
      EXPECT_EQ(reply.error, RMAD_ERROR_OK);
    }

    if (activity != AdditionalActivity::NOTHING) {
      target_additional_activities.push_back(static_cast<int>(activity));
    }
  }

  std::vector<int> additional_activities;
  EXPECT_TRUE(
      json_store_->GetValue(kAdditionalActivities, &additional_activities));
  EXPECT_EQ(additional_activities, target_additional_activities);

  std::vector<std::string> occurred_errors;
  EXPECT_TRUE(json_store_->GetValue(kOccurredErrors, &occurred_errors));
  EXPECT_EQ(occurred_errors, target_occurred_errors);
}

TEST_F(BaseStateHandlerTest, NextStateCaseWrapper_JsonFailed) {
  base::SetPosixFilePermissions(GetStateFilePath(), 0444);

  std::vector<std::string> target_occurred_errors;

  RmadState::StateCase state_case = RmadState::kWelcome;

  for (int i = RmadErrorCode_MIN; i <= RmadErrorCode_MAX; i++) {
    RmadErrorCode error_code = static_cast<RmadErrorCode>(i);
    auto handler = CreateStateHandler();

    BaseStateHandler::GetNextStateCaseReply reply =
        handler->NextStateCaseWrapper(state_case, error_code,
                                      AdditionalActivity::NOTHING);
    EXPECT_EQ(reply.state_case, state_case);
    EXPECT_EQ(reply.error, error_code);
  }

  for (AdditionalActivity activity : kValidAdditionalActivities) {
    auto handler = CreateStateHandler();

    BaseStateHandler::GetNextStateCaseReply reply =
        handler->NextStateCaseWrapper(state_case, RMAD_ERROR_OK, activity);
    EXPECT_EQ(reply.state_case, state_case);
    EXPECT_EQ(reply.error, RMAD_ERROR_OK);
  }

  std::vector<int> additional_activities;
  json_store_->GetValue(kAdditionalActivities, &additional_activities);
  EXPECT_EQ(additional_activities, std::vector<int>());

  std::vector<std::string> occurred_errors;
  json_store_->GetValue(kOccurredErrors, &occurred_errors);
  EXPECT_EQ(occurred_errors, std::vector<std::string>());
}

TEST_F(BaseStateHandlerTest, NextStateCaseWrapper_RunningTimeFailed) {
  std::vector<std::string> target_occurred_errors;
  std::vector<int> target_additional_activities;

  RmadState::StateCase state_case = RmadState::kWelcome;

  for (int i = RmadErrorCode_MIN; i <= RmadErrorCode_MAX; i++) {
    RmadErrorCode error_code = static_cast<RmadErrorCode>(i);
    auto handler = CreateStateHandler();

    BaseStateHandler::GetNextStateCaseReply reply =
        handler->NextStateCaseWrapper(state_case, error_code,
                                      AdditionalActivity::NOTHING);
    EXPECT_EQ(reply.state_case, state_case);
    EXPECT_EQ(reply.error, error_code);

    if (std::find(kExpectedErrorCodes.begin(), kExpectedErrorCodes.end(),
                  error_code) == kExpectedErrorCodes.end()) {
      target_occurred_errors.push_back(RmadErrorCode_Name(error_code));
    }
  }

  for (AdditionalActivity activity : kValidAdditionalActivities) {
    auto handler = CreateStateHandler();

    BaseStateHandler::GetNextStateCaseReply reply =
        handler->NextStateCaseWrapper(state_case, RMAD_ERROR_OK, activity);
    EXPECT_EQ(reply.state_case, state_case);
    EXPECT_EQ(reply.error, RMAD_ERROR_OK);

    if (activity != AdditionalActivity::NOTHING &&
        std::find(kExpectedPowerCycleActivities.begin(),
                  kExpectedPowerCycleActivities.end(),
                  activity) == kExpectedPowerCycleActivities.end()) {
      target_additional_activities.push_back(static_cast<int>(activity));
    }
  }

  std::vector<int> additional_activities;
  EXPECT_TRUE(
      json_store_->GetValue(kAdditionalActivities, &additional_activities));
  EXPECT_EQ(additional_activities, target_additional_activities);

  std::vector<std::string> occurred_errors;
  EXPECT_TRUE(json_store_->GetValue(kOccurredErrors, &occurred_errors));
  EXPECT_EQ(occurred_errors, target_occurred_errors);
}

}  // namespace rmad
