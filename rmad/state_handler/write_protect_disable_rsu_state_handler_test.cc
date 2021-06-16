// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/memory/scoped_refptr.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rmad/state_handler/state_handler_test_common.h"
#include "rmad/state_handler/write_protect_disable_rsu_state_handler.h"
#include "rmad/utils/mock_cr50_utils.h"

using testing::_;
using testing::DoAll;
using testing::Eq;
using testing::Ne;
using testing::NiceMock;
using testing::Return;
using testing::SetArgPointee;

namespace rmad {

namespace {

constexpr char kTestChallengeCode[] = "ABCDEFGH";
constexpr char kWrongChallengeCode[] = "AAA";
constexpr char kTestUnlockCode[] = "abcdefgh";
constexpr char kWrongUnlockCode[] = "aaa";

}  // namespace

class WriteProtectDisableRsuStateHandlerTest : public StateHandlerTest {
 public:
  scoped_refptr<WriteProtectDisableRsuStateHandler> CreateStateHandler() {
    // Mock |Cr50Utils|.
    auto mock_cr50_utils = std::make_unique<NiceMock<MockCr50Utils>>();
    ON_CALL(*mock_cr50_utils, GetRsuChallengeCode(_))
        .WillByDefault(
            DoAll(SetArgPointee<0>(kTestChallengeCode), Return(true)));
    ON_CALL(*mock_cr50_utils, PerformRsu(Eq(kTestUnlockCode)))
        .WillByDefault(Return(true));
    ON_CALL(*mock_cr50_utils, PerformRsu(Ne(kTestUnlockCode)))
        .WillByDefault(Return(false));

    return base::MakeRefCounted<WriteProtectDisableRsuStateHandler>(
        json_store_, std::move(mock_cr50_utils));
  }
};

TEST_F(WriteProtectDisableRsuStateHandlerTest, InitializeState_Success) {
  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
}

TEST_F(WriteProtectDisableRsuStateHandlerTest, GetNextStateCase_Success) {
  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto wp_disable_rsu = std::make_unique<WriteProtectDisableRsuState>();
  wp_disable_rsu->set_challenge_code(kTestChallengeCode);
  wp_disable_rsu->set_unlock_code(kTestUnlockCode);
  RmadState state;
  state.set_allocated_wp_disable_rsu(wp_disable_rsu.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kWpDisableComplete);
}

TEST_F(WriteProtectDisableRsuStateHandlerTest, GetNextStateCase_MissingState) {
  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // No WriteProtectDisableRsuState.
  RmadState state;

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kWpDisableRsu);
}

TEST_F(WriteProtectDisableRsuStateHandlerTest,
       GetNextStateCase_WrongChallengeCode) {
  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto wp_disable_rsu = std::make_unique<WriteProtectDisableRsuState>();
  wp_disable_rsu->set_challenge_code(kWrongChallengeCode);
  RmadState state;
  state.set_allocated_wp_disable_rsu(wp_disable_rsu.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_VIOLATION);
  EXPECT_EQ(state_case, RmadState::StateCase::kWpDisableRsu);
}

TEST_F(WriteProtectDisableRsuStateHandlerTest,
       GetNextStateCase_WrongUnlockCode) {
  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto wp_disable_rsu = std::make_unique<WriteProtectDisableRsuState>();
  wp_disable_rsu->set_challenge_code(kTestChallengeCode);
  wp_disable_rsu->set_unlock_code(kWrongUnlockCode);
  RmadState state;
  state.set_allocated_wp_disable_rsu(wp_disable_rsu.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_WRITE_PROTECT_DISABLE_RSU_CODE_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kWpDisableRsu);
}

}  // namespace rmad
