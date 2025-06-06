// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/welcome_screen_state_handler.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_util.h>
#include <base/memory/scoped_refptr.h>
#include <base/test/task_environment.h>
#include <brillo/file_utils.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rmad/constants.h"
#include "rmad/logs/logs_constants.h"
#include "rmad/proto_bindings/rmad.pb.h"
#include "rmad/state_handler/state_handler_test_common.h"
#include "rmad/system/mock_hardware_verifier_client.h"
#include "rmad/utils/json_store.h"
#include "rmad/utils/mock_cros_config_utils.h"
#include "rmad/utils/mock_vpd_utils.h"
#include "rmad/utils/rmad_config_utils_impl.h"

using testing::_;
using testing::DoAll;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::SetArgPointee;
using testing::StrictMock;

namespace {

constexpr char kUnqualifiedDramErrorStr[] = "Unqualified dram: dram_1234";
constexpr char kUnqualifiedBatteryErrorStr[] =
    "Unqualified battery: battery_5678";
constexpr char kVerificationFailedErrorStr[] =
    "Unqualified dram: dram_1234\nUnqualified battery: battery_5678";
constexpr char kVerificationFailedErrorLogStr[] =
    "Unqualified dram: dram_1234, Unqualified battery: battery_5678";

}  // namespace

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

  struct StateHandlerArgs {
    bool hw_verification_request_success = true;
    bool hw_verification_result = true;
    uint64_t shimless_mode_flags = 0;
    std::string rmad_config_text = "";
  };

  scoped_refptr<WelcomeScreenStateHandler> CreateStateHandler(
      const StateHandlerArgs& args) {
    // Mock |HardwareVerifierClient|.
    auto mock_hardware_verifier_client =
        std::make_unique<NiceMock<MockHardwareVerifierClient>>();
    ON_CALL(*mock_hardware_verifier_client, GetHardwareVerificationResult(_, _))
        .WillByDefault([args](bool* is_compliant,
                              std::vector<std::string>* error_strings) {
          if (args.hw_verification_request_success) {
            *is_compliant = args.hw_verification_result;
            if (!args.hw_verification_result) {
              *error_strings = {kUnqualifiedDramErrorStr,
                                kUnqualifiedBatteryErrorStr};
            }
          }
          return args.hw_verification_request_success;
        });

    // Register signal callback.
    daemon_callback_->SetHardwareVerificationSignalCallback(
        base::BindRepeating(&SignalSender::SendHardwareVerificationSignal,
                            base::Unretained(&signal_sender_)));

    // Mock |VpdUtils|.
    auto mock_vpd_utils = std::make_unique<NiceMock<MockVpdUtils>>();
    ON_CALL(*mock_vpd_utils, GetShimlessMode(_))
        .WillByDefault(
            DoAll(SetArgPointee<0>(args.shimless_mode_flags), Return(true)));

    // Inject textproto content for |RmadConfigUtils|.
    auto mock_cros_config_utils =
        std::make_unique<StrictMock<MockCrosConfigUtils>>();
    if (!args.rmad_config_text.empty()) {
      EXPECT_CALL(*mock_cros_config_utils, GetModelName(_))
          .WillOnce(DoAll(SetArgPointee<0>("model_name"), Return(true)));

      const base::FilePath textproto_file_path =
          GetTempDirPath()
              .Append("model_name")
              .Append(kDefaultRmadConfigProtoFilePath);

      EXPECT_TRUE(base::CreateDirectory(textproto_file_path.DirName()));
      EXPECT_TRUE(base::WriteFile(textproto_file_path, args.rmad_config_text));
    } else {
      EXPECT_CALL(*mock_cros_config_utils, GetModelName(_))
          .WillOnce(Return(false));
    }
    auto rmad_config_utils = std::make_unique<RmadConfigUtilsImpl>(
        GetTempDirPath(), std::move(mock_cros_config_utils));

    // Initialization should always succeed.
    auto handler = base::MakeRefCounted<WelcomeScreenStateHandler>(
        json_store_, daemon_callback_, GetTempDirPath(),
        std::move(mock_hardware_verifier_client), std::move(mock_vpd_utils),
        std::move(rmad_config_utils));
    EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

    return handler;
  }

  void ExpectSignal(bool is_compliant,
                    const std::string& error_str,
                    bool is_skipped) {
    EXPECT_CALL(signal_sender_, SendHardwareVerificationSignal(_))
        .WillOnce(Invoke([is_compliant, error_str, is_skipped](
                             const HardwareVerificationResult& result) {
          EXPECT_EQ(result.is_compliant(), is_compliant);
          EXPECT_EQ(result.error_str(), error_str);
          EXPECT_EQ(result.is_skipped(), is_skipped);
        }));
    task_environment_.RunUntilIdle();
  }

 protected:
  StrictMock<SignalSender> signal_sender_;

  // Variables for TaskRunner.
  base::test::TaskEnvironment task_environment_;
};

