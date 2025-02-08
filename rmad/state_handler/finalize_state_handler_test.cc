// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/finalize_state_handler.h"

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
#include "rmad/proto_bindings/rmad.pb.h"
#include "rmad/state_handler/state_handler_test_common.h"
#include "rmad/system/mock_power_manager_client.h"
#include "rmad/utils/json_store.h"
#include "rmad/utils/mock_cros_config_utils.h"
#include "rmad/utils/mock_gsc_utils.h"
#include "rmad/utils/mock_vpd_utils.h"
#include "rmad/utils/mock_write_protect_utils.h"

using testing::_;
using testing::Assign;
using testing::DoAll;
using testing::ElementsAre;
using testing::Eq;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::SetArgPointee;
using testing::StrictMock;

namespace {

constexpr char kValidBoardIdType[] = "12345678";
constexpr char kInvalidBoardIdType[] = "ffffffff";
constexpr char kValidBoardIdFlags[] = "00007f80";
constexpr char kInvalidBoardIdFlags[] = "00007f7f";
constexpr char kFingerprintSensorLocation[] = "power-button-top-left";

}  // namespace

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

  struct StateHandlerArgs {
    // First WP check to know if factory mode is enabled.
    // Second WP check after disabling factory mode. WP is expected to be
    // enabled.
    bool hwwp_enabled = true;
    bool enable_swwp_success = true;
    bool disable_factory_mode_success = true;
    std::string board_id_type = kValidBoardIdType;
    std::string board_id_flags = kValidBoardIdFlags;
    std::optional<std::string> fingerprint_sensor_location = std::nullopt;
    bool reset_fps_success = true;
    uint64_t shimless_mode_flags = 0;
  };

  scoped_refptr<FinalizeStateHandler> CreateInitializedStateHandler(
      const StateHandlerArgs& args) {
    // Mock |CrosConfigUtils|.
    auto mock_cros_config_utils =
        std::make_unique<NiceMock<MockCrosConfigUtils>>();
    ON_CALL(*mock_cros_config_utils, GetFingerprintSensorLocation)
        .WillByDefault(Return(args.fingerprint_sensor_location));

    // Mock |GscUtils|.
    auto mock_gsc_utils = std::make_unique<NiceMock<MockGscUtils>>();
    ON_CALL(*mock_gsc_utils, DisableFactoryMode())
        .WillByDefault(Return(args.disable_factory_mode_success));
    ON_CALL(*mock_gsc_utils, GetBoardIdType())
        .WillByDefault(Return(args.board_id_type));
    ON_CALL(*mock_gsc_utils, GetBoardIdFlags())
        .WillByDefault(Return(args.board_id_flags));

    // Mock |WriteProtectUtils|.
    auto mock_write_protect_utils =
        std::make_unique<StrictMock<MockWriteProtectUtils>>();
    EXPECT_CALL(*mock_write_protect_utils, GetHardwareWriteProtectionStatus())
        .WillRepeatedly(Return(args.hwwp_enabled));
    EXPECT_CALL(*mock_write_protect_utils, EnableSoftwareWriteProtection())
        .WillRepeatedly(Return(args.enable_swwp_success));

    // Mock |VpdUtils|.
    auto mock_vpd_utils = std::make_unique<MockVpdUtils>();
    ON_CALL(*mock_vpd_utils, GetShimlessMode(_))
        .WillByDefault(
            DoAll(SetArgPointee<0>(args.shimless_mode_flags), Return(true)));

    // Mock |PowerManagerClient|.
    reboot_called_ = false;
    auto mock_power_manager_client =
        std::make_unique<NiceMock<MockPowerManagerClient>>();
    ON_CALL(*mock_power_manager_client, Restart())
        .WillByDefault(DoAll(Assign(&reboot_called_, true), Return(true)));

    // Register signal callback.
    daemon_callback_->SetFinalizeSignalCallback(
        base::BindRepeating(&SignalSender::SendFinalizeProgressSignal,
                            base::Unretained(&signal_sender_)));
    daemon_callback_->SetExecuteResetFpmcuEntropyCallback(
        base::BindRepeating(&FinalizeStateHandlerTest::ResetFpmcuEntropy,
                            base::Unretained(this), args.reset_fps_success));

    // Initialization should always succeed.
    auto handler = base::MakeRefCounted<FinalizeStateHandler>(
        json_store_, daemon_callback_, GetTempDirPath(),
        std::move(mock_cros_config_utils), std::move(mock_gsc_utils),
        std::move(mock_write_protect_utils), std::move(mock_vpd_utils),
        std::move(mock_power_manager_client));
    EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

    return handler;
  }

  RmadState CreateFinalizeRequest(FinalizeState_FinalizeChoice choice) const {
    RmadState state;
    state.mutable_finalize()->set_choice(choice);
    return state;
  }

  void ExpectTransitionSucceeded(scoped_refptr<FinalizeStateHandler> handler,
                                 const RmadState& state,
                                 RmadState::StateCase expected_state_case) {
    auto [error, state_case] = handler->GetNextStateCase(state);
    EXPECT_EQ(error, RMAD_ERROR_OK);
    EXPECT_EQ(state_case, expected_state_case);
  }

  void ExpectTransitionFailedWithError(
      scoped_refptr<FinalizeStateHandler> handler,
      const RmadState& state,
      RmadErrorCode expected_error) {
    auto [error, state_case] = handler->GetNextStateCase(state);
    EXPECT_EQ(error, expected_error);
    EXPECT_EQ(state_case, RmadState::StateCase::kFinalize);
  }

  void ExpectSignal(FinalizeStatus_Status expected_status,
                    double expected_progress,
                    FinalizeStatus_Error expected_error =
                        FinalizeStatus::RMAD_FINALIZE_ERROR_UNKNOWN) {
    EXPECT_CALL(signal_sender_, SendFinalizeProgressSignal(_))
        .WillRepeatedly(Invoke([expected_status, expected_progress,
                                expected_error](const FinalizeStatus& status) {
          EXPECT_EQ(status.status(), expected_status);
          EXPECT_DOUBLE_EQ(status.progress(), expected_progress);
          EXPECT_EQ(status.error(), expected_error);
        }));
    task_environment_.FastForwardBy(
        FinalizeStateHandler::kReportStatusInterval);
  }

  void ResetFpmcuEntropy(bool reset_fps_success,
                         base::OnceCallback<void(bool)> callback) {
    std::move(callback).Run(reset_fps_success);
  }

 protected:
  StrictMock<SignalSender> signal_sender_;
  bool reboot_called_;

  // Variables for TaskRunner.
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
};

