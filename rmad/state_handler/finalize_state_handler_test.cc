// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>

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

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::StrictMock;

namespace rmad {

class FinalizeStateHandlerTest : public StateHandlerTest {
 public:
  // Helper class to mock the callback function to send signal.
  class SignalSender {
   public:
    MOCK_METHOD(bool,
                SendFinalizeProgressSignal,
                (const FinalizeStatus&),
                (const));
  };

  scoped_refptr<FinalizeStateHandler> CreateStateHandler(
      bool disable_factory_mode_success) {
    // Mock |Cr50Utils|.
    auto mock_cr50_utils = std::make_unique<NiceMock<MockCr50Utils>>();
    ON_CALL(*mock_cr50_utils, DisableFactoryMode())
        .WillByDefault(Return(disable_factory_mode_success));
    auto handler = base::MakeRefCounted<FinalizeStateHandler>(
        json_store_, std::move(mock_cr50_utils));
    auto callback =
        std::make_unique<base::RepeatingCallback<bool(const FinalizeStatus&)>>(
            base::BindRepeating(&SignalSender::SendFinalizeProgressSignal,
                                base::Unretained(&signal_sender_)));
    handler->RegisterSignalSender(std::move(callback));
    return handler;
  }

 protected:
  StrictMock<SignalSender> signal_sender_;

  // Variables for TaskRunner.
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  base::RunLoop run_loop_;
};

TEST_F(FinalizeStateHandlerTest, InitializeState_Success) {
  auto handler = CreateStateHandler(true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  EXPECT_CALL(signal_sender_, SendFinalizeProgressSignal(_))
      .WillOnce(Invoke([](const FinalizeStatus& status) {
        EXPECT_EQ(status.status(),
                  FinalizeStatus::RMAD_FINALIZE_STATUS_COMPLETE);
        return true;
      }));
  task_environment_.FastForwardBy(FinalizeStateHandler::kReportStatusInterval);
}

TEST_F(FinalizeStateHandlerTest, InitializeState_DisableFactoryModeFailed) {
  auto handler = CreateStateHandler(false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  EXPECT_CALL(signal_sender_, SendFinalizeProgressSignal(_))
      .WillOnce(Invoke([](const FinalizeStatus& status) {
        EXPECT_EQ(status.status(),
                  FinalizeStatus::RMAD_FINALIZE_STATUS_FAILED_BLOCKING);
        return true;
      }));
  task_environment_.FastForwardBy(FinalizeStateHandler::kReportStatusInterval);
}

TEST_F(FinalizeStateHandlerTest, GetNextStateCase_Success) {
  auto handler = CreateStateHandler(true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  task_environment_.RunUntilIdle();

  auto finalize = std::make_unique<FinalizeState>();
  finalize->set_choice(FinalizeState::RMAD_FINALIZE_CHOICE_CONTINUE);
  RmadState state;
  state.set_allocated_finalize(finalize.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kRepairComplete);
}

TEST_F(FinalizeStateHandlerTest, GetNextStateCase_InProgress) {
  auto handler = CreateStateHandler(true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto finalize = std::make_unique<FinalizeState>();
  finalize->set_choice(FinalizeState::RMAD_FINALIZE_CHOICE_CONTINUE);
  RmadState state;
  state.set_allocated_finalize(finalize.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_WAIT);
  EXPECT_EQ(state_case, RmadState::StateCase::kFinalize);

  task_environment_.RunUntilIdle();
}

TEST_F(FinalizeStateHandlerTest, GetNextStateCase_MissingState) {
  auto handler = CreateStateHandler(true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  task_environment_.RunUntilIdle();

  RmadState state;

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kFinalize);
}

TEST_F(FinalizeStateHandlerTest, GetNextStateCase_MissingArgs) {
  auto handler = CreateStateHandler(true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  task_environment_.RunUntilIdle();

  auto finalize = std::make_unique<FinalizeState>();
  finalize->set_choice(FinalizeState::RMAD_FINALIZE_CHOICE_UNKNOWN);
  RmadState state;
  state.set_allocated_finalize(finalize.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_MISSING);
  EXPECT_EQ(state_case, RmadState::StateCase::kFinalize);
}

TEST_F(FinalizeStateHandlerTest, GetNextStateCase_BlockingFailure_Retry) {
  auto handler = CreateStateHandler(false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  task_environment_.RunUntilIdle();

  // Get blocking failure.
  {
    auto finalize = std::make_unique<FinalizeState>();
    finalize->set_choice(FinalizeState::RMAD_FINALIZE_CHOICE_CONTINUE);
    RmadState state;
    state.set_allocated_finalize(finalize.release());

    auto [error, state_case] = handler->GetNextStateCase(state);
    EXPECT_EQ(error, RMAD_ERROR_FINALIZATION_FAILED);
    EXPECT_EQ(state_case, RmadState::StateCase::kFinalize);
  }

  // Request a retry.
  {
    auto finalize = std::make_unique<FinalizeState>();
    finalize->set_choice(FinalizeState::RMAD_FINALIZE_CHOICE_RETRY);
    RmadState state;
    state.set_allocated_finalize(finalize.release());

    auto [error, state_case] = handler->GetNextStateCase(state);
    EXPECT_EQ(error, RMAD_ERROR_WAIT);
    EXPECT_EQ(state_case, RmadState::StateCase::kFinalize);
  }

  // In progress.
  {
    auto finalize = std::make_unique<FinalizeState>();
    finalize->set_choice(FinalizeState::RMAD_FINALIZE_CHOICE_CONTINUE);
    RmadState state;
    state.set_allocated_finalize(finalize.release());

    auto [error, state_case] = handler->GetNextStateCase(state);
    EXPECT_EQ(error, RMAD_ERROR_WAIT);
    EXPECT_EQ(state_case, RmadState::StateCase::kFinalize);
  }

  task_environment_.RunUntilIdle();

  // Still fails.
  {
    auto finalize = std::make_unique<FinalizeState>();
    finalize->set_choice(FinalizeState::RMAD_FINALIZE_CHOICE_CONTINUE);
    RmadState state;
    state.set_allocated_finalize(finalize.release());

    auto [error, state_case] = handler->GetNextStateCase(state);
    EXPECT_EQ(error, RMAD_ERROR_FINALIZATION_FAILED);
    EXPECT_EQ(state_case, RmadState::StateCase::kFinalize);
  }
}

}  // namespace rmad
