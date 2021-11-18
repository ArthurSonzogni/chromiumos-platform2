// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/check_calibration_state_handler.h"

#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>

#include <base/files/file_util.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rmad/state_handler/state_handler_test_common.h"
#include "rmad/utils/mock_iio_sensor_probe_utils.h"

using testing::_;
using testing::DoAll;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;

namespace {

constexpr char kBaseInstructionName[] =
    "RMAD_CALIBRATION_INSTRUCTION_PLACE_BASE_ON_FLAT_SURFACE";
constexpr char kLidInstructionName[] =
    "RMAD_CALIBRATION_INSTRUCTION_PLACE_LID_ON_FLAT_SURFACE";

constexpr char kBaseAccName[] = "RMAD_COMPONENT_BASE_ACCELEROMETER";
constexpr char kLidAccName[] = "RMAD_COMPONENT_LID_ACCELEROMETER";
constexpr char kBaseGyroName[] = "RMAD_COMPONENT_BASE_GYROSCOPE";
constexpr char kLidGyroName[] = "RMAD_COMPONENT_LID_GYROSCOPE";

constexpr char kStatusWaitingName[] = "RMAD_CALIBRATION_WAITING";
constexpr char kStatusCompleteName[] = "RMAD_CALIBRATION_COMPLETE";
constexpr char kStatusInProgressName[] = "RMAD_CALIBRATION_IN_PROGRESS";
constexpr char kStatusSkipName[] = "RMAD_CALIBRATION_SKIP";
constexpr char kStatusFailedName[] = "RMAD_CALIBRATION_FAILED";

}  // namespace

namespace rmad {

class CheckCalibrationStateHandlerTest : public StateHandlerTest {
 public:
  scoped_refptr<CheckCalibrationStateHandler> CreateStateHandler(
      const std::set<RmadComponent>& probed_components) {
    auto mock_runtime_probe_client =
        std::make_unique<NiceMock<MockIioSensorProbeUtils>>();
    ON_CALL(*mock_runtime_probe_client, Probe())
        .WillByDefault(Return(probed_components));
    return base::MakeRefCounted<CheckCalibrationStateHandler>(
        json_store_, std::move(mock_runtime_probe_client));
  }

 protected:
  void SetUp() override { StateHandlerTest::SetUp(); }
};

TEST_F(CheckCalibrationStateHandlerTest, InitializeState_Success) {
  auto handler = CreateStateHandler({});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
}

TEST_F(CheckCalibrationStateHandlerTest,
       InitializeState_SuccessUnknownComponentProbed) {
  const std::map<std::string, std::map<std::string, std::string>>
      predefined_calibration_map = {{kBaseInstructionName,
                                     {{kBaseAccName, kStatusCompleteName},
                                      {kBaseGyroName, kStatusCompleteName}}},
                                    {kLidInstructionName,
                                     {{kLidAccName, kStatusCompleteName},
                                      {kLidGyroName, kStatusCompleteName}}}};
  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationMap, predefined_calibration_map));

  auto handler = CreateStateHandler(
      {RMAD_COMPONENT_BASE_ACCELEROMETER, RMAD_COMPONENT_LID_ACCELEROMETER,
       RMAD_COMPONENT_BASE_GYROSCOPE, RMAD_COMPONENT_LID_GYROSCOPE,
       RMAD_COMPONENT_UNKNOWN});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
}

TEST_F(CheckCalibrationStateHandlerTest,
       InitializeState_SuccessInvalidComponentProbed) {
  const std::map<std::string, std::map<std::string, std::string>>
      predefined_calibration_map = {{kBaseInstructionName,
                                     {{kBaseAccName, kStatusCompleteName},
                                      {kBaseGyroName, kStatusCompleteName}}},
                                    {kLidInstructionName,
                                     {{kLidAccName, kStatusCompleteName},
                                      {kLidGyroName, kStatusCompleteName}}}};
  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationMap, predefined_calibration_map));

  auto handler = CreateStateHandler(
      {RMAD_COMPONENT_BASE_ACCELEROMETER, RMAD_COMPONENT_LID_ACCELEROMETER,
       RMAD_COMPONENT_BASE_GYROSCOPE, RMAD_COMPONENT_LID_GYROSCOPE,
       RMAD_COMPONENT_BATTERY});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
}

