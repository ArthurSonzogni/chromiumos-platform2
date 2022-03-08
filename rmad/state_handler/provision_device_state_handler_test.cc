// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/provision_device_state_handler.h"

#include <base/memory/scoped_refptr.h>
#include <base/test/task_environment.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "rmad/constants.h"
#include "rmad/proto_bindings/rmad.pb.h"
#include "rmad/state_handler/state_handler_test_common.h"
#include "rmad/system/mock_power_manager_client.h"
#include "rmad/utils/json_store.h"
#include "rmad/utils/mock_cbi_utils.h"
#include "rmad/utils/mock_cros_config_utils.h"
#include "rmad/utils/mock_crossystem_utils.h"
#include "rmad/utils/mock_iio_sensor_probe_utils.h"
#include "rmad/utils/mock_ssfc_utils.h"
#include "rmad/utils/mock_vpd_utils.h"

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
constexpr uint32_t kTestSSFC = 0x1234;

// crossystem HWWP property name.
constexpr char kHwwpProperty[] = "wpsw_cur";

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
      bool get_model_name = true,
      bool get_ssfc = true,
      bool need_update_ssfc = true,
      bool set_ssfc = true,
      bool set_stable_dev_secret = true,
      bool flush_vpd = true,
      bool hw_wp_enabled = false,
      const std::set<RmadComponent>& probed_components = {
          RMAD_COMPONENT_BASE_ACCELEROMETER, RMAD_COMPONENT_LID_ACCELEROMETER,
          RMAD_COMPONENT_BASE_GYROSCOPE, RMAD_COMPONENT_LID_GYROSCOPE}) {
    // Expect signal is always sent.
    ON_CALL(signal_sender_, SendProvisionProgressSignal(_))
        .WillByDefault(WithArg<0>(
            Invoke(this, &ProvisionDeviceStateHandlerTest::QueueStatus)));

    // Mock |PowerManagerClient|.
    reboot_called_ = false;
    auto mock_power_manager_client =
        std::make_unique<NiceMock<MockPowerManagerClient>>();
    ON_CALL(*mock_power_manager_client, Restart())
        .WillByDefault(DoAll(Assign(&reboot_called_, true), Return(true)));

    auto cbi_utils = std::make_unique<NiceMock<MockCbiUtils>>();

    auto cros_config_utils = std::make_unique<NiceMock<MockCrosConfigUtils>>();
    if (get_model_name) {
      ON_CALL(*cros_config_utils, GetModelName(_))
          .WillByDefault(DoAll(SetArgPointee<0>(std::string(kTestModelName)),
                               Return(true)));
    } else {
      ON_CALL(*cros_config_utils, GetModelName(_)).WillByDefault(Return(false));
    }

    auto crossystem_utils = std::make_unique<NiceMock<MockCrosSystemUtils>>();
    ON_CALL(*crossystem_utils, GetInt(Eq(kHwwpProperty), _))
        .WillByDefault(DoAll(SetArgPointee<1>(hw_wp_enabled), Return(true)));

    auto iio_sensor_probe_utils =
        std::make_unique<NiceMock<MockIioSensorProbeUtils>>();
    ON_CALL(*iio_sensor_probe_utils, Probe())
        .WillByDefault(Return(probed_components));

    auto ssfc_utils = std::make_unique<NiceMock<MockSsfcUtils>>();

    if (need_update_ssfc) {
      ON_CALL(*ssfc_utils, GetSSFC(_, _, _))
          .WillByDefault(DoAll(SetArgPointee<1>(true),
                               SetArgPointee<2>(kTestSSFC), Return(get_ssfc)));
      ON_CALL(*cbi_utils, SetSSFC(_)).WillByDefault(Return(set_ssfc));
    } else {
      ON_CALL(*ssfc_utils, GetSSFC(_, _, _))
          .WillByDefault(DoAll(SetArgPointee<1>(false), Return(true)));
    }

    auto vpd_utils = std::make_unique<NiceMock<MockVpdUtils>>();
    ON_CALL(*vpd_utils, SetStableDeviceSecret(_))
        .WillByDefault(Return(set_stable_dev_secret));
    ON_CALL(*vpd_utils, FlushOutRoVpdCache()).WillByDefault(Return(flush_vpd));

    // Fake |HardwareVerifierUtils|.
    auto handler = base::MakeRefCounted<ProvisionDeviceStateHandler>(
        json_store_, std::move(mock_power_manager_client), std::move(cbi_utils),
        std::move(cros_config_utils), std::move(crossystem_utils),
        std::move(iio_sensor_probe_utils), std::move(ssfc_utils),
        std::move(vpd_utils));
    auto callback =
        base::BindRepeating(&SignalSender::SendProvisionProgressSignal,
                            base::Unretained(&signal_sender_));
    handler->RegisterSignalSender(callback);
    return handler;
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
  RunHandlerTaskRunner(handler);
}