TEST_F(WelcomeScreenStateHandlerTest,
       InitializeState_Succeeded_VerificationPass_DoGetStateTask) {
  auto handler = CreateStateHandler({});

  RmadState state = handler->GetState(true);
  ExpectSignal(true, "", false);

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
       InitializeState_Succeeded_VerificationBypass_DoGetStateTask) {
  // Bypass hardware verification check.
  ASSERT_TRUE(brillo::TouchFile(GetTempDirPath().Append(kDisableRaccFilePath)));

  auto handler = CreateStateHandler({.hw_verification_request_success = false});
  RmadState state = handler->GetState(true);
  ExpectSignal(false, "", true);
}

TEST_F(WelcomeScreenStateHandlerTest,
       InitializeState_Succeeded_VerificationBypassWithFlags_DoGetStateTask) {
  auto handler = CreateStateHandler(
      {.hw_verification_request_success = false,
       .shimless_mode_flags = kShimlessModeFlagsRaccResultBypass});
  RmadState state = handler->GetState(true);
  ExpectSignal(false, "", true);
}

TEST_F(
    WelcomeScreenStateHandlerTest,
    InitializeState_Succeeded_VerificationBypassWithRmadConfig_DoGetStateTask) {
  std::string textproto = R"(
      skip_hardware_verification: true
    )";
  auto handler = CreateStateHandler({.hw_verification_request_success = false,
                                     .rmad_config_text = textproto});
  RmadState state = handler->GetState(true);
  ExpectSignal(false, "", true);
}

TEST_F(WelcomeScreenStateHandlerTest,
       InitializeState_Succeeded_VerificationPass_NoGetStateTask) {
  auto handler = CreateStateHandler({});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  RmadState state = handler->GetState();
}

TEST_F(WelcomeScreenStateHandlerTest,
       InitializeState_Succeeded_VerificationFail) {
  auto handler = CreateStateHandler({.hw_verification_result = false});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  RmadState state = handler->GetState(true);
  ExpectSignal(false, kVerificationFailedErrorStr, false);

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
       InitializeState_Succeeded_VerificationCallFail) {
  auto handler = CreateStateHandler({.hw_verification_request_success = false});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
}

TEST_F(WelcomeScreenStateHandlerTest, GetNextStateCase_Succeeded) {
  auto handler = CreateStateHandler({});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  RmadState state;
  state.mutable_welcome()->set_choice(
      WelcomeState::RMAD_CHOICE_FINALIZE_REPAIR);

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kComponentsRepair);
}

TEST_F(WelcomeScreenStateHandlerTest, GetNextStateCase_SpareMlb_Succeeded) {
  auto handler = CreateStateHandler({});
  json_store_->SetValue(kSpareMlb, true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  RmadState state;
  state.mutable_welcome()->set_choice(
      WelcomeState::RMAD_CHOICE_FINALIZE_REPAIR);

  auto [error, state_case] = handler->GetNextStateCase(state);
  bool mlb_repair;
  json_store_->GetValue(kMlbRepair, &mlb_repair);

  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_TRUE(mlb_repair);
  EXPECT_EQ(state_case, RmadState::StateCase::kWpDisablePhysical);
}

TEST_F(WelcomeScreenStateHandlerTest, GetNextStateCase_MissingState) {
  auto handler = CreateStateHandler({});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // No WelcomeScreenState.
  RmadState state;

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kWelcome);
}

TEST_F(WelcomeScreenStateHandlerTest, GetNextStateCase_MissingArgs) {
  auto handler = CreateStateHandler({});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  RmadState state;
  state.mutable_welcome()->set_choice(WelcomeState::RMAD_CHOICE_UNKNOWN);

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_MISSING);
  EXPECT_EQ(state_case, RmadState::StateCase::kWelcome);
}

}  // namespace rmad