TEST_F(CheckCalibrationStateHandlerTest,
       InitializeState_SuccessUndeterminedStatus) {
  const std::map<std::string, std::map<std::string, std::string>>
      predefined_calibration_map = {{kBaseInstructionName,
                                     {{kBaseAccName, kStatusCompleteName},
                                      {kBaseGyroName, kStatusCompleteName}}},
                                    {kLidInstructionName,
                                     {{kLidAccName, kStatusInProgressName},
                                      {kLidGyroName, kStatusInProgressName}}}};
  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationMap, predefined_calibration_map));

  auto handler = CreateStateHandler(
      {RMAD_COMPONENT_BASE_ACCELEROMETER, RMAD_COMPONENT_LID_ACCELEROMETER,
       RMAD_COMPONENT_BASE_GYROSCOPE, RMAD_COMPONENT_LID_GYROSCOPE});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // All undetermined statuses should be marked as failed, because we expect
  // everything to be done in this state.
  std::map<std::string, std::map<std::string, std::string>>
      current_calibration_map;
  EXPECT_TRUE(json_store_->GetValue(kCalibrationMap, &current_calibration_map));
  const std::map<std::string, std::map<std::string, std::string>>
      target_calibration_map = {{kBaseInstructionName,
                                 {{kBaseAccName, kStatusCompleteName},
                                  {kBaseGyroName, kStatusCompleteName}}},
                                {kLidInstructionName,
                                 {{kLidAccName, kStatusFailedName},
                                  {kLidGyroName, kStatusFailedName}}}};
  EXPECT_EQ(current_calibration_map, target_calibration_map);
}

TEST_F(CheckCalibrationStateHandlerTest, InitializeState_JsonFailed) {
  const std::map<std::string, std::map<std::string, std::string>>
      predefined_calibration_map = {{kBaseInstructionName,
                                     {{kBaseAccName, kStatusCompleteName},
                                      {kBaseGyroName, kStatusCompleteName}}},
                                    {kLidInstructionName,
                                     {{kLidAccName, kStatusInProgressName},
                                      {kLidGyroName, kStatusInProgressName}}}};
  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationMap, predefined_calibration_map));

  base::SetPosixFilePermissions(GetStateFilePath(), 0444);

  auto handler = CreateStateHandler(
      {RMAD_COMPONENT_BASE_ACCELEROMETER, RMAD_COMPONENT_LID_ACCELEROMETER,
       RMAD_COMPONENT_BASE_GYROSCOPE, RMAD_COMPONENT_LID_GYROSCOPE});
  EXPECT_EQ(handler->InitializeState(),
            RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED);
}

TEST_F(CheckCalibrationStateHandlerTest, GetNextStateCase_Success) {
  const std::map<std::string, std::map<std::string, std::string>>
      predefined_calibration_map = {{kBaseInstructionName,
                                     {{kBaseAccName, kStatusCompleteName},
                                      {kBaseGyroName, kStatusCompleteName}}},
                                    {kLidInstructionName,
                                     {{kLidAccName, kStatusCompleteName},
                                      {kLidGyroName, kStatusCompleteName}}}};
  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationMap, predefined_calibration_map));

  auto handler = CreateStateHandler(
      {RMAD_COMPONENT_BASE_ACCELEROMETER, RMAD_COMPONENT_LID_ACCELEROMETER,
       RMAD_COMPONENT_BASE_GYROSCOPE, RMAD_COMPONENT_LID_GYROSCOPE});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  RmadState state = handler->GetState();

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kProvisionDevice);

  std::map<std::string, std::map<std::string, std::string>>
      current_calibration_map;
  EXPECT_TRUE(json_store_->GetValue(kCalibrationMap, &current_calibration_map));

  const std::map<std::string, std::map<std::string, std::string>>
      target_calibration_map = {{kBaseInstructionName,
                                 {{kBaseAccName, kStatusCompleteName},
                                  {kBaseGyroName, kStatusCompleteName}}},
                                {kLidInstructionName,
                                 {{kLidAccName, kStatusCompleteName},
                                  {kLidGyroName, kStatusCompleteName}}}};

  EXPECT_EQ(current_calibration_map, target_calibration_map);
}

