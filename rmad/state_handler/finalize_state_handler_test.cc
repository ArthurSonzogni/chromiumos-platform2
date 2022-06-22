// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_util.h>
#include <base/memory/scoped_refptr.h>
#include <base/run_loop.h>
#include <base/test/task_environment.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rmad/proto_bindings/rmad.pb.h"
#include "rmad/state_handler/finalize_state_handler.h"
#include "rmad/state_handler/state_handler_test_common.h"
#include "rmad/utils/json_store.h"
#include "rmad/utils/mock_cr50_utils.h"
#include "rmad/utils/mock_crossystem_utils.h"
#include "rmad/utils/mock_flashrom_utils.h"

using testing::_;
using testing::DoAll;
using testing::Eq;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::SetArgPointee;
using testing::StrictMock;

namespace rmad {

class FinalizeStateHandlerTest : public StateHandlerTest {
 public:
  // Helper class to mock the callback function to send signal.
  class SignalSender {
   public:
    MOCK_METHOD(void,
                SendFinalizeProgressSignal,
                (const FinalizeStatus&),
                (const));
  };

  scoped_refptr<FinalizeStateHandler> CreateStateHandler(
      const std::vector<int>& wp_status_list,
      bool enable_swwp_success,
      bool disable_factory_mode_success) {
    // Mock |Cr50Utils|.
    auto mock_cr50_utils = std::make_unique<NiceMock<MockCr50Utils>>();
    ON_CALL(*mock_cr50_utils, DisableFactoryMode())
        .WillByDefault(Return(disable_factory_mode_success));

    // Mock |CrosSystemUtils|.
    auto mock_crossystem_utils =
        std::make_unique<StrictMock<MockCrosSystemUtils>>();
    {
      InSequence seq;
      for (int i = 0; i < wp_status_list.size(); ++i) {
        EXPECT_CALL(*mock_crossystem_utils,
                    GetInt(Eq(CrosSystemUtils::kHwwpStatusProperty), _))
            .WillOnce(DoAll(SetArgPointee<1>(wp_status_list[i]), Return(true)));
      }
    }

    // Mock |FlashromUtils|.
    auto mock_flashrom_utils = std::make_unique<NiceMock<MockFlashromUtils>>();
    ON_CALL(*mock_flashrom_utils, EnableSoftwareWriteProtection())
        .WillByDefault(Return(enable_swwp_success));

    // Register signal callback.
    daemon_callback_->SetFinalizeSignalCallback(
        base::BindRepeating(&SignalSender::SendFinalizeProgressSignal,
                            base::Unretained(&signal_sender_)));

    return base::MakeRefCounted<FinalizeStateHandler>(
        json_store_, daemon_callback_, std::move(mock_cr50_utils),
        std::move(mock_crossystem_utils), std::move(mock_flashrom_utils));
  }

 protected:
  StrictMock<SignalSender> signal_sender_;

