// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <map>
#include <memory>
#include <string>

#include <base/strings/string_number_conversions.h>
#include <gtest/gtest.h>

#include "rmad/constants.h"
#include "rmad/state_handler/check_calibration_state_handler.h"
#include "rmad/state_handler/state_handler_test_common.h"

using CalibrationStatus = rmad::CheckCalibrationState::CalibrationStatus;

namespace rmad {

class CheckCalibrationStateHandlerTest : public StateHandlerTest {
 public:
  scoped_refptr<CheckCalibrationStateHandler> CreateStateHandler() {
    return base::MakeRefCounted<CheckCalibrationStateHandler>(json_store_);
  }
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
  auto accelerometer = check_calibration->add_components();
  accelerometer->set_name(
      CalibrationStatus::RMAD_CALIBRATION_COMPONENT_ACCELEROMETER);
  accelerometer->set_status(CalibrationStatus::RMAD_CALIBRATE_WAITING);
  auto gyroscope = check_calibration->add_components();
  gyroscope->set_name(CalibrationStatus::RMAD_CALIBRATION_COMPONENT_GYROSCOPE);
  gyroscope->set_status(CalibrationStatus::RMAD_CALIBRATE_WAITING);

  RmadState state;
  state.set_allocated_check_calibration(check_calibration.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kSetupCalibration);

  std::map<std::string, std::string> components_calibration_map;
  EXPECT_TRUE(
      json_store_->GetValue(kCalibrationMap, &components_calibration_map));

  const std::map<std::string, std::string> target_calibration_map = {
      {base::NumberToString(
           CalibrationStatus::RMAD_CALIBRATION_COMPONENT_ACCELEROMETER),
       base::NumberToString(CalibrationStatus::RMAD_CALIBRATE_WAITING)},
      {base::NumberToString(
           CalibrationStatus::RMAD_CALIBRATION_COMPONENT_GYROSCOPE),
       base::NumberToString(CalibrationStatus::RMAD_CALIBRATE_WAITING)}};

  EXPECT_EQ(components_calibration_map, target_calibration_map);
}

TEST_F(CheckCalibrationStateHandlerTest,
       GetNextStateCase_RetryCalibration_Success) {
  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  std::unique_ptr<CheckCalibrationState> check_calibration =
      std::make_unique<CheckCalibrationState>();
  auto accelerometer = check_calibration->add_components();
  accelerometer->set_name(
      CalibrationStatus::RMAD_CALIBRATION_COMPONENT_ACCELEROMETER);
  accelerometer->set_status(CalibrationStatus::RMAD_CALIBRATE_FAILED);
  auto gyroscope = check_calibration->add_components();
  gyroscope->set_name(CalibrationStatus::RMAD_CALIBRATION_COMPONENT_GYROSCOPE);
  gyroscope->set_status(CalibrationStatus::RMAD_CALIBRATE_COMPLETE);

  RmadState state;
  state.set_allocated_check_calibration(check_calibration.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kSetupCalibration);

  std::map<std::string, std::string> components_calibration_map;
  EXPECT_TRUE(
      json_store_->GetValue(kCalibrationMap, &components_calibration_map));

  const std::map<std::string, std::string> target_calibration_map = {
      {base::NumberToString(
           CalibrationStatus::RMAD_CALIBRATION_COMPONENT_ACCELEROMETER),
       base::NumberToString(CalibrationStatus::RMAD_CALIBRATE_FAILED)},
      {base::NumberToString(
           CalibrationStatus::RMAD_CALIBRATION_COMPONENT_GYROSCOPE),
       base::NumberToString(CalibrationStatus::RMAD_CALIBRATE_COMPLETE)}};

  EXPECT_EQ(components_calibration_map, target_calibration_map);
}

TEST_F(CheckCalibrationStateHandlerTest,
       GetNextStateCase_NoNeedCalibration_Success) {
  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  std::unique_ptr<CheckCalibrationState> check_calibration =
      std::make_unique<CheckCalibrationState>();
  auto accelerometer = check_calibration->add_components();
  accelerometer->set_name(
      CalibrationStatus::RMAD_CALIBRATION_COMPONENT_ACCELEROMETER);
  accelerometer->set_status(CalibrationStatus::RMAD_CALIBRATE_COMPLETE);
  auto gyroscope = check_calibration->add_components();
  gyroscope->set_name(CalibrationStatus::RMAD_CALIBRATION_COMPONENT_GYROSCOPE);
  gyroscope->set_status(CalibrationStatus::RMAD_CALIBRATE_SKIP);

  RmadState state;
  state.set_allocated_check_calibration(check_calibration.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kProvisionDevice);

  std::map<std::string, std::string> components_calibration_map;
  EXPECT_TRUE(
      json_store_->GetValue(kCalibrationMap, &components_calibration_map));

  const std::map<std::string, std::string> target_calibration_map = {
      {base::NumberToString(
           CalibrationStatus::RMAD_CALIBRATION_COMPONENT_ACCELEROMETER),
       base::NumberToString(CalibrationStatus::RMAD_CALIBRATE_COMPLETE)},
      {base::NumberToString(
           CalibrationStatus::RMAD_CALIBRATION_COMPONENT_GYROSCOPE),
       base::NumberToString(CalibrationStatus::RMAD_CALIBRATE_SKIP)}};

  EXPECT_EQ(components_calibration_map, target_calibration_map);
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
  auto unknown = check_calibration->add_components();
  unknown->set_name(CalibrationStatus::RMAD_CALIBRATION_COMPONENT_UNKNOWN);
  unknown->set_status(CalibrationStatus::RMAD_CALIBRATE_WAITING);

  RmadState state;
  state.set_allocated_check_calibration(check_calibration.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kCheckCalibration);
}

TEST_F(CheckCalibrationStateHandlerTest, GetNextStateCase_UnknownStatus) {
  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  std::unique_ptr<CheckCalibrationState> check_calibration =
      std::make_unique<CheckCalibrationState>();
  auto accelerometer = check_calibration->add_components();
  accelerometer->set_name(
      CalibrationStatus::RMAD_CALIBRATION_COMPONENT_ACCELEROMETER);
  accelerometer->set_status(CalibrationStatus::RMAD_CALIBRATE_UNKNOWN);

  RmadState state;
  state.set_allocated_check_calibration(check_calibration.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kCheckCalibration);
}

}  // namespace rmad