TEST_F(CheckCalibrationStateHandlerTest,
       GetNextStateCase_SuccessNeedCalibration) {
  const std::map<std::string, std::map<std::string, std::string>>
      predefined_calibration_map = {{kBaseInstructionName,
                                     {{kBaseAccName, kStatusFailedName},
                                      {kBaseGyroName, kStatusCompleteName}}},
                                    {kLidInstructionName,
                                     {{kLidAccName, kStatusFailedName},
                                      {kLidGyroName, kStatusCompleteName}}}};
  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationMap, predefined_calibration_map));

  auto handler = CreateStateHandler(
      {RMAD_COMPONENT_BASE_ACCELEROMETER, RMAD_COMPONENT_LID_ACCELEROMETER,
       RMAD_COMPONENT_BASE_GYROSCOPE, RMAD_COMPONENT_LID_GYROSCOPE});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  std::unique_ptr<CheckCalibrationState> check_calibration =
      std::make_unique<CheckCalibrationState>();
  auto base_accelerometer = check_calibration->add_components();
  base_accelerometer->set_component(RMAD_COMPONENT_BASE_ACCELEROMETER);
  base_accelerometer->set_status(
      CalibrationComponentStatus::RMAD_CALIBRATION_WAITING);
  auto lid_accelerometer = check_calibration->add_components();
  lid_accelerometer->set_component(RMAD_COMPONENT_LID_ACCELEROMETER);
  lid_accelerometer->set_status(
      CalibrationComponentStatus::RMAD_CALIBRATION_WAITING);
  auto base_gyroscope = check_calibration->add_components();
  base_gyroscope->set_component(RMAD_COMPONENT_BASE_GYROSCOPE);
  base_gyroscope->set_status(
      CalibrationComponentStatus::RMAD_CALIBRATION_COMPLETE);
  auto lid_gyroscope = check_calibration->add_components();
  lid_gyroscope->set_component(RMAD_COMPONENT_LID_GYROSCOPE);
  lid_gyroscope->set_status(
      CalibrationComponentStatus::RMAD_CALIBRATION_COMPLETE);

  RmadState state;
  state.set_allocated_check_calibration(check_calibration.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kSetupCalibration);

  std::map<std::string, std::map<std::string, std::string>>
      current_calibration_map;
  EXPECT_TRUE(json_store_->GetValue(kCalibrationMap, &current_calibration_map));

  const std::map<std::string, std::map<std::string, std::string>>
      target_calibration_map = {{kBaseInstructionName,
                                 {{kBaseAccName, kStatusWaitingName},
                                  {kBaseGyroName, kStatusCompleteName}}},
                                {kLidInstructionName,
                                 {{kLidAccName, kStatusWaitingName},
                                  {kLidGyroName, kStatusCompleteName}}}};

  EXPECT_EQ(current_calibration_map, target_calibration_map);
}

TEST_F(CheckCalibrationStateHandlerTest, GetNextStateCase_SuccessSkipSensors) {
  const std::map<std::string, std::map<std::string, std::string>>
      predefined_calibration_map = {{kBaseInstructionName,
                                     {{kBaseAccName, kStatusFailedName},
                                      {kBaseGyroName, kStatusCompleteName}}},
                                    {kLidInstructionName,
                                     {{kLidAccName, kStatusFailedName},
                                      {kLidGyroName, kStatusCompleteName}}}};
  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationMap, predefined_calibration_map));

  auto handler = CreateStateHandler(
      {RMAD_COMPONENT_BASE_ACCELEROMETER, RMAD_COMPONENT_LID_ACCELEROMETER,
       RMAD_COMPONENT_BASE_GYROSCOPE, RMAD_COMPONENT_LID_GYROSCOPE});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  std::unique_ptr<CheckCalibrationState> check_calibration =
      std::make_unique<CheckCalibrationState>();
  auto base_accelerometer = check_calibration->add_components();
  base_accelerometer->set_component(RMAD_COMPONENT_BASE_ACCELEROMETER);
  base_accelerometer->set_status(
      CalibrationComponentStatus::RMAD_CALIBRATION_SKIP);
  auto lid_accelerometer = check_calibration->add_components();
  lid_accelerometer->set_component(RMAD_COMPONENT_LID_ACCELEROMETER);
  lid_accelerometer->set_status(
      CalibrationComponentStatus::RMAD_CALIBRATION_SKIP);
  auto base_gyroscope = check_calibration->add_components();
  base_gyroscope->set_component(RMAD_COMPONENT_BASE_GYROSCOPE);
  base_gyroscope->set_status(
      CalibrationComponentStatus::RMAD_CALIBRATION_COMPLETE);
  auto lid_gyroscope = check_calibration->add_components();
  lid_gyroscope->set_component(RMAD_COMPONENT_LID_GYROSCOPE);
  lid_gyroscope->set_status(
      CalibrationComponentStatus::RMAD_CALIBRATION_COMPLETE);

  RmadState state;
  state.set_allocated_check_calibration(check_calibration.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kProvisionDevice);

  std::map<std::string, std::map<std::string, std::string>>
      current_calibration_map;
  EXPECT_TRUE(json_store_->GetValue(kCalibrationMap, &current_calibration_map));

  const std::map<std::string, std::map<std::string, std::string>>
      target_calibration_map = {{kBaseInstructionName,
                                 {{kBaseAccName, kStatusSkipName},
                                  {kBaseGyroName, kStatusCompleteName}}},
                                {kLidInstructionName,
                                 {{kLidAccName, kStatusSkipName},
                                  {kLidGyroName, kStatusCompleteName}}}};

  EXPECT_EQ(current_calibration_map, target_calibration_map);
}