TEST_F(FinalizeStateHandlerTest, InitializeState_HwwpDisabled_Succeeded) {
  auto handler = CreateInitializedStateHandler({.hwwp_enabled = false});

  handler->RunState();
  ExpectSignal(FinalizeStatus::RMAD_FINALIZE_STATUS_IN_PROGRESS, 0.6);

  EXPECT_FALSE(reboot_called_);
  task_environment_.FastForwardBy(FinalizeStateHandler::kRebootDelay);
  EXPECT_TRUE(reboot_called_);
}

TEST_F(FinalizeStateHandlerTest, InitializeState_EnableSwwpFailed) {
  StateHandlerArgs args = {.hwwp_enabled = false, .enable_swwp_success = false};
  auto handler = CreateInitializedStateHandler(args);

  handler->RunState();
  ExpectSignal(FinalizeStatus::RMAD_FINALIZE_STATUS_FAILED_BLOCKING, 0,
               FinalizeStatus::RMAD_FINALIZE_ERROR_CANNOT_ENABLE_SWWP);
}

TEST_F(FinalizeStateHandlerTest, InitializeState_DisableFactoryModeFailed) {
  StateHandlerArgs args = {.hwwp_enabled = false,
                           .disable_factory_mode_success = false};
  auto handler = CreateInitializedStateHandler(args);

  handler->RunState();
  ExpectSignal(FinalizeStatus::RMAD_FINALIZE_STATUS_FAILED_BLOCKING, 0.5,
               FinalizeStatus::RMAD_FINALIZE_ERROR_CANNOT_ENABLE_HWWP);
}

