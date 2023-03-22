// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/provision_device_state_handler.h"

#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/memory/scoped_refptr.h>
#include <base/run_loop.h>
#include <base/test/task_environment.h>
#include <brillo/file_utils.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rmad/constants.h"
#include "rmad/proto_bindings/rmad.pb.h"
#include "rmad/ssfc/mock_ssfc_prober.h"
#include "rmad/state_handler/state_handler_test_common.h"
#include "rmad/system/mock_power_manager_client.h"
#include "rmad/utils/json_store.h"
#include "rmad/utils/mock_cbi_utils.h"
#include "rmad/utils/mock_cmd_utils.h"
#include "rmad/utils/mock_cr50_utils.h"
#include "rmad/utils/mock_cros_config_utils.h"
#include "rmad/utils/mock_iio_sensor_probe_utils.h"
#include "rmad/utils/mock_vpd_utils.h"
#include "rmad/utils/mock_write_protect_utils.h"

using testing::_;
using testing::Assign;
using testing::DoAll;
using testing::Eq;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::SetArgPointee;
using testing::WithArg;

namespace {

constexpr char kTestModelName[] = "TestModelName";
constexpr uint32_t kTestSsfc = 0x1234;

constexpr char kEmptyBoardIdType[] = "ffffffff";
constexpr char kValidBoardIdType[] = "12345678";
constexpr char kInvalidBoardIdType[] = "5a5a4352";  // ZZCR.
constexpr char kPvtBoardIdFlags[] = "00007f80";
constexpr char kCustomLabelPvtBoardIdFlags[] = "00003f80";

struct StateHandlerArgs {
  bool get_model_name = true;
  bool get_ssfc = true;
  bool need_update_ssfc = true;
  bool set_ssfc = true;
  bool set_stable_dev_secret = true;
  bool flush_vpd = true;
  bool hwwp_enabled = false;
  bool reset_gbb_success = true;
  bool board_id_read_success = true;
  std::string board_id_type = kValidBoardIdType;
  std::string board_id_flags = kPvtBoardIdFlags;
  std::set<rmad::RmadComponent> probed_components = {
      rmad::RMAD_COMPONENT_BASE_ACCELEROMETER,
      rmad::RMAD_COMPONENT_LID_ACCELEROMETER,
      rmad::RMAD_COMPONENT_BASE_GYROSCOPE, rmad::RMAD_COMPONENT_LID_GYROSCOPE};
};

}  // namespace

namespace rmad {

class ProvisionDeviceStateHandlerTest : public StateHandlerTest {
 public:
  // Helper class to mock the callback function to send signal.
  class SignalSender {
   public:
    MOCK_METHOD(void,
                SendProvisionProgressSignal,
                (const ProvisionStatus&),
                (const));
  };

  void QueueStatus(const ProvisionStatus& status) {
    status_history_.push_back(status);
  }

  scoped_refptr<ProvisionDeviceStateHandler> CreateStateHandler(
      const StateHandlerArgs& args = {}) {
    // Expect signal is always sent.
    ON_CALL(signal_sender_, SendProvisionProgressSignal(_))
        .WillByDefault(WithArg<0>(
            Invoke(this, &ProvisionDeviceStateHandlerTest::QueueStatus)));

    // Mock |SsfcProber|.
    auto mock_ssfc_prober = std::make_unique<NiceMock<MockSsfcProber>>();
    ON_CALL(*mock_ssfc_prober, IsSsfcRequired())
        .WillByDefault(Return(args.need_update_ssfc));
    if (args.need_update_ssfc && args.get_ssfc) {
      ON_CALL(*mock_ssfc_prober, ProbeSsfc(_))
          .WillByDefault(DoAll(SetArgPointee<0>(kTestSsfc), Return(true)));
    } else {
      ON_CALL(*mock_ssfc_prober, ProbeSsfc(_)).WillByDefault(Return(false));
    }

    // Mock |PowerManagerClient|.
    reboot_called_ = false;
    auto mock_power_manager_client =
        std::make_unique<NiceMock<MockPowerManagerClient>>();
    ON_CALL(*mock_power_manager_client, Restart())
        .WillByDefault(DoAll(Assign(&reboot_called_, true), Return(true)));

    // Mock |CbiUtils|.
    auto mock_cbi_utils = std::make_unique<NiceMock<MockCbiUtils>>();
    ON_CALL(*mock_cbi_utils, SetSsfc(_)).WillByDefault(Return(args.set_ssfc));

    // Mock |CmdUtils|.
    auto mock_cmd_utils = std::make_unique<NiceMock<MockCmdUtils>>();
    ON_CALL(*mock_cmd_utils, GetOutput(_, _))
        .WillByDefault(Return(args.reset_gbb_success));

    // Mock |Cr50Utils|.
    auto mock_cr50_utils = std::make_unique<NiceMock<MockCr50Utils>>();
    if (args.board_id_read_success) {
      ON_CALL(*mock_cr50_utils, GetBoardIdType(_))
          .WillByDefault(
              DoAll(SetArgPointee<0>(args.board_id_type), Return(true)));
      ON_CALL(*mock_cr50_utils, GetBoardIdFlags(_))
          .WillByDefault(
              DoAll(SetArgPointee<0>(args.board_id_flags), Return(true)));
    } else {
      ON_CALL(*mock_cr50_utils, GetBoardIdType(_)).WillByDefault(Return(false));
      ON_CALL(*mock_cr50_utils, GetBoardIdFlags(_))
          .WillByDefault(Return(false));
    }
    ON_CALL(*mock_cr50_utils, SetBoardId(_))
        .WillByDefault(Invoke([args](bool is_custom_label) {
          if (args.board_id_type != kEmptyBoardIdType) {
            return false;
          }
          if (is_custom_label) {
            return (args.board_id_flags == kCustomLabelPvtBoardIdFlags);
          }
          return (args.board_id_flags == kPvtBoardIdFlags);
        }));

    // Mock |CrosConfigUtils|.
    auto mock_cros_config_utils =
        std::make_unique<NiceMock<MockCrosConfigUtils>>();
    if (args.get_model_name) {
      ON_CALL(*mock_cros_config_utils, GetModelName(_))
          .WillByDefault(DoAll(SetArgPointee<0>(std::string(kTestModelName)),
                               Return(true)));
    } else {
      ON_CALL(*mock_cros_config_utils, GetModelName(_))
          .WillByDefault(Return(false));
    }

    // Mock |WriteProtectUtils|.
    auto mock_write_protect_utils =
        std::make_unique<NiceMock<MockWriteProtectUtils>>();
    ON_CALL(*mock_write_protect_utils, GetHardwareWriteProtectionStatus(_))
        .WillByDefault(
            DoAll(SetArgPointee<0>(args.hwwp_enabled), Return(true)));

    // Mock |IioSensorProbeUtils|.
    auto mock_iio_sensor_probe_utils =
        std::make_unique<NiceMock<MockIioSensorProbeUtils>>();
    ON_CALL(*mock_iio_sensor_probe_utils, Probe())
        .WillByDefault(Return(args.probed_components));

    // Mock |VpdUtils|.
    auto mock_vpd_utils = std::make_unique<NiceMock<MockVpdUtils>>();
    ON_CALL(*mock_vpd_utils, SetStableDeviceSecret(_))
        .WillByDefault(Return(args.set_stable_dev_secret));
    ON_CALL(*mock_vpd_utils, FlushOutRoVpdCache())
        .WillByDefault(Return(args.flush_vpd));

    // Register signal callback.
    daemon_callback_->SetProvisionSignalCallback(
        base::BindRepeating(&SignalSender::SendProvisionProgressSignal,
                            base::Unretained(&signal_sender_)));

    return base::MakeRefCounted<ProvisionDeviceStateHandler>(
        json_store_, daemon_callback_, GetTempDirPath(),
        std::move(mock_ssfc_prober), std::move(mock_power_manager_client),
        std::move(mock_cbi_utils), std::move(mock_cmd_utils),
        std::move(mock_cr50_utils), std::move(mock_cros_config_utils),
        std::move(mock_write_protect_utils),
        std::move(mock_iio_sensor_probe_utils), std::move(mock_vpd_utils));
  }