  // Variables for TaskRunner.
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  base::RunLoop run_loop_;
};

TEST_F(FinalizeStateHandlerTest, InitializeState_HwwpDisabled_Success) {
  auto handler = CreateStateHandler({0, 1}, true, true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  EXPECT_CALL(signal_sender_, SendFinalizeProgressSignal(_))
      .WillOnce(Invoke([](const FinalizeStatus& status) {
        EXPECT_EQ(status.status(),
                  FinalizeStatus::RMAD_FINALIZE_STATUS_COMPLETE);
        EXPECT_EQ(status.progress(), 1);
        EXPECT_EQ(status.error(), FinalizeStatus::RMAD_FINALIZE_ERROR_UNKNOWN);
      }));
  handler->RunState();
  task_environment_.FastForwardBy(FinalizeStateHandler::kReportStatusInterval);
}

TEST_F(FinalizeStateHandlerTest, InitializeState_HwwpEnabled_Success) {
  auto handler = CreateStateHandler({1, 1}, false, true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  EXPECT_CALL(signal_sender_, SendFinalizeProgressSignal(_))
      .WillOnce(Invoke([](const FinalizeStatus& status) {
        EXPECT_EQ(status.status(),
                  FinalizeStatus::RMAD_FINALIZE_STATUS_COMPLETE);
        EXPECT_EQ(status.progress(), 1);
        EXPECT_EQ(status.error(), FinalizeStatus::RMAD_FINALIZE_ERROR_UNKNOWN);
      }));
  handler->RunState();
  task_environment_.FastForwardBy(FinalizeStateHandler::kReportStatusInterval);
}

TEST_F(FinalizeStateHandlerTest, InitializeState_EnableSwwpFailed) {
  auto handler = CreateStateHandler({0}, false, true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  EXPECT_CALL(signal_sender_, SendFinalizeProgressSignal(_))
      .WillOnce(Invoke([](const FinalizeStatus& status) {
        EXPECT_EQ(status.status(),
                  FinalizeStatus::RMAD_FINALIZE_STATUS_FAILED_BLOCKING);
        EXPECT_EQ(status.progress(), 0);
        EXPECT_EQ(status.error(),
                  FinalizeStatus::RMAD_FINALIZE_ERROR_CANNOT_ENABLE_SWWP);
      }));
  handler->RunState();
  task_environment_.FastForwardBy(FinalizeStateHandler::kReportStatusInterval);
}

TEST_F(FinalizeStateHandlerTest, InitializeState_DisableFactoryModeFailed) {
  auto handler = CreateStateHandler({0}, true, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  EXPECT_CALL(signal_sender_, SendFinalizeProgressSignal(_))
      .WillOnce(Invoke([](const FinalizeStatus& status) {
        EXPECT_EQ(status.status(),
                  FinalizeStatus::RMAD_FINALIZE_STATUS_FAILED_BLOCKING);
        EXPECT_EQ(status.progress(), 0.5);
        EXPECT_EQ(status.error(),
                  FinalizeStatus::RMAD_FINALIZE_ERROR_CANNOT_ENABLE_HWWP);
      }));
  handler->RunState();
  task_environment_.FastForwardBy(FinalizeStateHandler::kReportStatusInterval);
}

TEST_F(FinalizeStateHandlerTest, InitializeState_HwwpDisabled) {
  auto handler = CreateStateHandler({0, 0}, true, true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  EXPECT_CALL(signal_sender_, SendFinalizeProgressSignal(_))
      .WillOnce(Invoke([](const FinalizeStatus& status) {
        EXPECT_EQ(status.status(),
                  FinalizeStatus::RMAD_FINALIZE_STATUS_FAILED_BLOCKING);
        EXPECT_EQ(status.progress(), 0.8);
        EXPECT_EQ(status.error(),
                  FinalizeStatus::RMAD_FINALIZE_ERROR_CANNOT_ENABLE_HWWP);
      }));
  handler->RunState();
  task_environment_.FastForwardBy(FinalizeStateHandler::kReportStatusInterval);
}

TEST_F(FinalizeStateHandlerTest, GetNextStateCase_Success) {
  auto handler = CreateStateHandler({0, 1}, true, true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  handler->RunState();
  task_environment_.RunUntilIdle();

  RmadState state;
  state.mutable_finalize()->set_choice(
      FinalizeState::RMAD_FINALIZE_CHOICE_CONTINUE);

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kRepairComplete);
}

TEST_F(FinalizeStateHandlerTest, GetNextStateCase_InProgress) {
  auto handler = CreateStateHandler({0, 1}, true, true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  handler->RunState();

  RmadState state;
  state.mutable_finalize()->set_choice(
      FinalizeState::RMAD_FINALIZE_CHOICE_CONTINUE);

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_WAIT);
  EXPECT_EQ(state_case, RmadState::StateCase::kFinalize);

  task_environment_.RunUntilIdle();
}

TEST_F(FinalizeStateHandlerTest, GetNextStateCase_MissingState) {
  auto handler = CreateStateHandler({0, 1}, true, true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  handler->RunState();
  task_environment_.RunUntilIdle();

  RmadState state;

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kFinalize);
}

TEST_F(FinalizeStateHandlerTest, GetNextStateCase_MissingArgs) {
  auto handler = CreateStateHandler({0, 1}, true, true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  handler->RunState();
  task_environment_.RunUntilIdle();

  RmadState state;
  state.mutable_finalize()->set_choice(
      FinalizeState::RMAD_FINALIZE_CHOICE_UNKNOWN);

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_MISSING);
  EXPECT_EQ(state_case, RmadState::StateCase::kFinalize);
}

TEST_F(FinalizeStateHandlerTest, GetNextStateCase_BlockingFailure_Retry) {
  auto handler = CreateStateHandler({0, 0}, true, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  handler->RunState();
  task_environment_.RunUntilIdle();

  // Get blocking failure.
  {
    RmadState state;
    state.mutable_finalize()->set_choice(
        FinalizeState::RMAD_FINALIZE_CHOICE_CONTINUE);

    auto [error, state_case] = handler->GetNextStateCase(state);
    EXPECT_EQ(error, RMAD_ERROR_FINALIZATION_FAILED);
    EXPECT_EQ(state_case, RmadState::StateCase::kFinalize);
  }

  // Request a retry.
  {
    RmadState state;
    state.mutable_finalize()->set_choice(
        FinalizeState::RMAD_FINALIZE_CHOICE_RETRY);

    auto [error, state_case] = handler->GetNextStateCase(state);
    EXPECT_EQ(error, RMAD_ERROR_WAIT);
    EXPECT_EQ(state_case, RmadState::StateCase::kFinalize);
  }

  task_environment_.RunUntilIdle();

  // Still fails.
  {
    RmadState state;
    state.mutable_finalize()->set_choice(
        FinalizeState::RMAD_FINALIZE_CHOICE_CONTINUE);

    auto [error, state_case] = handler->GetNextStateCase(state);
    EXPECT_EQ(error, RMAD_ERROR_FINALIZATION_FAILED);
    EXPECT_EQ(state_case, RmadState::StateCase::kFinalize);
  }
}

}  // namespace rmad