TEST_F(FinalizeStateHandlerTest, TryGetNextStateCaseAtBoot_HwwpDisabled) {
  StateHandlerArgs args = {.hwwp_enabled = false};
  json_store_->SetValue(kFinalizeRebooted, true);
  auto handler = CreateInitializedStateHandler(args);

  auto [error, state] = handler->TryGetNextStateCaseAtBoot();
  EXPECT_EQ(error, RMAD_ERROR_WAIT);
  EXPECT_EQ(state, RmadState::StateCase::kFinalize);

  handler->RunState();
  ExpectSignal(FinalizeStatus::RMAD_FINALIZE_STATUS_FAILED_BLOCKING, 0.8,
               FinalizeStatus::RMAD_FINALIZE_ERROR_CANNOT_ENABLE_HWWP);
}

TEST_F(FinalizeStateHandlerTest, TryGetNextStateCaseAtBoot_InvalidBoardId) {
  StateHandlerArgs args = {.board_id_type = kInvalidBoardIdType};
  json_store_->SetValue(kFinalizeRebooted, true);
  auto handler = CreateInitializedStateHandler(args);

  auto [error, state] = handler->TryGetNextStateCaseAtBoot();
  EXPECT_EQ(error, RMAD_ERROR_WAIT);
  EXPECT_EQ(state, RmadState::StateCase::kFinalize);

  handler->RunState();
  ExpectSignal(FinalizeStatus::RMAD_FINALIZE_STATUS_FAILED_BLOCKING, 0.95,
               FinalizeStatus::RMAD_FINALIZE_ERROR_CR50);
}

TEST_F(FinalizeStateHandlerTest,
       TryGetNextStateCaseAtBoot_InvalidBoardId_Bypass) {
  StateHandlerArgs args = {.board_id_type = kInvalidBoardIdType};
  json_store_->SetValue(kFinalizeRebooted, true);
  auto handler = CreateInitializedStateHandler(args);

  // Bypass board ID check.
  EXPECT_TRUE(brillo::TouchFile(GetTempDirPath().AppendASCII(kTestDirPath)));

  auto [error, state] = handler->TryGetNextStateCaseAtBoot();
  EXPECT_EQ(error, RMAD_ERROR_WAIT);
  EXPECT_EQ(state, RmadState::StateCase::kFinalize);

  handler->RunState();
  ExpectSignal(FinalizeStatus::RMAD_FINALIZE_STATUS_COMPLETE, 1);
}

TEST_F(FinalizeStateHandlerTest,
       TryGetNextStateCaseAtBoot_InvalidBoardId_BypassWithFlags) {
  StateHandlerArgs args = {
      .board_id_type = kInvalidBoardIdType,
      .shimless_mode_flags = kShimlessModeFlagsBoardIdCheckResultBypass};
  json_store_->SetValue(kFinalizeRebooted, true);
  auto handler = CreateInitializedStateHandler(args);

  auto [error, state] = handler->TryGetNextStateCaseAtBoot();
  EXPECT_EQ(error, RMAD_ERROR_WAIT);
  EXPECT_EQ(state, RmadState::StateCase::kFinalize);

  handler->RunState();
  ExpectSignal(FinalizeStatus::RMAD_FINALIZE_STATUS_COMPLETE, 1);
}