TEST_F(ProvisionDeviceStateHandlerTest, Clenaup_Success) {
  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  handler->CleanUpState();
  RunHandlerTaskRunner(handler);
}

TEST_F(ProvisionDeviceStateHandlerTest, GetNextStateCase_Success) {
  auto handler = CreateStateHandler();
  json_store_->SetValue(kSameOwner, false);
  json_store_->SetValue(kWipeDevice, true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
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
  auto [error_try_boot, state_case_try_boot] =
      handler_after_reboot->TryGetNextStateCaseAtBoot();
  EXPECT_EQ(error_try_boot, RMAD_ERROR_OK);
  EXPECT_EQ(state_case_try_boot, RmadState::StateCase::kFinalize);

  RunHandlerTaskRunner(handler);
}

TEST_F(ProvisionDeviceStateHandlerTest,
       TryGetNextStateCaseAtBoot_BaseAccNotProbedFailedBlocking) {
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
      true, true, true, true, true, true, false,
      {RMAD_COMPONENT_LID_ACCELEROMETER, RMAD_COMPONENT_BASE_GYROSCOPE,
       RMAD_COMPONENT_LID_GYROSCOPE});
  EXPECT_EQ(handler_after_reboot->InitializeState(), RMAD_ERROR_OK);
  task_environment_.FastForwardBy(
      ProvisionDeviceStateHandler::kReportStatusInterval);
  EXPECT_GE(status_history_.size(), 2);
  EXPECT_EQ(status_history_.back().status(),
            ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_BLOCKING);
  EXPECT_EQ(status_history_.back().error(),
            ProvisionStatus::RMAD_PROVISION_ERROR_MISSING_BASE_ACCELEROMETER);
  auto [error_try_boot, state_case_try_boot] =
      handler_after_reboot->TryGetNextStateCaseAtBoot();
  EXPECT_EQ(error_try_boot, RMAD_ERROR_TRANSITION_FAILED);
  EXPECT_EQ(state_case_try_boot, RmadState::StateCase::kProvisionDevice);

  RunHandlerTaskRunner(handler);
}

TEST_F(ProvisionDeviceStateHandlerTest,
       TryGetNextStateCaseAtBoot_LidAccNotProbedFailedBlocking) {
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
      true, true, true, true, true, true, false,
      {RMAD_COMPONENT_BASE_ACCELEROMETER, RMAD_COMPONENT_BASE_GYROSCOPE,
       RMAD_COMPONENT_LID_GYROSCOPE});
  EXPECT_EQ(handler_after_reboot->InitializeState(), RMAD_ERROR_OK);
  task_environment_.FastForwardBy(
      ProvisionDeviceStateHandler::kReportStatusInterval);
  EXPECT_GE(status_history_.size(), 2);
  EXPECT_EQ(status_history_.back().status(),
            ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_BLOCKING);
  EXPECT_EQ(status_history_.back().error(),
            ProvisionStatus::RMAD_PROVISION_ERROR_MISSING_LID_ACCELEROMETER);
  auto [error_try_boot, state_case_try_boot] =
      handler_after_reboot->TryGetNextStateCaseAtBoot();
  EXPECT_EQ(error_try_boot, RMAD_ERROR_TRANSITION_FAILED);
  EXPECT_EQ(state_case_try_boot, RmadState::StateCase::kProvisionDevice);

  RunHandlerTaskRunner(handler);
}

TEST_F(ProvisionDeviceStateHandlerTest,
       TryGetNextStateCaseAtBoot_BaseGyroNotProbedFailedBlocking) {
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
      true, true, true, true, true, true, false,
      {RMAD_COMPONENT_BASE_ACCELEROMETER, RMAD_COMPONENT_LID_ACCELEROMETER,
       RMAD_COMPONENT_LID_GYROSCOPE});
  EXPECT_EQ(handler_after_reboot->InitializeState(), RMAD_ERROR_OK);
  task_environment_.FastForwardBy(
      ProvisionDeviceStateHandler::kReportStatusInterval);
  EXPECT_GE(status_history_.size(), 2);
  EXPECT_EQ(status_history_.back().status(),
            ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_BLOCKING);
  EXPECT_EQ(status_history_.back().error(),
            ProvisionStatus::RMAD_PROVISION_ERROR_MISSING_BASE_GYROSCOPE);
  auto [error_try_boot, state_case_try_boot] =
      handler_after_reboot->TryGetNextStateCaseAtBoot();
  EXPECT_EQ(error_try_boot, RMAD_ERROR_TRANSITION_FAILED);
  EXPECT_EQ(state_case_try_boot, RmadState::StateCase::kProvisionDevice);

  RunHandlerTaskRunner(handler);
}

