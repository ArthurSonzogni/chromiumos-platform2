// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>

#include <base/files/file_util.h>
#include <base/memory/scoped_refptr.h>
#include <base/test/task_environment.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rmad/proto_bindings/rmad.pb.h"
#include "rmad/state_handler/state_handler_test_common.h"
#include "rmad/state_handler/welcome_screen_state_handler.h"
#include "rmad/system/mock_hardware_verifier_client.h"
#include "rmad/utils/json_store.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::StrictMock;

namespace rmad {

class WelcomeScreenStateHandlerTest : public StateHandlerTest {
 public:
  // Helper class to mock the callback function to send signal.
  class SignalSender {
   public:
    MOCK_METHOD(bool,
                SendHardwareVerificationResultSignal,
                (const HardwareVerificationResult&),
                (const));
  };

  scoped_refptr<WelcomeScreenStateHandler> CreateStateHandler(
      int hw_verification_result) {
    // Mock |HardwareVerifierClient|.
    auto mock_hardware_verifier_client =
        std::make_unique<NiceMock<MockHardwareVerifierClient>>();
    ON_CALL(*mock_hardware_verifier_client, GetHardwareVerificationResult(_))
        .WillByDefault(
            [hw_verification_result](HardwareVerificationResult* result) {
              if (hw_verification_result == 0) {
                result->set_is_compliant(false);
                result->set_error_str("mock_hardware_verifier_error_string");
                return true;
              } else if (hw_verification_result == 1) {
                result->set_is_compliant(true);
                result->set_error_str("mock_hardware_verifier_error_string");
                return true;
              } else {
                return false;
              }
            });
    auto handler = base::MakeRefCounted<WelcomeScreenStateHandler>(
        json_store_, std::move(mock_hardware_verifier_client));
    auto callback = std::make_unique<
        base::RepeatingCallback<bool(const HardwareVerificationResult&)>>(
        base::BindRepeating(&SignalSender::SendHardwareVerificationResultSignal,
                            base::Unretained(&signal_sender_)));
    handler->RegisterSignalSender(std::move(callback));
    return handler;
  }

 protected:
  StrictMock<SignalSender> signal_sender_;

  // Variables for TaskRunner.
  base::test::TaskEnvironment task_environment_;
};

TEST_F(WelcomeScreenStateHandlerTest,
       InitializeState_Success_VerificationPass_DoGetStateTask) {
  auto handler = CreateStateHandler(1);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  EXPECT_CALL(signal_sender_, SendHardwareVerificationResultSignal(_))
      .WillOnce(Invoke([](const HardwareVerificationResult& result) {
        EXPECT_EQ(result.is_compliant(), true);
        EXPECT_EQ(result.error_str(), "mock_hardware_verifier_error_string");
        return true;
      }));
  RmadState state = handler->GetState(true);
  task_environment_.RunUntilIdle();
}

TEST_F(WelcomeScreenStateHandlerTest,
       InitializeState_Success_VerificationPass_NoGetStateTask) {
  auto handler = CreateStateHandler(1);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  RmadState state = handler->GetState();
}

TEST_F(WelcomeScreenStateHandlerTest,
       InitializeState_Success_VerificationFail) {
  auto handler = CreateStateHandler(0);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
}

TEST_F(WelcomeScreenStateHandlerTest,
       InitializeState_Success_VerificationCallFail) {
  auto handler = CreateStateHandler(-1);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  task_environment_.RunUntilIdle();
}

TEST_F(WelcomeScreenStateHandlerTest, GetNextStateCase_Success) {
  auto handler = CreateStateHandler(-1);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto welcome = std::make_unique<WelcomeState>();
  welcome->set_choice(WelcomeState::RMAD_CHOICE_FINALIZE_REPAIR);
  RmadState state;
  state.set_allocated_welcome(welcome.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kComponentsRepair);

  task_environment_.RunUntilIdle();
}

TEST_F(WelcomeScreenStateHandlerTest, GetNextStateCase_MissingState) {
  auto handler = CreateStateHandler(-1);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // No WelcomeScreenState.
  RmadState state;

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kWelcome);

  task_environment_.RunUntilIdle();
}

TEST_F(WelcomeScreenStateHandlerTest, GetNextStateCase_MissingArgs) {
  auto handler = CreateStateHandler(-1);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto welcome = std::make_unique<WelcomeState>();
  welcome->set_choice(WelcomeState::RMAD_CHOICE_UNKNOWN);
  RmadState state;
  state.set_allocated_welcome(welcome.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_MISSING);
  EXPECT_EQ(state_case, RmadState::StateCase::kWelcome);

  task_environment_.RunUntilIdle();
}

}  // namespace rmad
