// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <map>
#include <memory>
#include <string>

#include <base/strings/string_number_conversions.h>
#include <gtest/gtest.h>

#include "rmad/constants.h"
#include "rmad/state_handler/setup_calibration_state_handler.h"
#include "rmad/state_handler/state_handler_test_common.h"

using CalibrationStatus = rmad::CheckCalibrationState::CalibrationStatus;

namespace rmad {

class SetupCalibrationStateHandlerTest : public StateHandlerTest {
 public:
  scoped_refptr<SetupCalibrationStateHandler> CreateStateHandler() {
    return base::MakeRefCounted<SetupCalibrationStateHandler>(json_store_);
  }
};

TEST_F(SetupCalibrationStateHandlerTest, InitializeState_Success) {
  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
}

TEST_F(SetupCalibrationStateHandlerTest, GetNextStateCase_Success) {
  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  RmadState state;
  state.set_allocated_setup_calibration(new SetupCalibrationState);

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kRunCalibration);
}

TEST_F(SetupCalibrationStateHandlerTest, GetNextStateCase_MissingState) {
  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // No CheckCalibrationState.
  RmadState state;

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kSetupCalibration);
}

}  // namespace rmad
