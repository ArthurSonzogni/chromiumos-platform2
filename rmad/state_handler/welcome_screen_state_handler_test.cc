// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_util.h>
#include <base/memory/scoped_refptr.h>
#include <base/test/task_environment.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rmad/logs/logs_constants.h"
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

constexpr char kUnqualifiedDramErrorStr[] = "Unqualified dram: dram_1234";
constexpr char kUnqualifiedBatteryErrorStr[] =
    "Unqualified battery: battery_5678";
constexpr char kVerificationFailedErrorStr[] =
    "Unqualified dram: dram_1234\nUnqualified battery: battery_5678";
constexpr char kVerificationFailedErrorLogStr[] =
    "Unqualified dram: dram_1234, Unqualified battery: battery_5678";

namespace rmad {

class WelcomeScreenStateHandlerTest : public StateHandlerTest {
 public:
  // Helper class to mock the callback function to send signal.
  class SignalSender {
   public:
    MOCK_METHOD(void,
                SendHardwareVerificationSignal,
                (const HardwareVerificationResult&),
                (const));
  };

  scoped_refptr<WelcomeScreenStateHandler> CreateStateHandler(
      bool hw_verification_request_success, bool hw_verification_result) {
    // Mock |HardwareVerifierClient|.
    auto mock_hardware_verifier_client =
        std::make_unique<NiceMock<MockHardwareVerifierClient>>();
    ON_CALL(*mock_hardware_verifier_client, GetHardwareVerificationResult(_, _))
        .WillByDefault(
            [hw_verification_request_success, hw_verification_result](
                bool* is_compliant, std::vector<std::string>* error_strings) {
              if (hw_verification_request_success) {
                *is_compliant = hw_verification_result;
                if (!hw_verification_result) {
                  *error_strings = {kUnqualifiedDramErrorStr,
                                    kUnqualifiedBatteryErrorStr};
                }
              }
              return hw_verification_request_success;
            });

    // Register signal callback.
    daemon_callback_->SetHardwareVerificationSignalCallback(
        base::BindRepeating(&SignalSender::SendHardwareVerificationSignal,
                            base::Unretained(&signal_sender_)));

    return base::MakeRefCounted<WelcomeScreenStateHandler>(
        json_store_, daemon_callback_,
        std::move(mock_hardware_verifier_client));
  }

 protected:
  StrictMock<SignalSender> signal_sender_;

  // Variables for TaskRunner.
  base::test::TaskEnvironment task_environment_;
};

TEST_F(WelcomeScreenStateHandlerTest,
       InitializeState_Success_VerificationPass_DoGetStateTask) {
  auto handler = CreateStateHandler(true, true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  EXPECT_CALL(signal_sender_, SendHardwareVerificationSignal(_))
      .WillOnce(Invoke([](const HardwareVerificationResult& result) {
        EXPECT_EQ(result.is_compliant(), true);
        EXPECT_EQ(result.error_str(), "");
      }));
  RmadState state = handler->GetState(true);
  task_environment_.RunUntilIdle();

  // Verify the hardware verification result is recorded to logs.
  base::Value logs(base::Value::Type::DICT);
  json_store_->GetValue(kLogs, &logs);

  const base::Value::List* events = logs.GetDict().FindList(kEvents);
  CHECK_EQ(events->size(), 1);

  const base::Value::Dict* verification_result =
      (*events)[0].GetDict().FindDict(kDetails);
  EXPECT_TRUE(verification_result->FindBool(kLogIsCompliant).has_value());
  EXPECT_TRUE(verification_result->FindBool(kLogIsCompliant).value());
  EXPECT_EQ("", *verification_result->FindString(kLogUnqualifiedComponents));
}

TEST_F(WelcomeScreenStateHandlerTest,
       InitializeState_Success_VerificationPass_NoGetStateTask) {
  auto handler = CreateStateHandler(true, true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  RmadState state = handler->GetState();
}

TEST_F(WelcomeScreenStateHandlerTest,
       InitializeState_Success_VerificationFail) {
  auto handler = CreateStateHandler(true, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  EXPECT_CALL(signal_sender_, SendHardwareVerificationSignal(_))
      .WillOnce(Invoke([](const HardwareVerificationResult& result) {
        EXPECT_EQ(result.is_compliant(), false);
        EXPECT_EQ(result.error_str(), kVerificationFailedErrorStr);
      }));
  RmadState state = handler->GetState(true);
  task_environment_.RunUntilIdle();

  // Verify the hardware verification result is recorded to logs.
  base::Value logs(base::Value::Type::DICT);
  json_store_->GetValue(kLogs, &logs);

  const base::Value::List* events = logs.GetDict().FindList(kEvents);
  CHECK_EQ(events->size(), 1);

  const base::Value::Dict* verification_result =
      (*events)[0].GetDict().FindDict(kDetails);
  EXPECT_TRUE(verification_result->FindBool(kLogIsCompliant).has_value());
  EXPECT_FALSE(verification_result->FindBool(kLogIsCompliant).value());
  EXPECT_EQ(kVerificationFailedErrorLogStr,
            *verification_result->FindString(kLogUnqualifiedComponents));
}

TEST_F(WelcomeScreenStateHandlerTest,
       InitializeState_Success_VerificationCallFail) {
  auto handler = CreateStateHandler(false, true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  task_environment_.RunUntilIdle();
}

TEST_F(WelcomeScreenStateHandlerTest, GetNextStateCase_Success) {
  auto handler = CreateStateHandler(true, true);
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
  auto handler = CreateStateHandler(true, true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // No WelcomeScreenState.
  RmadState state;

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kWelcome);

  task_environment_.RunUntilIdle();
}

TEST_F(WelcomeScreenStateHandlerTest, GetNextStateCase_MissingArgs) {
  auto handler = CreateStateHandler(true, true);
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
