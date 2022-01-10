// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/setup_calibration_state_handler.h"

#include <map>
#include <memory>
#include <set>
#include <string>

#include <base/files/file_util.h>
#include <base/strings/string_number_conversions.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rmad/state_handler/state_handler_test_common.h"
#include "rmad/utils/calibration_utils.h"
#include "rmad/utils/mock_iio_sensor_probe_utils.h"

using testing::_;
using testing::Assign;
using testing::DoAll;
using testing::NiceMock;
using testing::Return;
using testing::StrictMock;

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

  scoped_refptr<SetupCalibrationStateHandler> CreateStateHandler(
      const std::set<RmadComponent>& probed_components) {
    auto mock_runtime_probe_client =
        std::make_unique<NiceMock<MockIioSensorProbeUtils>>();
    ON_CALL(*mock_runtime_probe_client, Probe())
        .WillByDefault(Return(probed_components));
    return base::MakeRefCounted<SetupCalibrationStateHandler>(
        json_store_, std::move(mock_runtime_probe_client));
  }
};

TEST_F(SetupCalibrationStateHandlerTest, InitializeState_SuccessSensorProbed) {
  auto handler = CreateStateHandler(
      {RMAD_COMPONENT_BASE_ACCELEROMETER, RMAD_COMPONENT_LID_ACCELEROMETER,
       RMAD_COMPONENT_BASE_GYROSCOPE, RMAD_COMPONENT_LID_GYROSCOPE});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  const std::map<std::string, std::map<std::string, std::string>>
      target_calibration_map = {{kBaseInstructionName,
                                 {{kBaseAccName, kStatusWaitingName},
                                  {kBaseGyroName, kStatusWaitingName}}},
                                {kLidInstructionName,
                                 {{kLidAccName, kStatusWaitingName},
                                  {kLidGyroName, kStatusWaitingName}}}};
  std::map<std::string, std::map<std::string, std::string>> calibration_map;
  EXPECT_TRUE(json_store_->GetValue(kCalibrationMap, &calibration_map));
  EXPECT_EQ(target_calibration_map, calibration_map);
}

TEST_F(SetupCalibrationStateHandlerTest,
       InitializeState_SuccessUnknownComponentProbed) {
  auto handler = CreateStateHandler(
      {RMAD_COMPONENT_BASE_ACCELEROMETER, RMAD_COMPONENT_LID_ACCELEROMETER,
       RMAD_COMPONENT_BASE_GYROSCOPE, RMAD_COMPONENT_LID_GYROSCOPE,
       RMAD_COMPONENT_UNKNOWN});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  const std::map<std::string, std::map<std::string, std::string>>
      target_calibration_map = {{kBaseInstructionName,
                                 {{kBaseAccName, kStatusWaitingName},
                                  {kBaseGyroName, kStatusWaitingName}}},
                                {kLidInstructionName,
                                 {{kLidAccName, kStatusWaitingName},
                                  {kLidGyroName, kStatusWaitingName}}}};
  std::map<std::string, std::map<std::string, std::string>> calibration_map;
  EXPECT_TRUE(json_store_->GetValue(kCalibrationMap, &calibration_map));
  EXPECT_EQ(target_calibration_map, calibration_map);
}

TEST_F(SetupCalibrationStateHandlerTest,
       InitializeState_SuccessInvalidomponentProbed) {
  auto handler = CreateStateHandler(
      {RMAD_COMPONENT_BASE_ACCELEROMETER, RMAD_COMPONENT_LID_ACCELEROMETER,
       RMAD_COMPONENT_BASE_GYROSCOPE, RMAD_COMPONENT_LID_GYROSCOPE,
       RMAD_COMPONENT_BATTERY});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  const std::map<std::string, std::map<std::string, std::string>>
      target_calibration_map = {{kBaseInstructionName,
                                 {{kBaseAccName, kStatusWaitingName},
                                  {kBaseGyroName, kStatusWaitingName}}},
                                {kLidInstructionName,
                                 {{kLidAccName, kStatusWaitingName},
                                  {kLidGyroName, kStatusWaitingName}}}};
  std::map<std::string, std::map<std::string, std::string>> calibration_map;
  EXPECT_TRUE(json_store_->GetValue(kCalibrationMap, &calibration_map));
  EXPECT_EQ(target_calibration_map, calibration_map);
}

