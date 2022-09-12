// Copyright 2021 The ChromiumOS Authors
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
#include "rmad/utils/mock_flashrom_utils.h"

using testing::NiceMock;
using testing::Return;

namespace rmad {

class WriteProtectDisableCompleteStateHandlerTest : public StateHandlerTest {
 public:
  scoped_refptr<WriteProtectDisableCompleteStateHandler> CreateStateHandler(
      WpDisableMethod wp_disable_method, bool disable_swwp_success) {
    // Mock |FlashromUtils|.
    auto mock_flashrom_utils = std::make_unique<NiceMock<MockFlashromUtils>>();
    ON_CALL(*mock_flashrom_utils, DisableSoftwareWriteProtection())
        .WillByDefault(Return(disable_swwp_success));

    EXPECT_TRUE(json_store_->SetValue(kWpDisableMethod,
                                      WpDisableMethod_Name(wp_disable_method)));
    return base::MakeRefCounted<WriteProtectDisableCompleteStateHandler>(
        json_store_, daemon_callback_, std::move(mock_flashrom_utils));
  }
};

TEST_F(WriteProtectDisableCompleteStateHandlerTest, InitializeState_Skipped) {
  auto handler = CreateStateHandler(RMAD_WP_DISABLE_METHOD_SKIPPED, true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  EXPECT_EQ(handler->GetState().wp_disable_complete().action(),
            WriteProtectDisableCompleteState::RMAD_WP_DISABLE_COMPLETE_NO_OP);
}

TEST_F(WriteProtectDisableCompleteStateHandlerTest, InitializeState_Rsu) {
  auto handler = CreateStateHandler(RMAD_WP_DISABLE_METHOD_RSU, true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  EXPECT_EQ(handler->GetState().wp_disable_complete().action(),
            WriteProtectDisableCompleteState::RMAD_WP_DISABLE_COMPLETE_NO_OP);
}

TEST_F(WriteProtectDisableCompleteStateHandlerTest,
       InitializeState_PhysicalAssembleDevice) {
  auto handler =
      CreateStateHandler(RMAD_WP_DISABLE_METHOD_PHYSICAL_ASSEMBLE_DEVICE, true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  EXPECT_EQ(handler->GetState().wp_disable_complete().action(),
            WriteProtectDisableCompleteState::
                RMAD_WP_DISABLE_COMPLETE_ASSEMBLE_DEVICE);
}

TEST_F(WriteProtectDisableCompleteStateHandlerTest,
       InitializeState_PhysicalKeepDeviceOpen) {
  auto handler = CreateStateHandler(
      RMAD_WP_DISABLE_METHOD_PHYSICAL_KEEP_DEVICE_OPEN, true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  EXPECT_EQ(handler->GetState().wp_disable_complete().action(),
            WriteProtectDisableCompleteState::
                RMAD_WP_DISABLE_COMPLETE_KEEP_DEVICE_OPEN);
}

TEST_F(WriteProtectDisableCompleteStateHandlerTest, GetNextStateCase_Success) {
  auto handler = CreateStateHandler(RMAD_WP_DISABLE_METHOD_RSU, true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  RmadState state;
  state.set_allocated_wp_disable_complete(new WriteProtectDisableCompleteState);

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateRoFirmware);
}

TEST_F(WriteProtectDisableCompleteStateHandlerTest,
       GetNextStateCase_DisableSwwpFailed) {
  auto handler = CreateStateHandler(RMAD_WP_DISABLE_METHOD_RSU, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  RmadState state;
  state.set_allocated_wp_disable_complete(new WriteProtectDisableCompleteState);

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_WP_ENABLED);
  EXPECT_EQ(state_case, RmadState::StateCase::kWpDisableComplete);
}

TEST_F(WriteProtectDisableCompleteStateHandlerTest,
       GetNextStateCase_MissingState) {
  auto handler = CreateStateHandler(RMAD_WP_DISABLE_METHOD_RSU, true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // No WriteProtectDisableCompleteState.
  RmadState state;

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kWpDisableComplete);
}

}  // namespace rmad
