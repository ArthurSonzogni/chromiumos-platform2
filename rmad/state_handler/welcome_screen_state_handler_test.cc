// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>

#include <base/memory/scoped_refptr.h>
#include <base/run_loop.h>
#include <base/test/task_environment.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rmad/proto_bindings/rmad.pb.h"
#include "rmad/state_handler/state_handler_test_common.h"
#include "rmad/state_handler/welcome_screen_state_handler.h"
#include "rmad/utils/fake_hardware_verifier_utils.h"
#include "rmad/utils/json_store.h"

using testing::_;
using testing::Invoke;
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

  scoped_refptr<WelcomeScreenStateHandler> CreateStateHandler() {
    // Expect signal is always sent.
    EXPECT_CALL(signal_sender_, SendHardwareVerificationResultSignal(_))
        .WillOnce(Invoke([](const HardwareVerificationResult& result) {
          EXPECT_FALSE(result.is_compliant());
          EXPECT_EQ(result.error_str(), "fake_error_string");
          return true;
        }));

    // Fake |HardwareVerifierUtils|.
    auto handler = base::MakeRefCounted<WelcomeScreenStateHandler>(
        json_store_, std::make_unique<FakeHardwareVerifierUtils>());
    auto callback = std::make_unique<
        base::RepeatingCallback<bool(const HardwareVerificationResult&)>>(
        base::BindRepeating(&SignalSender::SendHardwareVerificationResultSignal,
                            base::Unretained(&signal_sender_)));
    handler->RegisterSignalSender(std::move(callback));
    return handler;
  }

  void RunHandlerTaskRunner(scoped_refptr<WelcomeScreenStateHandler> handler) {
    handler->GetTaskRunner()->PostTask(FROM_HERE, run_loop_.QuitClosure());
    run_loop_.Run();
  }

 protected:
  StrictMock<SignalSender> signal_sender_;

  // Variables for TaskRunner.
  base::test::TaskEnvironment task_environment_;
  base::RunLoop run_loop_;
};

TEST_F(WelcomeScreenStateHandlerTest, InitializeState_Success) {
  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  RunHandlerTaskRunner(handler);
}

TEST_F(WelcomeScreenStateHandlerTest, GetNextStateCase_Success) {
  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto welcome = std::make_unique<WelcomeState>();
  welcome->set_choice(WelcomeState::RMAD_CHOICE_FINALIZE_REPAIR);
  RmadState state;
  state.set_allocated_welcome(welcome.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kComponentsRepair);

  RunHandlerTaskRunner(handler);
}

TEST_F(WelcomeScreenStateHandlerTest, GetNextStateCase_MissingState) {
  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // No WelcomeScreenState.
  RmadState state;

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kWelcome);

  RunHandlerTaskRunner(handler);
}

TEST_F(WelcomeScreenStateHandlerTest, GetNextStateCase_MissingArgs) {
  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto welcome = std::make_unique<WelcomeState>();
  welcome->set_choice(WelcomeState::RMAD_CHOICE_UNKNOWN);
  RmadState state;
  state.set_allocated_welcome(welcome.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_MISSING);
  EXPECT_EQ(state_case, RmadState::StateCase::kWelcome);

  RunHandlerTaskRunner(handler);
}

}  // namespace rmad