TEST_F(CheckCalibrationStateHandlerTest, GetNextStateCase_WrongComponentsSize) {
  const std::map<std::string, std::map<std::string, std::string>>
      predefined_calibration_map = {{kBaseInstructionName,
                                     {{kBaseAccName, kStatusFailedName},
                                      {kBaseGyroName, kStatusCompleteName}}},
                                    {kLidInstructionName,
                                     {{kLidAccName, kStatusFailedName},
                                      {kLidGyroName, kStatusCompleteName}}}};
  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationMap, predefined_calibration_map));

  auto handler = CreateStateHandler(
      {RMAD_COMPONENT_BASE_ACCELEROMETER, RMAD_COMPONENT_LID_ACCELEROMETER,
       RMAD_COMPONENT_BASE_GYROSCOPE, RMAD_COMPONENT_LID_GYROSCOPE});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  std::unique_ptr<CheckCalibrationState> check_calibration =
      std::make_unique<CheckCalibrationState>();
  auto base_accelerometer = check_calibration->add_components();
  base_accelerometer->set_component(RMAD_COMPONENT_BASE_ACCELEROMETER);
  base_accelerometer->set_status(
      CalibrationComponentStatus::RMAD_CALIBRATION_SKIP);
  auto lid_accelerometer = check_calibration->add_components();
  lid_accelerometer->set_component(RMAD_COMPONENT_LID_ACCELEROMETER);
  lid_accelerometer->set_status(
      CalibrationComponentStatus::RMAD_CALIBRATION_SKIP);
  auto base_gyroscope = check_calibration->add_components();
  base_gyroscope->set_component(RMAD_COMPONENT_BASE_GYROSCOPE);
  base_gyroscope->set_status(
      CalibrationComponentStatus::RMAD_CALIBRATION_COMPLETE);

  RmadState state;
  state.set_allocated_check_calibration(check_calibration.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_CALIBRATION_COMPONENT_MISSING);
  EXPECT_EQ(state_case, RmadState::StateCase::kCheckCalibration);
}