TEST_F(ProvisionDeviceStateHandlerTest,
       TryGetNextStateCaseAtBoot_LidGyroNotProbedFailedBlocking) {
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
      true, true, true, true, true, true, false,
      {RMAD_COMPONENT_BASE_ACCELEROMETER, RMAD_COMPONENT_LID_ACCELEROMETER,
       RMAD_COMPONENT_BASE_GYROSCOPE});
  EXPECT_EQ(handler_after_reboot->InitializeState(), RMAD_ERROR_OK);
  task_environment_.FastForwardBy(
      ProvisionDeviceStateHandler::kReportStatusInterval);
  EXPECT_GE(status_history_.size(), 2);
  EXPECT_EQ(status_history_.back().status(),
            ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_BLOCKING);
  EXPECT_EQ(status_history_.back().error(),
            ProvisionStatus::RMAD_PROVISION_ERROR_MISSING_LID_GYROSCOPE);
  auto [error_try_boot, state_case_try_boot] =
      handler_after_reboot->TryGetNextStateCaseAtBoot();
  EXPECT_EQ(error_try_boot, RMAD_ERROR_TRANSITION_FAILED);
  EXPECT_EQ(state_case_try_boot, RmadState::StateCase::kProvisionDevice);

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
  auto handler = CreateStateHandler(true, true, true, true, false, true);
  json_store_->SetValue(kSameOwner, false);
  json_store_->SetValue(kWipeDevice, true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
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
  auto handler = CreateStateHandler(false, true, true, true, true, true);
  json_store_->SetValue(kSameOwner, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
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
  auto handler = CreateStateHandler(true, true, false, true, true, true);
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
  auto [error_try_boot, state_case_try_boot] =
      handler_after_reboot->TryGetNextStateCaseAtBoot();
  EXPECT_EQ(error_try_boot, RMAD_ERROR_OK);
  EXPECT_EQ(state_case_try_boot, RmadState::StateCase::kSetupCalibration);

  RunHandlerTaskRunner(handler);
}

TEST_F(ProvisionDeviceStateHandlerTest,
       GetNextStateCase_GetSsfcFailedBlocking) {
  auto handler = CreateStateHandler(true, false, true, true, true, true);
  json_store_->SetValue(kSameOwner, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
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
  auto handler = CreateStateHandler(true, true, true, false, true, true);
  json_store_->SetValue(kSameOwner, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
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
  auto handler = CreateStateHandler(true, true, true, false, true, true, true);
  json_store_->SetValue(kSameOwner, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  task_environment_.FastForwardBy(
      ProvisionDeviceStateHandler::kReportStatusInterval);
  EXPECT_GE(status_history_.size(), 1);
  EXPECT_EQ(status_history_.back().status(),
            ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_BLOCKING);
  EXPECT_EQ(status_history_.back().error(),
            ProvisionStatus::RMAD_PROVISION_ERROR_WP_ENABLED);

  RunHandlerTaskRunner(handler);
}

TEST_F(ProvisionDeviceStateHandlerTest,
       GetNextStateCase_VpdFlushFailedBlocking) {
  auto handler = CreateStateHandler(true, true, true, true, true, false);
  json_store_->SetValue(kSameOwner, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  task_environment_.FastForwardBy(
      ProvisionDeviceStateHandler::kReportStatusInterval);
  EXPECT_GE(status_history_.size(), 1);
  EXPECT_EQ(status_history_.back().status(),
            ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_BLOCKING);
  EXPECT_EQ(status_history_.back().error(),
            ProvisionStatus::RMAD_PROVISION_ERROR_CANNOT_WRITE);

  RunHandlerTaskRunner(handler);
}

TEST_F(ProvisionDeviceStateHandlerTest, GetNextStateCase_MissingState) {
  auto handler = CreateStateHandler();
  json_store_->SetValue(kSameOwner, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

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