TEST_F(FinalizeStateHandlerTest,
       TryGetNextStateCaseAtBoot_InvalidBoardIdFlags) {
  StateHandlerArgs args = {.board_id_flags = kInvalidBoardIdFlags};
  json_store_->SetValue(kFinalizeRebooted, true);
  auto handler = CreateInitializedStateHandler(args);

  auto [error, state] = handler->TryGetNextStateCaseAtBoot();
  EXPECT_EQ(error, RMAD_ERROR_WAIT);
  EXPECT_EQ(state, RmadState::StateCase::kFinalize);

  handler->RunState();
  ExpectSignal(FinalizeStatus::RMAD_FINALIZE_STATUS_FAILED_BLOCKING, 0.95,
               FinalizeStatus::RMAD_FINALIZE_ERROR_CR50);
}

TEST_F(FinalizeStateHandlerTest,
       TryGetNextStateCaseAtBoot_InvalidBoardIdFlags_Bypass) {
  StateHandlerArgs args = {.board_id_flags = kInvalidBoardIdFlags};
  json_store_->SetValue(kFinalizeRebooted, true);
  auto handler = CreateInitializedStateHandler(args);

  // Bypass board ID check.
  EXPECT_TRUE(brillo::TouchFile(GetTempDirPath().AppendASCII(kTestDirPath)));

  auto [error, state] = handler->TryGetNextStateCaseAtBoot();
  EXPECT_EQ(error, RMAD_ERROR_WAIT);
  EXPECT_EQ(state, RmadState::StateCase::kFinalize);

  handler->RunState();
  ExpectSignal(FinalizeStatus::RMAD_FINALIZE_STATUS_COMPLETE, 1);
}

TEST_F(FinalizeStateHandlerTest,
       TryGetNextStateCaseAtBoot_InvalidBoardIdFlags_BypassWithFlags) {
  StateHandlerArgs args = {
      .board_id_type = kInvalidBoardIdFlags,
      .shimless_mode_flags = kShimlessModeFlagsBoardIdCheckResultBypass};
  json_store_->SetValue(kFinalizeRebooted, true);
  auto handler = CreateInitializedStateHandler(args);

  auto [error, state] = handler->TryGetNextStateCaseAtBoot();
  EXPECT_EQ(error, RMAD_ERROR_WAIT);
  EXPECT_EQ(state, RmadState::StateCase::kFinalize);

  handler->RunState();
  ExpectSignal(FinalizeStatus::RMAD_FINALIZE_STATUS_COMPLETE, 1);
}

TEST_F(FinalizeStateHandlerTest, TryGetNextStateCaseAtBoot_HasFps_Success) {
  StateHandlerArgs args = {.fingerprint_sensor_location =
                               kFingerprintSensorLocation};
  json_store_->SetValue(kFinalizeRebooted, true);
  auto handler = CreateInitializedStateHandler(args);

  auto [error, state] = handler->TryGetNextStateCaseAtBoot();
  EXPECT_EQ(error, RMAD_ERROR_WAIT);
  EXPECT_EQ(state, RmadState::StateCase::kFinalize);

  handler->RunState();
  ExpectSignal(FinalizeStatus::RMAD_FINALIZE_STATUS_COMPLETE, 1);
}

TEST_F(FinalizeStateHandlerTest, TryGetNextStateCaseAtBoot_FpsNone_Success) {
  StateHandlerArgs args = {.fingerprint_sensor_location = "none"};
  json_store_->SetValue(kFinalizeRebooted, true);
  auto handler = CreateInitializedStateHandler(args);

  auto [error, state] = handler->TryGetNextStateCaseAtBoot();
  EXPECT_EQ(error, RMAD_ERROR_WAIT);
  EXPECT_EQ(state, RmadState::StateCase::kFinalize);

  handler->RunState();
  ExpectSignal(FinalizeStatus::RMAD_FINALIZE_STATUS_COMPLETE, 1);
}