TEST_F(CheckCalibrationStateHandlerTest, GetNextStateCase_UnknownComponent) {
  const std::map<std::string, std::map<std::string, std::string>>
      predefined_calibration_map = {{kBaseInstructionName,
                                     {{kBaseAccName, kStatusFailedName},
                                      {kBaseGyroName, kStatusCompleteName}}},
                                    {kLidInstructionName,
                                     {{kLidAccName, kStatusFailedName},
                                      {kLidGyroName, kStatusCompleteName}}}};
  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationMap, predefined_calibration_map));

  auto handler = CreateStateHandler(
      {RMAD_COMPONENT_BASE_ACCELEROMETER, RMAD_COMPONENT_LID_ACCELEROMETER,
       RMAD_COMPONENT_BASE_GYROSCOPE, RMAD_COMPONENT_LID_GYROSCOPE});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  std::unique_ptr<CheckCalibrationState> check_calibration =
      std::make_unique<CheckCalibrationState>();
  auto base_accelerometer = check_calibration->add_components();
  base_accelerometer->set_component(RMAD_COMPONENT_BASE_ACCELEROMETER);
  base_accelerometer->set_status(
      CalibrationComponentStatus::RMAD_CALIBRATION_WAITING);
  auto lid_accelerometer = check_calibration->add_components();
  lid_accelerometer->set_component(RMAD_COMPONENT_LID_ACCELEROMETER);
  lid_accelerometer->set_status(
      CalibrationComponentStatus::RMAD_CALIBRATION_WAITING);
  auto base_gyroscope = check_calibration->add_components();
  base_gyroscope->set_component(RMAD_COMPONENT_BASE_GYROSCOPE);
  base_gyroscope->set_status(
      CalibrationComponentStatus::RMAD_CALIBRATION_COMPLETE);
  auto unknown = check_calibration->add_components();
  unknown->set_component(RMAD_COMPONENT_UNKNOWN);
  unknown->set_status(CalibrationComponentStatus::RMAD_CALIBRATION_COMPLETE);

  RmadState state;
  state.set_allocated_check_calibration(check_calibration.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_CALIBRATION_COMPONENT_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kCheckCalibration);
}

TEST_F(CheckCalibrationStateHandlerTest, GetNextStateCase_DecisionNotMade) {
  const std::map<std::string, std::map<std::string, std::string>>
      predefined_calibration_map = {{kBaseInstructionName,
                                     {{kBaseAccName, kStatusFailedName},
                                      {kBaseGyroName, kStatusCompleteName}}},
                                    {kLidInstructionName,
                                     {{kLidAccName, kStatusFailedName},
                                      {kLidGyroName, kStatusCompleteName}}}};
  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationMap, predefined_calibration_map));

  auto handler = CreateStateHandler(
      {RMAD_COMPONENT_BASE_ACCELEROMETER, RMAD_COMPONENT_LID_ACCELEROMETER,
       RMAD_COMPONENT_BASE_GYROSCOPE, RMAD_COMPONENT_LID_GYROSCOPE});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  std::unique_ptr<CheckCalibrationState> check_calibration =
      std::make_unique<CheckCalibrationState>();
  auto base_accelerometer = check_calibration->add_components();
  base_accelerometer->set_component(RMAD_COMPONENT_BASE_ACCELEROMETER);
  base_accelerometer->set_status(
      CalibrationComponentStatus::RMAD_CALIBRATION_WAITING);
  auto lid_accelerometer = check_calibration->add_components();
  lid_accelerometer->set_component(RMAD_COMPONENT_LID_ACCELEROMETER);
  lid_accelerometer->set_status(
      CalibrationComponentStatus::RMAD_CALIBRATION_FAILED);
  auto base_gyroscope = check_calibration->add_components();
  base_gyroscope->set_component(RMAD_COMPONENT_BASE_GYROSCOPE);
  base_gyroscope->set_status(
      CalibrationComponentStatus::RMAD_CALIBRATION_COMPLETE);
  auto lid_gyroscope = check_calibration->add_components();
  lid_gyroscope->set_component(RMAD_COMPONENT_LID_GYROSCOPE);
  lid_gyroscope->set_status(
      CalibrationComponentStatus::RMAD_CALIBRATION_COMPLETE);

  RmadState state;
  state.set_allocated_check_calibration(check_calibration.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_CALIBRATION_STATUS_MISSING);
  EXPECT_EQ(state_case, RmadState::StateCase::kCheckCalibration);
}

TEST_F(CheckCalibrationStateHandlerTest, GetNextStateCase_MissingState) {
  const std::map<std::string, std::map<std::string, std::string>>
      predefined_calibration_map = {{kBaseInstructionName,
                                     {{kBaseAccName, kStatusFailedName},
                                      {kBaseGyroName, kStatusCompleteName}}},
                                    {kLidInstructionName,
                                     {{kLidAccName, kStatusFailedName},
                                      {kLidGyroName, kStatusCompleteName}}}};
  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationMap, predefined_calibration_map));

  auto handler = CreateStateHandler(
      {RMAD_COMPONENT_BASE_ACCELEROMETER, RMAD_COMPONENT_LID_ACCELEROMETER,
       RMAD_COMPONENT_BASE_GYROSCOPE, RMAD_COMPONENT_LID_GYROSCOPE});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  RmadState state;
  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kCheckCalibration);
}

}  // namespace rmad