  void RunHandlerTaskRunner(
      scoped_refptr<ProvisionDeviceStateHandler> handler) {
    handler->GetTaskRunner()->PostTask(FROM_HERE, run_loop_.QuitClosure());
    run_loop_.Run();
  }

 protected:
  NiceMock<SignalSender> signal_sender_;
  std::vector<ProvisionStatus> status_history_;
  bool reboot_called_;

  // Variables for TaskRunner.
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::ThreadPoolExecutionMode::ASYNC,
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  base::RunLoop run_loop_;
};

TEST_F(ProvisionDeviceStateHandlerTest, InitializeState_Success) {
  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  handler->RunState();
  RunHandlerTaskRunner(handler);
}

TEST_F(ProvisionDeviceStateHandlerTest, Clenaup_Success) {
  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  handler->RunState();
  handler->CleanUpState();
  RunHandlerTaskRunner(handler);
}

TEST_F(ProvisionDeviceStateHandlerTest, GetNextStateCase_Success) {
  auto handler = CreateStateHandler();
  json_store_->SetValue(kSameOwner, false);
  json_store_->SetValue(kWipeDevice, true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  handler->RunState();
  task_environment_.FastForwardBy(
      ProvisionDeviceStateHandler::kReportStatusInterval);
  EXPECT_GE(status_history_.size(), 1);
  EXPECT_EQ(status_history_.back().status(),
            ProvisionStatus::RMAD_PROVISION_STATUS_COMPLETE);

  auto provision = std::make_unique<ProvisionDeviceState>();
  provision->set_choice(ProvisionDeviceState::RMAD_PROVISION_CHOICE_CONTINUE);
  RmadState state;
  state.set_allocated_provision_device(provision.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_EXPECT_REBOOT);
  task_environment_.FastForwardBy(ProvisionDeviceStateHandler::kRebootDelay);
  EXPECT_EQ(reboot_called_, true);
  EXPECT_EQ(state_case, RmadState::StateCase::kProvisionDevice);

  RunHandlerTaskRunner(handler);
}

TEST_F(ProvisionDeviceStateHandlerTest, TryGetNextStateCaseAtBoot_Failed) {
  auto handler = CreateStateHandler();
  json_store_->SetValue(kSameOwner, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  handler->RunState();
  auto [error, state] = handler->TryGetNextStateCaseAtBoot();
  EXPECT_EQ(error, RMAD_ERROR_TRANSITION_FAILED);
  EXPECT_EQ(state, RmadState::StateCase::kProvisionDevice);

  RunHandlerTaskRunner(handler);
}

TEST_F(ProvisionDeviceStateHandlerTest,
       TryGetNextStateCaseAtBoot_NeedCalibrationSuccess) {
  auto handler = CreateStateHandler();
  json_store_->SetValue(kSameOwner, false);
  json_store_->SetValue(
      kReplacedComponentNames,
      std::vector<std::string>{
          RmadComponent_Name(RMAD_COMPONENT_BATTERY),
          RmadComponent_Name(RMAD_COMPONENT_BASE_ACCELEROMETER),
          RmadComponent_Name(RMAD_COMPONENT_LID_ACCELEROMETER),
          RmadComponent_Name(RMAD_COMPONENT_BASE_GYROSCOPE),
          RmadComponent_Name(RMAD_COMPONENT_LID_GYROSCOPE)});

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  handler->RunState();
  task_environment_.FastForwardBy(
      ProvisionDeviceStateHandler::kReportStatusInterval);
  EXPECT_GE(status_history_.size(), 1);
  EXPECT_EQ(status_history_.back().status(),
            ProvisionStatus::RMAD_PROVISION_STATUS_COMPLETE);

  auto provision = std::make_unique<ProvisionDeviceState>();
  provision->set_choice(ProvisionDeviceState::RMAD_PROVISION_CHOICE_CONTINUE);
  RmadState state;
  state.set_allocated_provision_device(provision.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_EXPECT_REBOOT);
  task_environment_.FastForwardBy(ProvisionDeviceStateHandler::kRebootDelay);
  EXPECT_EQ(reboot_called_, true);
  EXPECT_EQ(state_case, RmadState::StateCase::kProvisionDevice);

  auto handler_after_reboot = CreateStateHandler();
  EXPECT_EQ(handler_after_reboot->InitializeState(), RMAD_ERROR_OK);
  handler_after_reboot->RunState();
  auto [error_try_boot, state_case_try_boot] =
      handler_after_reboot->TryGetNextStateCaseAtBoot();
  EXPECT_EQ(error_try_boot, RMAD_ERROR_OK);
  EXPECT_EQ(state_case_try_boot, RmadState::StateCase::kSetupCalibration);

  RunHandlerTaskRunner(handler);
}

TEST_F(ProvisionDeviceStateHandlerTest,
       TryGetNextStateCaseAtBoot_NoNeedCalibrationSuccess) {
  auto handler = CreateStateHandler();
  json_store_->SetValue(kSameOwner, false);
  json_store_->SetValue(
      kReplacedComponentNames,
      std::vector<std::string>{RmadComponent_Name(RMAD_COMPONENT_BATTERY)});

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  handler->RunState();
  task_environment_.FastForwardBy(
      ProvisionDeviceStateHandler::kReportStatusInterval);
  EXPECT_GE(status_history_.size(), 1);
  EXPECT_EQ(status_history_.back().status(),
            ProvisionStatus::RMAD_PROVISION_STATUS_COMPLETE);

  auto provision = std::make_unique<ProvisionDeviceState>();
  provision->set_choice(ProvisionDeviceState::RMAD_PROVISION_CHOICE_CONTINUE);
  RmadState state;
  state.set_allocated_provision_device(provision.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_EXPECT_REBOOT);
  task_environment_.FastForwardBy(ProvisionDeviceStateHandler::kRebootDelay);
  EXPECT_EQ(reboot_called_, true);
  EXPECT_EQ(state_case, RmadState::StateCase::kProvisionDevice);

  auto handler_after_reboot = CreateStateHandler();
  EXPECT_EQ(handler_after_reboot->InitializeState(), RMAD_ERROR_OK);
  handler_after_reboot->RunState();
  auto [error_try_boot, state_case_try_boot] =
      handler_after_reboot->TryGetNextStateCaseAtBoot();
  EXPECT_EQ(error_try_boot, RMAD_ERROR_OK);
  EXPECT_EQ(state_case_try_boot, RmadState::StateCase::kFinalize);

  RunHandlerTaskRunner(handler);
}

TEST_F(ProvisionDeviceStateHandlerTest,
       TryGetNextStateCaseAtBoot_BaseAccNotProbedComplete) {
  auto handler = CreateStateHandler();
  json_store_->SetValue(kSameOwner, false);
  json_store_->SetValue(
      kReplacedComponentNames,
      std::vector<std::string>{
          RmadComponent_Name(RMAD_COMPONENT_BATTERY),
          RmadComponent_Name(RMAD_COMPONENT_BASE_ACCELEROMETER),
          RmadComponent_Name(RMAD_COMPONENT_LID_ACCELEROMETER),
          RmadComponent_Name(RMAD_COMPONENT_BASE_GYROSCOPE),
          RmadComponent_Name(RMAD_COMPONENT_LID_GYROSCOPE)});
  json_store_->SetValue(kMlbRepair, false);

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  handler->RunState();
  task_environment_.FastForwardBy(
      ProvisionDeviceStateHandler::kReportStatusInterval);
  EXPECT_GE(status_history_.size(), 1);
  EXPECT_EQ(status_history_.back().status(),
            ProvisionStatus::RMAD_PROVISION_STATUS_COMPLETE);

  auto provision = std::make_unique<ProvisionDeviceState>();
  provision->set_choice(ProvisionDeviceState::RMAD_PROVISION_CHOICE_CONTINUE);
  RmadState state;
  state.set_allocated_provision_device(provision.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_EXPECT_REBOOT);
  task_environment_.FastForwardBy(ProvisionDeviceStateHandler::kRebootDelay);
  EXPECT_EQ(reboot_called_, true);
  EXPECT_EQ(state_case, RmadState::StateCase::kProvisionDevice);

  auto handler_after_reboot = CreateStateHandler(
      {.probed_components = {RMAD_COMPONENT_LID_ACCELEROMETER,
                             RMAD_COMPONENT_BASE_GYROSCOPE,
                             RMAD_COMPONENT_LID_GYROSCOPE}});
  EXPECT_EQ(handler_after_reboot->InitializeState(), RMAD_ERROR_OK);
  handler_after_reboot->RunState();
  task_environment_.FastForwardBy(
      ProvisionDeviceStateHandler::kReportStatusInterval);
  EXPECT_GE(status_history_.size(), 2);
  EXPECT_EQ(status_history_.back().status(),
            ProvisionStatus::RMAD_PROVISION_STATUS_COMPLETE);
  auto [error_try_boot, state_case_try_boot] =
      handler_after_reboot->TryGetNextStateCaseAtBoot();
  EXPECT_EQ(error_try_boot, RMAD_ERROR_OK);
  EXPECT_EQ(state_case_try_boot, RmadState::StateCase::kSetupCalibration);

  InstructionCalibrationStatusMap calibration_map;
  EXPECT_EQ(GetCalibrationMap(json_store_, &calibration_map), true);
  EXPECT_EQ(calibration_map[GetCalibrationSetupInstruction(
                                RMAD_COMPONENT_BASE_ACCELEROMETER)]
                .count(RMAD_COMPONENT_BASE_ACCELEROMETER),
            0);
  EXPECT_EQ(
      calibration_map[GetCalibrationSetupInstruction(
          RMAD_COMPONENT_LID_ACCELEROMETER)][RMAD_COMPONENT_LID_ACCELEROMETER],
      CalibrationComponentStatus::RMAD_CALIBRATION_WAITING);
  EXPECT_EQ(calibration_map[GetCalibrationSetupInstruction(
                RMAD_COMPONENT_BASE_GYROSCOPE)][RMAD_COMPONENT_BASE_GYROSCOPE],
            CalibrationComponentStatus::RMAD_CALIBRATION_WAITING);
  EXPECT_EQ(calibration_map[GetCalibrationSetupInstruction(
                RMAD_COMPONENT_LID_GYROSCOPE)][RMAD_COMPONENT_LID_GYROSCOPE],
            CalibrationComponentStatus::RMAD_CALIBRATION_WAITING);

  RunHandlerTaskRunner(handler);
}

TEST_F(ProvisionDeviceStateHandlerTest,
       TryGetNextStateCaseAtBoot_LidAccNotProbedComplete) {
  auto handler = CreateStateHandler();
  json_store_->SetValue(kSameOwner, false);
  json_store_->SetValue(
      kReplacedComponentNames,
      std::vector<std::string>{
          RmadComponent_Name(RMAD_COMPONENT_BATTERY),
          RmadComponent_Name(RMAD_COMPONENT_BASE_ACCELEROMETER),
          RmadComponent_Name(RMAD_COMPONENT_LID_ACCELEROMETER),
          RmadComponent_Name(RMAD_COMPONENT_BASE_GYROSCOPE),
          RmadComponent_Name(RMAD_COMPONENT_LID_GYROSCOPE)});
  json_store_->SetValue(kMlbRepair, false);

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  handler->RunState();
  task_environment_.FastForwardBy(
      ProvisionDeviceStateHandler::kReportStatusInterval);
  EXPECT_GE(status_history_.size(), 1);
  EXPECT_EQ(status_history_.back().status(),
            ProvisionStatus::RMAD_PROVISION_STATUS_COMPLETE);

  auto provision = std::make_unique<ProvisionDeviceState>();
  provision->set_choice(ProvisionDeviceState::RMAD_PROVISION_CHOICE_CONTINUE);
  RmadState state;
  state.set_allocated_provision_device(provision.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_EXPECT_REBOOT);
  task_environment_.FastForwardBy(ProvisionDeviceStateHandler::kRebootDelay);
  EXPECT_EQ(reboot_called_, true);
  EXPECT_EQ(state_case, RmadState::StateCase::kProvisionDevice);

  auto handler_after_reboot = CreateStateHandler(
      {.probed_components = {RMAD_COMPONENT_BASE_ACCELEROMETER,
                             RMAD_COMPONENT_BASE_GYROSCOPE,
                             RMAD_COMPONENT_LID_GYROSCOPE}});
  EXPECT_EQ(handler_after_reboot->InitializeState(), RMAD_ERROR_OK);
  handler_after_reboot->RunState();
  task_environment_.FastForwardBy(
      ProvisionDeviceStateHandler::kReportStatusInterval);
  EXPECT_GE(status_history_.size(), 2);
  EXPECT_EQ(status_history_.back().status(),
            ProvisionStatus::RMAD_PROVISION_STATUS_COMPLETE);
  auto [error_try_boot, state_case_try_boot] =
      handler_after_reboot->TryGetNextStateCaseAtBoot();
  EXPECT_EQ(error_try_boot, RMAD_ERROR_OK);
  EXPECT_EQ(state_case_try_boot, RmadState::StateCase::kSetupCalibration);

  InstructionCalibrationStatusMap calibration_map;
  EXPECT_EQ(GetCalibrationMap(json_store_, &calibration_map), true);
  EXPECT_EQ(calibration_map[GetCalibrationSetupInstruction(
                RMAD_COMPONENT_BASE_ACCELEROMETER)]
                           [RMAD_COMPONENT_BASE_ACCELEROMETER],
            CalibrationComponentStatus::RMAD_CALIBRATION_WAITING);
  EXPECT_EQ(calibration_map[GetCalibrationSetupInstruction(
                                RMAD_COMPONENT_LID_ACCELEROMETER)]
                .count(RMAD_COMPONENT_LID_ACCELEROMETER),
            0);
  EXPECT_EQ(calibration_map[GetCalibrationSetupInstruction(
                RMAD_COMPONENT_BASE_GYROSCOPE)][RMAD_COMPONENT_BASE_GYROSCOPE],
            CalibrationComponentStatus::RMAD_CALIBRATION_WAITING);
  EXPECT_EQ(calibration_map[GetCalibrationSetupInstruction(
                RMAD_COMPONENT_LID_GYROSCOPE)][RMAD_COMPONENT_LID_GYROSCOPE],
            CalibrationComponentStatus::RMAD_CALIBRATION_WAITING);

  RunHandlerTaskRunner(handler);
}

TEST_F(ProvisionDeviceStateHandlerTest,
       TryGetNextStateCaseAtBoot_BaseGyroNotProbedComplete) {
  auto handler = CreateStateHandler();
  json_store_->SetValue(kSameOwner, false);
  json_store_->SetValue(
      kReplacedComponentNames,
      std::vector<std::string>{
          RmadComponent_Name(RMAD_COMPONENT_BATTERY),
          RmadComponent_Name(RMAD_COMPONENT_BASE_ACCELEROMETER),
          RmadComponent_Name(RMAD_COMPONENT_LID_ACCELEROMETER),
          RmadComponent_Name(RMAD_COMPONENT_BASE_GYROSCOPE),
          RmadComponent_Name(RMAD_COMPONENT_LID_GYROSCOPE)});
  json_store_->SetValue(kMlbRepair, false);

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  handler->RunState();
  task_environment_.FastForwardBy(
      ProvisionDeviceStateHandler::kReportStatusInterval);
  EXPECT_GE(status_history_.size(), 1);
  EXPECT_EQ(status_history_.back().status(),
            ProvisionStatus::RMAD_PROVISION_STATUS_COMPLETE);

  auto provision = std::make_unique<ProvisionDeviceState>();
  provision->set_choice(ProvisionDeviceState::RMAD_PROVISION_CHOICE_CONTINUE);
  RmadState state;
  state.set_allocated_provision_device(provision.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_EXPECT_REBOOT);
  task_environment_.FastForwardBy(ProvisionDeviceStateHandler::kRebootDelay);
  EXPECT_EQ(reboot_called_, true);
  EXPECT_EQ(state_case, RmadState::StateCase::kProvisionDevice);

  auto handler_after_reboot = CreateStateHandler(
      {.probed_components = {RMAD_COMPONENT_BASE_ACCELEROMETER,
                             RMAD_COMPONENT_LID_ACCELEROMETER,
                             RMAD_COMPONENT_LID_GYROSCOPE}});
  EXPECT_EQ(handler_after_reboot->InitializeState(), RMAD_ERROR_OK);
  handler_after_reboot->RunState();
  task_environment_.FastForwardBy(
      ProvisionDeviceStateHandler::kReportStatusInterval);
  EXPECT_GE(status_history_.size(), 2);
  EXPECT_EQ(status_history_.back().status(),
            ProvisionStatus::RMAD_PROVISION_STATUS_COMPLETE);
  auto [error_try_boot, state_case_try_boot] =
      handler_after_reboot->TryGetNextStateCaseAtBoot();
  EXPECT_EQ(error_try_boot, RMAD_ERROR_OK);
  EXPECT_EQ(state_case_try_boot, RmadState::StateCase::kSetupCalibration);

  InstructionCalibrationStatusMap calibration_map;
  EXPECT_EQ(GetCalibrationMap(json_store_, &calibration_map), true);
  EXPECT_EQ(calibration_map[GetCalibrationSetupInstruction(
                RMAD_COMPONENT_BASE_ACCELEROMETER)]
                           [RMAD_COMPONENT_BASE_ACCELEROMETER],
            CalibrationComponentStatus::RMAD_CALIBRATION_WAITING);
  EXPECT_EQ(
      calibration_map[GetCalibrationSetupInstruction(
          RMAD_COMPONENT_LID_ACCELEROMETER)][RMAD_COMPONENT_LID_ACCELEROMETER],
      CalibrationComponentStatus::RMAD_CALIBRATION_WAITING);
  EXPECT_EQ(calibration_map[GetCalibrationSetupInstruction(
                                RMAD_COMPONENT_BASE_GYROSCOPE)]
                .count(RMAD_COMPONENT_BASE_GYROSCOPE),
            0);
  EXPECT_EQ(calibration_map[GetCalibrationSetupInstruction(
                RMAD_COMPONENT_LID_GYROSCOPE)][RMAD_COMPONENT_LID_GYROSCOPE],
            CalibrationComponentStatus::RMAD_CALIBRATION_WAITING);

  RunHandlerTaskRunner(handler);
}

TEST_F(ProvisionDeviceStateHandlerTest,
       TryGetNextStateCaseAtBoot_LidGyroNotProbedComplete) {
  auto handler = CreateStateHandler();
  json_store_->SetValue(kSameOwner, false);
  json_store_->SetValue(
      kReplacedComponentNames,
      std::vector<std::string>{
          RmadComponent_Name(RMAD_COMPONENT_BATTERY),
          RmadComponent_Name(RMAD_COMPONENT_BASE_ACCELEROMETER),
          RmadComponent_Name(RMAD_COMPONENT_LID_ACCELEROMETER),
          RmadComponent_Name(RMAD_COMPONENT_BASE_GYROSCOPE),
          RmadComponent_Name(RMAD_COMPONENT_LID_GYROSCOPE)});
  json_store_->SetValue(kMlbRepair, false);

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  handler->RunState();
  task_environment_.FastForwardBy(
      ProvisionDeviceStateHandler::kReportStatusInterval);
  EXPECT_GE(status_history_.size(), 1);
  EXPECT_EQ(status_history_.back().status(),
            ProvisionStatus::RMAD_PROVISION_STATUS_COMPLETE);

  auto provision = std::make_unique<ProvisionDeviceState>();
  provision->set_choice(ProvisionDeviceState::RMAD_PROVISION_CHOICE_CONTINUE);
  RmadState state;
  state.set_allocated_provision_device(provision.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_EXPECT_REBOOT);
  task_environment_.FastForwardBy(ProvisionDeviceStateHandler::kRebootDelay);
  EXPECT_EQ(reboot_called_, true);
  EXPECT_EQ(state_case, RmadState::StateCase::kProvisionDevice);

  auto handler_after_reboot = CreateStateHandler(
      {.probed_components = {RMAD_COMPONENT_BASE_ACCELEROMETER,
                             RMAD_COMPONENT_LID_ACCELEROMETER,
                             RMAD_COMPONENT_BASE_GYROSCOPE}});
  EXPECT_EQ(handler_after_reboot->InitializeState(), RMAD_ERROR_OK);
  handler_after_reboot->RunState();
  task_environment_.FastForwardBy(
      ProvisionDeviceStateHandler::kReportStatusInterval);
  EXPECT_GE(status_history_.size(), 2);
  EXPECT_EQ(status_history_.back().status(),
            ProvisionStatus::RMAD_PROVISION_STATUS_COMPLETE);
  auto [error_try_boot, state_case_try_boot] =
      handler_after_reboot->TryGetNextStateCaseAtBoot();
  EXPECT_EQ(error_try_boot, RMAD_ERROR_OK);
  EXPECT_EQ(state_case_try_boot, RmadState::StateCase::kSetupCalibration);

  InstructionCalibrationStatusMap calibration_map;
  EXPECT_EQ(GetCalibrationMap(json_store_, &calibration_map), true);
  EXPECT_EQ(calibration_map[GetCalibrationSetupInstruction(
                RMAD_COMPONENT_BASE_ACCELEROMETER)]
                           [RMAD_COMPONENT_BASE_ACCELEROMETER],
            CalibrationComponentStatus::RMAD_CALIBRATION_WAITING);
  EXPECT_EQ(
      calibration_map[GetCalibrationSetupInstruction(
          RMAD_COMPONENT_LID_ACCELEROMETER)][RMAD_COMPONENT_LID_ACCELEROMETER],
      CalibrationComponentStatus::RMAD_CALIBRATION_WAITING);
  EXPECT_EQ(calibration_map[GetCalibrationSetupInstruction(
                RMAD_COMPONENT_BASE_GYROSCOPE)][RMAD_COMPONENT_BASE_GYROSCOPE],
            CalibrationComponentStatus::RMAD_CALIBRATION_WAITING);
  EXPECT_EQ(calibration_map[GetCalibrationSetupInstruction(
                                RMAD_COMPONENT_LID_GYROSCOPE)]
                .count(RMAD_COMPONENT_LID_GYROSCOPE),
            0);

  RunHandlerTaskRunner(handler);
}

TEST_F(ProvisionDeviceStateHandlerTest,
       TryGetNextStateCaseAtBoot_PartialNeedCalibrationSuccess) {
  auto handler = CreateStateHandler();
  json_store_->SetValue(kSameOwner, false);
  json_store_->SetValue(
      kReplacedComponentNames,
      std::vector<std::string>{
          RmadComponent_Name(RMAD_COMPONENT_BATTERY),
          RmadComponent_Name(RMAD_COMPONENT_LID_ACCELEROMETER),
          RmadComponent_Name(RMAD_COMPONENT_BASE_GYROSCOPE)});

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  handler->RunState();
  task_environment_.FastForwardBy(
      ProvisionDeviceStateHandler::kReportStatusInterval);
  EXPECT_GE(status_history_.size(), 1);
  EXPECT_EQ(status_history_.back().status(),
            ProvisionStatus::RMAD_PROVISION_STATUS_COMPLETE);

  auto provision = std::make_unique<ProvisionDeviceState>();
  provision->set_choice(ProvisionDeviceState::RMAD_PROVISION_CHOICE_CONTINUE);
  RmadState state;
  state.set_allocated_provision_device(provision.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_EXPECT_REBOOT);
  task_environment_.FastForwardBy(ProvisionDeviceStateHandler::kRebootDelay);
  EXPECT_EQ(reboot_called_, true);
  EXPECT_EQ(state_case, RmadState::StateCase::kProvisionDevice);

  auto handler_after_reboot = CreateStateHandler();
  EXPECT_EQ(handler_after_reboot->InitializeState(), RMAD_ERROR_OK);
  handler_after_reboot->RunState();
  auto [error_try_boot, state_case_try_boot] =
      handler_after_reboot->TryGetNextStateCaseAtBoot();
  EXPECT_EQ(error_try_boot, RMAD_ERROR_OK);
  EXPECT_EQ(state_case_try_boot, RmadState::StateCase::kSetupCalibration);

  RunHandlerTaskRunner(handler);
}

TEST_F(ProvisionDeviceStateHandlerTest,
       TryGetNextStateCaseAtBoot_KeepDevOpenSuccess) {
  auto handler = CreateStateHandler();
  json_store_->SetValue(kSameOwner, true);
  json_store_->SetValue(
      kReplacedComponentNames,
      std::vector<std::string>{RmadComponent_Name(RMAD_COMPONENT_BATTERY)});
  json_store_->SetValue(kWipeDevice, false);

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  handler->RunState();
  task_environment_.FastForwardBy(
      ProvisionDeviceStateHandler::kReportStatusInterval);
  EXPECT_GE(status_history_.size(), 1);
  EXPECT_EQ(status_history_.back().status(),
            ProvisionStatus::RMAD_PROVISION_STATUS_COMPLETE);

  auto provision = std::make_unique<ProvisionDeviceState>();
  provision->set_choice(ProvisionDeviceState::RMAD_PROVISION_CHOICE_CONTINUE);
  RmadState state;
  state.set_allocated_provision_device(provision.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_EXPECT_REBOOT);
  task_environment_.FastForwardBy(ProvisionDeviceStateHandler::kRebootDelay);
  EXPECT_EQ(reboot_called_, true);
  EXPECT_EQ(state_case, RmadState::StateCase::kProvisionDevice);

  auto handler_after_reboot = CreateStateHandler();
  EXPECT_EQ(handler_after_reboot->InitializeState(), RMAD_ERROR_OK);
  handler_after_reboot->RunState();
  auto [error_try_boot, state_case_try_boot] =
      handler_after_reboot->TryGetNextStateCaseAtBoot();
  EXPECT_EQ(error_try_boot, RMAD_ERROR_OK);
  EXPECT_EQ(state_case_try_boot, RmadState::StateCase::kWpEnablePhysical);

  RunHandlerTaskRunner(handler);
}

TEST_F(ProvisionDeviceStateHandlerTest,
       GetNextStateCase_UnknownDestinationFailedBlocking) {
  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  handler->RunState();
  task_environment_.FastForwardBy(
      ProvisionDeviceStateHandler::kReportStatusInterval);
  EXPECT_GE(status_history_.size(), 1);
  EXPECT_EQ(status_history_.back().status(),
            ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_BLOCKING);
  EXPECT_EQ(status_history_.back().error(),
            ProvisionStatus::RMAD_PROVISION_ERROR_CANNOT_READ);

  auto provision = std::make_unique<ProvisionDeviceState>();
  provision->set_choice(ProvisionDeviceState::RMAD_PROVISION_CHOICE_CONTINUE);
  RmadState state;
  state.set_allocated_provision_device(provision.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_PROVISIONING_FAILED);
  EXPECT_EQ(state_case, RmadState::StateCase::kProvisionDevice);

  RunHandlerTaskRunner(handler);
}

TEST_F(ProvisionDeviceStateHandlerTest, GetNextStateCase_Retry) {
  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  handler->RunState();
  task_environment_.FastForwardBy(
      ProvisionDeviceStateHandler::kReportStatusInterval);
  EXPECT_GE(status_history_.size(), 1);
  EXPECT_EQ(status_history_.back().status(),
            ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_BLOCKING);
  EXPECT_EQ(status_history_.back().error(),
            ProvisionStatus::RMAD_PROVISION_ERROR_CANNOT_READ);

  auto provision = std::make_unique<ProvisionDeviceState>();
  provision->set_choice(ProvisionDeviceState::RMAD_PROVISION_CHOICE_RETRY);
  RmadState state;
  state.set_allocated_provision_device(provision.release());

  json_store_->SetValue(kSameOwner, false);

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_WAIT);
  EXPECT_EQ(state_case, RmadState::StateCase::kProvisionDevice);

  task_environment_.FastForwardBy(
      ProvisionDeviceStateHandler::kReportStatusInterval);
  EXPECT_GE(status_history_.size(), 2);
  EXPECT_EQ(status_history_.back().status(),
            ProvisionStatus::RMAD_PROVISION_STATUS_COMPLETE);
  EXPECT_EQ(status_history_.back().error(),
            ProvisionStatus::RMAD_PROVISION_ERROR_UNKNOWN);

  RunHandlerTaskRunner(handler);
}

TEST_F(ProvisionDeviceStateHandlerTest,
       GetNextStateCase_SetStableDeviceSecretFailedBlocking) {
  auto handler = CreateStateHandler({.set_stable_dev_secret = false});
  json_store_->SetValue(kSameOwner, false);
  json_store_->SetValue(kWipeDevice, true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  handler->RunState();
  task_environment_.FastForwardBy(
      ProvisionDeviceStateHandler::kReportStatusInterval);
  EXPECT_GE(status_history_.size(), 1);
  EXPECT_EQ(status_history_.back().status(),
            ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_BLOCKING);
  EXPECT_EQ(status_history_.back().error(),
            ProvisionStatus::RMAD_PROVISION_ERROR_INTERNAL);

  RunHandlerTaskRunner(handler);
}

TEST_F(ProvisionDeviceStateHandlerTest,
       GetNextStateCase_GetModelNameFailedBlocking) {
  auto handler = CreateStateHandler({.get_model_name = false});
  json_store_->SetValue(kSameOwner, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  handler->RunState();
  task_environment_.FastForwardBy(
      ProvisionDeviceStateHandler::kReportStatusInterval);
  EXPECT_GE(status_history_.size(), 1);
  EXPECT_EQ(status_history_.back().status(),
            ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_BLOCKING);
  EXPECT_EQ(status_history_.back().error(),
            ProvisionStatus::RMAD_PROVISION_ERROR_CANNOT_READ);

  RunHandlerTaskRunner(handler);
}

TEST_F(ProvisionDeviceStateHandlerTest,
       GetNextStateCase_SsfcNotRequiredSuccess) {
  auto handler = CreateStateHandler({.need_update_ssfc = false});
  json_store_->SetValue(kSameOwner, false);
  json_store_->SetValue(kWipeDevice, true);
  json_store_->SetValue(
      kReplacedComponentNames,
      std::vector<std::string>{
          RmadComponent_Name(RMAD_COMPONENT_BATTERY),
          RmadComponent_Name(RMAD_COMPONENT_BASE_ACCELEROMETER),
          RmadComponent_Name(RMAD_COMPONENT_LID_ACCELEROMETER),
          RmadComponent_Name(RMAD_COMPONENT_BASE_GYROSCOPE),
          RmadComponent_Name(RMAD_COMPONENT_LID_GYROSCOPE)});

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  handler->RunState();
  task_environment_.FastForwardBy(
      ProvisionDeviceStateHandler::kReportStatusInterval);
  EXPECT_GE(status_history_.size(), 1);
  EXPECT_EQ(status_history_.back().status(),
            ProvisionStatus::RMAD_PROVISION_STATUS_COMPLETE);

  auto provision = std::make_unique<ProvisionDeviceState>();
  provision->set_choice(ProvisionDeviceState::RMAD_PROVISION_CHOICE_CONTINUE);
  RmadState state;
  state.set_allocated_provision_device(provision.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_EXPECT_REBOOT);
  task_environment_.FastForwardBy(ProvisionDeviceStateHandler::kRebootDelay);
  EXPECT_EQ(reboot_called_, true);
  EXPECT_EQ(state_case, RmadState::StateCase::kProvisionDevice);

  auto handler_after_reboot = CreateStateHandler();
  EXPECT_EQ(handler_after_reboot->InitializeState(), RMAD_ERROR_OK);
  handler_after_reboot->RunState();
  auto [error_try_boot, state_case_try_boot] =
      handler_after_reboot->TryGetNextStateCaseAtBoot();
  EXPECT_EQ(error_try_boot, RMAD_ERROR_OK);
  EXPECT_EQ(state_case_try_boot, RmadState::StateCase::kSetupCalibration);

  RunHandlerTaskRunner(handler);
}

TEST_F(ProvisionDeviceStateHandlerTest,
       GetNextStateCase_GetSsfcFailedBlocking) {
  auto handler = CreateStateHandler({.get_ssfc = false});
  json_store_->SetValue(kSameOwner, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  handler->RunState();
  task_environment_.FastForwardBy(
      ProvisionDeviceStateHandler::kReportStatusInterval);
  EXPECT_GE(status_history_.size(), 1);
  EXPECT_EQ(status_history_.back().status(),
            ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_BLOCKING);
  EXPECT_EQ(status_history_.back().error(),
            ProvisionStatus::RMAD_PROVISION_ERROR_CANNOT_READ);
}

TEST_F(ProvisionDeviceStateHandlerTest,
       GetNextStateCase_SetSsfcFailedBlockingCannotWrite) {
  auto handler = CreateStateHandler({.set_ssfc = false});
  json_store_->SetValue(kSameOwner, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  handler->RunState();
  task_environment_.FastForwardBy(
      ProvisionDeviceStateHandler::kReportStatusInterval);
  EXPECT_GE(status_history_.size(), 1);
  EXPECT_EQ(status_history_.back().status(),
            ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_BLOCKING);
  EXPECT_EQ(status_history_.back().error(),
            ProvisionStatus::RMAD_PROVISION_ERROR_CANNOT_WRITE);

  RunHandlerTaskRunner(handler);
}

TEST_F(ProvisionDeviceStateHandlerTest,
       GetNextStateCase_SetSsfcFailedBlockingWpEnabled) {
  auto handler = CreateStateHandler({.set_ssfc = false, .hwwp_enabled = true});
  json_store_->SetValue(kSameOwner, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  handler->RunState();
  task_environment_.FastForwardBy(
      ProvisionDeviceStateHandler::kReportStatusInterval);
  EXPECT_GE(status_history_.size(), 1);
  EXPECT_EQ(status_history_.back().status(),
            ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_BLOCKING);
  EXPECT_EQ(status_history_.back().error(),
            ProvisionStatus::RMAD_PROVISION_ERROR_WP_ENABLED);

  RunHandlerTaskRunner(handler);
}

TEST_F(ProvisionDeviceStateHandlerTest, GetNextStateCase_SetSsfcBypassed) {
  auto handler = CreateStateHandler({.set_ssfc = false});
  json_store_->SetValue(kSameOwner, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // Bypass setting SSFC.
  EXPECT_TRUE(brillo::TouchFile(GetTempDirPath().Append(kTestDirPath)));

  handler->RunState();
  task_environment_.FastForwardBy(
      ProvisionDeviceStateHandler::kReportStatusInterval);
  EXPECT_GE(status_history_.size(), 1);
  EXPECT_EQ(status_history_.back().status(),
            ProvisionStatus::RMAD_PROVISION_STATUS_COMPLETE);

  RunHandlerTaskRunner(handler);
}

TEST_F(ProvisionDeviceStateHandlerTest,
       GetNextStateCase_VpdFlushFailedBlocking) {
  auto handler = CreateStateHandler({.flush_vpd = false});
  json_store_->SetValue(kSameOwner, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  handler->RunState();
  task_environment_.FastForwardBy(
      ProvisionDeviceStateHandler::kReportStatusInterval);
  EXPECT_GE(status_history_.size(), 1);
  EXPECT_EQ(status_history_.back().status(),
            ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_BLOCKING);
  EXPECT_EQ(status_history_.back().error(),
            ProvisionStatus::RMAD_PROVISION_ERROR_CANNOT_WRITE);

  RunHandlerTaskRunner(handler);
}

TEST_F(ProvisionDeviceStateHandlerTest,
       GetNextStateCase_ResetGbbFlagsFailedBlocking) {
  auto handler = CreateStateHandler({.reset_gbb_success = false});
  json_store_->SetValue(kSameOwner, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  handler->RunState();
  task_environment_.FastForwardBy(
      ProvisionDeviceStateHandler::kReportStatusInterval);
  EXPECT_GE(status_history_.size(), 1);
  EXPECT_EQ(status_history_.back().status(),
            ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_BLOCKING);
  EXPECT_EQ(status_history_.back().error(),
            ProvisionStatus::RMAD_PROVISION_ERROR_GBB);

  RunHandlerTaskRunner(handler);
}

TEST_F(ProvisionDeviceStateHandlerTest,
       GetNextStateCase_CannotReadBoardIdBlocking) {
  auto handler = CreateStateHandler({.board_id_read_success = false});
  json_store_->SetValue(kSameOwner, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  handler->RunState();
  task_environment_.FastForwardBy(
      ProvisionDeviceStateHandler::kReportStatusInterval);
  EXPECT_GE(status_history_.size(), 1);
  EXPECT_EQ(status_history_.back().status(),
            ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_BLOCKING);
  EXPECT_EQ(status_history_.back().error(),
            ProvisionStatus::RMAD_PROVISION_ERROR_CR50);

  RunHandlerTaskRunner(handler);
}

TEST_F(ProvisionDeviceStateHandlerTest,
       GetNextStateCase_InvalidBoardIdTypeBlocking) {
  auto handler = CreateStateHandler({.board_id_type = kInvalidBoardIdType});
  json_store_->SetValue(kSameOwner, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  handler->RunState();
  task_environment_.FastForwardBy(
      ProvisionDeviceStateHandler::kReportStatusInterval);
  EXPECT_GE(status_history_.size(), 1);
  EXPECT_EQ(status_history_.back().status(),
            ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_BLOCKING);
  EXPECT_EQ(status_history_.back().error(),
            ProvisionStatus::RMAD_PROVISION_ERROR_CR50);

  RunHandlerTaskRunner(handler);
}

TEST_F(ProvisionDeviceStateHandlerTest,
       GetNextStateCase_InvalidBoardIdTypeBlocking_Bypass) {
  auto handler = CreateStateHandler({.board_id_type = kInvalidBoardIdType});
  json_store_->SetValue(kSameOwner, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // Bypass board ID check.
  EXPECT_TRUE(brillo::TouchFile(GetTempDirPath().Append(kTestDirPath)));

  handler->RunState();
  task_environment_.FastForwardBy(
      ProvisionDeviceStateHandler::kReportStatusInterval);
  EXPECT_GE(status_history_.size(), 1);
  EXPECT_EQ(status_history_.back().status(),
            ProvisionStatus::RMAD_PROVISION_STATUS_COMPLETE);

  RunHandlerTaskRunner(handler);
}

TEST_F(ProvisionDeviceStateHandlerTest,
       GetNextStateCase_EmptyBoardIdType_NotCustomLabel_Success) {
  auto handler = CreateStateHandler({.board_id_type = kEmptyBoardIdType});
  json_store_->SetValue(kSameOwner, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  handler->RunState();
  task_environment_.FastForwardBy(
      ProvisionDeviceStateHandler::kReportStatusInterval);
  EXPECT_GE(status_history_.size(), 1);
  EXPECT_EQ(status_history_.back().status(),
            ProvisionStatus::RMAD_PROVISION_STATUS_COMPLETE);

  RunHandlerTaskRunner(handler);
}

TEST_F(ProvisionDeviceStateHandlerTest,
       GetNextStateCase_EmptyBoardIdType_CustomLabel_Success) {
  auto handler =
      CreateStateHandler({.board_id_type = kEmptyBoardIdType,
                          .board_id_flags = kCustomLabelPvtBoardIdFlags});
  json_store_->SetValue(kSameOwner, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  handler->RunState();
  task_environment_.FastForwardBy(
      ProvisionDeviceStateHandler::kReportStatusInterval);
  EXPECT_GE(status_history_.size(), 1);
  EXPECT_EQ(status_history_.back().status(),
            ProvisionStatus::RMAD_PROVISION_STATUS_COMPLETE);

  RunHandlerTaskRunner(handler);
}

TEST_F(ProvisionDeviceStateHandlerTest, GetNextStateCase_MissingState) {
  auto handler = CreateStateHandler();
  json_store_->SetValue(kSameOwner, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  handler->RunState();

  // No WelcomeScreenState.
  RmadState state;

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kProvisionDevice);

  RunHandlerTaskRunner(handler);
}

TEST_F(ProvisionDeviceStateHandlerTest, GetNextStateCase_MissingArgs) {
  auto handler = CreateStateHandler();
  json_store_->SetValue(kSameOwner, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  handler->RunState();

  auto provision = std::make_unique<ProvisionDeviceState>();
  provision->set_choice(ProvisionDeviceState::RMAD_PROVISION_CHOICE_UNKNOWN);
  RmadState state;
  state.set_allocated_provision_device(provision.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_MISSING);
  EXPECT_EQ(state_case, RmadState::StateCase::kProvisionDevice);

  RunHandlerTaskRunner(handler);
}

}  // namespace rmad