TEST_F(FinalizeStateHandlerTest,
       TryGetNextStateCaseAtBoot_ResetFpsFailedBlocking) {
  StateHandlerArgs args = {
      .fingerprint_sensor_location = kFingerprintSensorLocation,
      .reset_fps_success = false};
  json_store_->SetValue(kFinalizeRebooted, true);
  auto handler = CreateInitializedStateHandler(args);

  auto [error, state] = handler->TryGetNextStateCaseAtBoot();
  EXPECT_EQ(error, RMAD_ERROR_WAIT);
  EXPECT_EQ(state, RmadState::StateCase::kFinalize);

  handler->RunState();
  ExpectSignal(FinalizeStatus::RMAD_FINALIZE_STATUS_FAILED_BLOCKING, 0.9,
               FinalizeStatus::RMAD_FINALIZE_ERROR_INTERNAL);
}

TEST_F(FinalizeStateHandlerTest, GetNextStateCase_Succeeded) {
  auto handler = CreateInitializedStateHandler({});
  json_store_->SetValue(kFinalizeRebooted, true);

  auto [error, state] = handler->TryGetNextStateCaseAtBoot();
  EXPECT_EQ(error, RMAD_ERROR_WAIT);
  EXPECT_EQ(state, RmadState::StateCase::kFinalize);

  handler->RunState();

  task_environment_.RunUntilIdle();

  ExpectTransitionSucceeded(
      handler,
      CreateFinalizeRequest(FinalizeState::RMAD_FINALIZE_CHOICE_CONTINUE),
      RmadState::StateCase::kRepairComplete);
}

TEST_F(FinalizeStateHandlerTest, GetNextStateCase_InProgress) {
  // RunState() is not called yet, so there's no call to get HWWP status either.
  auto handler = CreateInitializedStateHandler({});

  ExpectTransitionFailedWithError(
      handler,
      CreateFinalizeRequest(FinalizeState::RMAD_FINALIZE_CHOICE_CONTINUE),
      RMAD_ERROR_WAIT);
}

TEST_F(FinalizeStateHandlerTest, GetNextStateCase_MissingState) {
  auto handler = CreateInitializedStateHandler({});
  handler->RunState();
  task_environment_.RunUntilIdle();

  ExpectTransitionFailedWithError(handler, RmadState(),
                                  RMAD_ERROR_REQUEST_INVALID);
}

TEST_F(FinalizeStateHandlerTest, GetNextStateCase_MissingArgs) {
  auto handler = CreateInitializedStateHandler({});
  handler->RunState();
  task_environment_.RunUntilIdle();

  ExpectTransitionFailedWithError(
      handler,
      CreateFinalizeRequest(FinalizeState::RMAD_FINALIZE_CHOICE_UNKNOWN),
      RMAD_ERROR_REQUEST_ARGS_MISSING);
}

TEST_F(FinalizeStateHandlerTest, GetNextStateCase_BlockingFailure_Retry) {
  StateHandlerArgs args = {.disable_factory_mode_success = false};
  auto handler = CreateInitializedStateHandler(args);
  handler->RunState();
  task_environment_.RunUntilIdle();

  // Get blocking failure.
  ExpectTransitionFailedWithError(
      handler,
      CreateFinalizeRequest(FinalizeState::RMAD_FINALIZE_CHOICE_CONTINUE),
      RMAD_ERROR_FINALIZATION_FAILED);

  // Request a retry.
  ExpectTransitionFailedWithError(
      handler, CreateFinalizeRequest(FinalizeState::RMAD_FINALIZE_CHOICE_RETRY),
      RMAD_ERROR_WAIT);

  task_environment_.RunUntilIdle();

  // Still fails.
  ExpectTransitionFailedWithError(
      handler,
      CreateFinalizeRequest(FinalizeState::RMAD_FINALIZE_CHOICE_CONTINUE),
      RMAD_ERROR_FINALIZATION_FAILED);
}

}  // namespace rmad
