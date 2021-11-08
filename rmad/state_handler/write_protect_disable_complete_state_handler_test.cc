// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>

#include <base/memory/scoped_refptr.h>
#include <gtest/gtest.h>

#include "rmad/constants.h"
#include "rmad/proto_bindings/rmad.pb.h"
#include "rmad/state_handler/state_handler_test_common.h"
#include "rmad/state_handler/write_protect_disable_complete_state_handler.h"

namespace rmad {

class WriteProtectDisableCompleteStateHandlerTest : public StateHandlerTest {
 public:
  scoped_refptr<WriteProtectDisableCompleteStateHandler> CreateStateHandler(
      bool keep_device_open, bool wp_disable_skipped) {
    EXPECT_TRUE(json_store_->SetValue(kKeepDeviceOpen, keep_device_open));
    EXPECT_TRUE(json_store_->SetValue(kWpDisableSkipped, wp_disable_skipped));
    return base::MakeRefCounted<WriteProtectDisableCompleteStateHandler>(
        json_store_);
  }
};

TEST_F(WriteProtectDisableCompleteStateHandlerTest,
       InitializeState_KeepDeviceOpen_WpDisableNotSkipped) {
  auto handler = CreateStateHandler(true, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  EXPECT_EQ(handler->GetState().wp_disable_complete().action(),
            WriteProtectDisableCompleteState::
                RMAD_WP_DISABLE_COMPLETE_KEEP_DEVICE_OPEN);
}

TEST_F(WriteProtectDisableCompleteStateHandlerTest,
       InitializeState_CanCloseDevice_WpDisableSkipped) {
  auto handler = CreateStateHandler(false, true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  EXPECT_EQ(handler->GetState().wp_disable_complete().action(),
            WriteProtectDisableCompleteState::
                RMAD_WP_DISABLE_SKIPPED_ASSEMBLE_DEVICE);
}

TEST_F(WriteProtectDisableCompleteStateHandlerTest,
       InitializeState_CanCloseDevice_WpDisableNotSkipped) {
  auto handler = CreateStateHandler(false, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  EXPECT_EQ(handler->GetState().wp_disable_complete().action(),
            WriteProtectDisableCompleteState::
                RMAD_WP_DISABLE_COMPLETE_ASSEMBLE_DEVICE);
}

TEST_F(WriteProtectDisableCompleteStateHandlerTest, GetNextStateCase_Success) {
  auto handler = CreateStateHandler(false, true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  RmadState state;
  state.set_allocated_wp_disable_complete(new WriteProtectDisableCompleteState);

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateRoFirmware);
}

TEST_F(WriteProtectDisableCompleteStateHandlerTest,
       GetNextStateCase_MissingState) {
  auto handler = CreateStateHandler(false, true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // No WriteProtectDisableCompleteState.
  RmadState state;

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kWpDisableComplete);
}

}  // namespace rmad