TEST_F(SetupCalibrationStateHandlerTest, InitializeState_SuccessPredefined) {
  const std::map<std::string, std::map<std::string, std::string>>
      predefined_calibration_map = {{kBaseInstructionName,
                                     {{kBaseAccName, kStatusWaitingName},
                                      {kBaseGyroName, kStatusWaitingName}}},
                                    {kLidInstructionName,
                                     {{kLidAccName, kStatusWaitingName},
                                      {kLidGyroName, kStatusWaitingName}}}};
  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationMap, predefined_calibration_map));

  auto handler = CreateStateHandler(
      {RMAD_COMPONENT_BASE_ACCELEROMETER, RMAD_COMPONENT_LID_ACCELEROMETER,
       RMAD_COMPONENT_BASE_GYROSCOPE, RMAD_COMPONENT_LID_GYROSCOPE});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
}

TEST_F(SetupCalibrationStateHandlerTest,
       InitializeState_SuccessNotFinishedComponent) {
  const std::map<std::string, std::map<std::string, std::string>>
      predefined_calibration_map = {{kBaseInstructionName,
                                     {{kBaseAccName, kStatusInProgressName},
                                      {kBaseGyroName, kStatusInProgressName}}},
                                    {kLidInstructionName,
                                     {{kLidAccName, kStatusWaitingName},
                                      {kLidGyroName, kStatusWaitingName}}}};
  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationMap, predefined_calibration_map));

  auto handler = CreateStateHandler(
      {RMAD_COMPONENT_BASE_ACCELEROMETER, RMAD_COMPONENT_LID_ACCELEROMETER,
       RMAD_COMPONENT_BASE_GYROSCOPE, RMAD_COMPONENT_LID_GYROSCOPE});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
}

TEST_F(SetupCalibrationStateHandlerTest, InitializeState_JsonFailed) {
  base::SetPosixFilePermissions(GetStateFilePath(), 0444);

  auto handler = CreateStateHandler(
      {RMAD_COMPONENT_BASE_ACCELEROMETER, RMAD_COMPONENT_LID_ACCELEROMETER,
       RMAD_COMPONENT_BASE_GYROSCOPE, RMAD_COMPONENT_LID_GYROSCOPE});
  EXPECT_EQ(handler->InitializeState(),
            RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED);
}

TEST_F(SetupCalibrationStateHandlerTest, GetNextStateCase_Success) {
  auto handler = CreateStateHandler(
      {RMAD_COMPONENT_BASE_ACCELEROMETER, RMAD_COMPONENT_LID_ACCELEROMETER,
       RMAD_COMPONENT_BASE_GYROSCOPE, RMAD_COMPONENT_LID_GYROSCOPE});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  RmadState state = handler->GetState();
  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kRunCalibration);
}

TEST_F(SetupCalibrationStateHandlerTest,
       GetNextStateCase_SuccessNoNeedCalibration) {
  const std::map<std::string, std::map<std::string, std::string>>
      predefined_calibration_map = {{kBaseInstructionName,
                                     {{kBaseAccName, kStatusCompleteName},
                                      {kBaseGyroName, kStatusCompleteName}}},
                                    {kLidInstructionName,
                                     {{kLidAccName, kStatusSkipName},
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
  EXPECT_EQ(state_case, RmadState::StateCase::kFinalize);
}

TEST_F(SetupCalibrationStateHandlerTest,
       GetNextStateCase_SuccessNoNeedCalibrationKeepDeviceOpen) {
  EXPECT_TRUE(json_store_->SetValue(kKeepDeviceOpen, true));

  const std::map<std::string, std::map<std::string, std::string>>
      predefined_calibration_map = {{kBaseInstructionName,
                                     {{kBaseAccName, kStatusCompleteName},
                                      {kBaseGyroName, kStatusCompleteName}}},
                                    {kLidInstructionName,
                                     {{kLidAccName, kStatusSkipName},
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
  EXPECT_EQ(state_case, RmadState::StateCase::kWpEnablePhysical);
}

TEST_F(SetupCalibrationStateHandlerTest, GetNextStateCase_SuccessNoSensor) {
  auto handler = CreateStateHandler({});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  RmadState state = handler->GetState();
  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kFinalize);
}

TEST_F(SetupCalibrationStateHandlerTest,
       GetNextStateCase_SuccessNoSensorKeepDeviceOpen) {
  EXPECT_TRUE(json_store_->SetValue(kKeepDeviceOpen, true));

  auto handler = CreateStateHandler({});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  RmadState state = handler->GetState();
  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kWpEnablePhysical);
}

TEST_F(SetupCalibrationStateHandlerTest, GetNextStateCase_SuccessNeedToCheck) {
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

  RmadState state = handler->GetState();
  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kCheckCalibration);
}

TEST_F(SetupCalibrationStateHandlerTest,
       GetNextStateCase_SuccessNeedToCheckAutoTransition) {
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

  // Simulate the auto-transition scenario
  RmadState state = handler->GetState();

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kCheckCalibration);
}

TEST_F(SetupCalibrationStateHandlerTest, GetNextStateCase_MissingState) {
  auto handler = CreateStateHandler(
      {RMAD_COMPONENT_BASE_ACCELEROMETER, RMAD_COMPONENT_LID_ACCELEROMETER,
       RMAD_COMPONENT_BASE_GYROSCOPE, RMAD_COMPONENT_LID_GYROSCOPE});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // No SetupCalibrationState.
  RmadState state;

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kSetupCalibration);
}

TEST_F(SetupCalibrationStateHandlerTest,
       GetNextStateCase_ReadOnlyInstructionChanged) {
  auto handler = CreateStateHandler(
      {RMAD_COMPONENT_BASE_ACCELEROMETER, RMAD_COMPONENT_LID_ACCELEROMETER,
       RMAD_COMPONENT_BASE_GYROSCOPE, RMAD_COMPONENT_LID_GYROSCOPE});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  RmadState state = handler->GetState();
  auto setup_calibration_state =
      std::make_unique<SetupCalibrationState>(state.setup_calibration());
  setup_calibration_state->set_instruction(
      RMAD_CALIBRATION_INSTRUCTION_PLACE_LID_ON_FLAT_SURFACE);
  state.set_allocated_setup_calibration(setup_calibration_state.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kSetupCalibration);
}

TEST_F(SetupCalibrationStateHandlerTest, GetNextStateCase_NotInitialized) {
  auto handler = CreateStateHandler(
      {RMAD_COMPONENT_BASE_ACCELEROMETER, RMAD_COMPONENT_LID_ACCELEROMETER,
       RMAD_COMPONENT_BASE_GYROSCOPE, RMAD_COMPONENT_LID_GYROSCOPE});

  RmadState state;
  auto setup_calibration_state = std::make_unique<SetupCalibrationState>();
  state.set_allocated_setup_calibration(setup_calibration_state.release());

  // In order to be further checked by the user in kCheckCalibration, it should
  // return OK for transition.
  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kCheckCalibration);
}

TEST_F(SetupCalibrationStateHandlerTest, TryGetNextStateCaseAtBoot_Success) {
  auto handler = CreateStateHandler(
      {RMAD_COMPONENT_BASE_ACCELEROMETER, RMAD_COMPONENT_LID_ACCELEROMETER,
       RMAD_COMPONENT_BASE_GYROSCOPE, RMAD_COMPONENT_LID_GYROSCOPE});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto [error, state_case] = handler->TryGetNextStateCaseAtBoot();
  EXPECT_EQ(error, RMAD_ERROR_TRANSITION_FAILED);
  EXPECT_EQ(state_case, RmadState::StateCase::kSetupCalibration);
}

TEST_F(SetupCalibrationStateHandlerTest,
       TryGetNextStateCaseAtBoot_SuccessNeedToCheck) {
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

  auto [error, state_case] = handler->TryGetNextStateCaseAtBoot();
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kCheckCalibration);
}

}  // namespace rmad
