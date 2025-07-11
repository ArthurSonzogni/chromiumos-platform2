// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/write_protect_disable_physical_state_handler.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_util.h>
#include <base/memory/scoped_refptr.h>
#include <base/test/bind.h>
#include <base/test/task_environment.h>
#include <brillo/file_utils.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rmad/constants.h"
#include "rmad/metrics/metrics_utils.h"
#include "rmad/state_handler/state_handler_test_common.h"
#include "rmad/system/mock_power_manager_client.h"
#include "rmad/utils/mock_crossystem_utils.h"
#include "rmad/utils/mock_gsc_utils.h"
#include "rmad/utils/mock_write_protect_utils.h"

using testing::_;
using testing::Assign;
using testing::DoAll;
using testing::Eq;
using testing::InSequence;
using testing::IsFalse;
using testing::NiceMock;
using testing::Return;
using testing::SetArgPointee;
using testing::StrictMock;

namespace rmad {

class WriteProtectDisablePhysicalStateHandlerTest : public StateHandlerTest {
 public:
  // Helper class to mock the callback function to send signal.
  class SignalSender {
   public:
    MOCK_METHOD(void, SendHardwareWriteProtectSignal, (bool), (const));
  };

  struct StateHandlerArgs {
    std::vector<bool> wp_status_list = {};
    std::vector<bool> chassis_open_list = {};
    bool factory_mode_enabled = false;
    bool enable_factory_mode_succeeded = true;
    bool is_cros_debug = false;
    bool* factory_mode_toggled = nullptr;
    bool* powerwash_requested = nullptr;
    bool* reboot_called_ = nullptr;
  };

  scoped_refptr<WriteProtectDisablePhysicalStateHandler> CreateStateHandler(
      const StateHandlerArgs& args) {
    // Mock |CrosSystemUtils|.
    auto mock_crossystem_utils =
        std::make_unique<NiceMock<MockCrosSystemUtils>>();
    ON_CALL(*mock_crossystem_utils,
            GetInt(Eq(CrosSystemUtils::kCrosDebugProperty), _))
        .WillByDefault(
            DoAll(SetArgPointee<1>(args.is_cros_debug ? 1 : 0), Return(true)));

    // Mock |WriteProtectUtils|.
    auto mock_write_protect_utils =
        std::make_unique<StrictMock<MockWriteProtectUtils>>();
    {
      InSequence seq;
      for (bool enabled : args.wp_status_list) {
        EXPECT_CALL(*mock_write_protect_utils,
                    GetHardwareWriteProtectionStatus())
            .WillOnce(Return(enabled));
      }
    }

    // Mock |GscUtils|.
    auto mock_gsc_utils = std::make_unique<NiceMock<MockGscUtils>>();
    {
      InSequence seq;
      for (bool opened : args.chassis_open_list) {
        EXPECT_CALL(*mock_gsc_utils, GetChassisOpenStatus())
            .WillOnce(Return(opened));
      }
    }
    ON_CALL(*mock_gsc_utils, IsFactoryModeEnabled())
        .WillByDefault(Return(args.factory_mode_enabled));
    if (args.factory_mode_toggled) {
      ON_CALL(*mock_gsc_utils, EnableFactoryMode())
          .WillByDefault(DoAll(Assign(args.factory_mode_toggled, true),
                               Return(args.enable_factory_mode_succeeded)));
    }

    // Mock |PowerManagerClient|.
    reboot_called_ = false;
    auto mock_power_manager_client =
        std::make_unique<NiceMock<MockPowerManagerClient>>();
    ON_CALL(*mock_power_manager_client, Restart())
        .WillByDefault(DoAll(Assign(&reboot_called_, true), Return(true)));

    // Register signal callback.
    daemon_callback_->SetWriteProtectSignalCallback(
        base::BindRepeating(&SignalSender::SendHardwareWriteProtectSignal,
                            base::Unretained(&signal_sender_)));
    // Register request powerwash callback.
    daemon_callback_->SetExecuteRequestRmaPowerwashCallback(base::BindRepeating(
        &WriteProtectDisablePhysicalStateHandlerTest::RequestRmaPowerwash,
        base::Unretained(this), args.powerwash_requested));
    // Register preseed rma state callback.
    daemon_callback_->SetExecutePreseedRmaStateCallback(
        base::BindLambdaForTesting([](base::OnceCallback<void(bool)> callback) {
          std::move(callback).Run(true);
        }));

    return base::MakeRefCounted<WriteProtectDisablePhysicalStateHandler>(
        json_store_, daemon_callback_, GetTempDirPath(),
        std::move(mock_gsc_utils), std::move(mock_crossystem_utils),
        std::move(mock_write_protect_utils),
        std::move(mock_power_manager_client));
  }

  void RequestRmaPowerwash(bool* powerwash_requested,
                           base::OnceCallback<void(bool)> callback) {
    if (powerwash_requested) {
      *powerwash_requested = true;
    }
    std::move(callback).Run(true);
  }

 protected:
  StrictMock<SignalSender> signal_sender_;
  bool factory_mode_enabled_;
  bool reboot_called_;

  // Variables for TaskRunner.
  base::test::SingleThreadTaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
};

TEST_F(WriteProtectDisablePhysicalStateHandlerTest, InitializeState_Success) {
  // Set up environment to wipe device.
  EXPECT_TRUE(json_store_->SetValue(kWipeDevice, true));

  auto handler = CreateStateHandler({});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  handler->RunState();
  EXPECT_FALSE(handler->GetState().wp_disable_physical().keep_device_open());
}

TEST_F(WriteProtectDisablePhysicalStateHandlerTest, InitializeState_Failed) {
  // No kWipeDevice set in |json_store_|.
  auto handler = CreateStateHandler({});
  EXPECT_EQ(handler->InitializeState(),
            RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED);
}

TEST_F(WriteProtectDisablePhysicalStateHandlerTest,
       TryGetNextStateCaseAtBoot_Succeeded_FactoryModeEnabled) {
  // Set up environment for wiping the device.
  EXPECT_TRUE(json_store_->SetValue(kWipeDevice, true));

  auto handler = CreateStateHandler(
      {.wp_status_list = {false}, .factory_mode_enabled = true});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto [error, state_case] = handler->TryGetNextStateCaseAtBoot();
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kWpDisableComplete);

  // Check |json_store_|.
  std::string wp_disable_method_name;
  WpDisableMethod wp_disable_method;
  EXPECT_TRUE(json_store_->GetValue(kWpDisableMethod, &wp_disable_method_name));
  EXPECT_TRUE(
      WpDisableMethod_Parse(wp_disable_method_name, &wp_disable_method));
  EXPECT_EQ(wp_disable_method, RMAD_WP_DISABLE_METHOD_PHYSICAL_ASSEMBLE_DEVICE);

  // Check if the metrics value set correctly.
  EXPECT_TRUE(MetricsUtils::GetMetricsValue(
      json_store_, kMetricsWpDisableMethod, &wp_disable_method_name));
  EXPECT_TRUE(
      WpDisableMethod_Parse(wp_disable_method_name, &wp_disable_method));
  EXPECT_EQ(wp_disable_method, RMAD_WP_DISABLE_METHOD_PHYSICAL_ASSEMBLE_DEVICE);
}

TEST_F(WriteProtectDisablePhysicalStateHandlerTest,
       TryGetNextStateCaseAtBoot_Succeeded_KeepDeviceOpen) {
  // Set up environment for not wiping the device.
  EXPECT_TRUE(json_store_->SetValue(kWipeDevice, false));

  auto handler = CreateStateHandler(
      {.wp_status_list = {false}, .factory_mode_enabled = false});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto [error, state_case] = handler->TryGetNextStateCaseAtBoot();
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kWpDisableComplete);

  // Check |json_store_|.
  std::string wp_disable_method_name;
  WpDisableMethod wp_disable_method;
  EXPECT_TRUE(json_store_->GetValue(kWpDisableMethod, &wp_disable_method_name));
  EXPECT_TRUE(
      WpDisableMethod_Parse(wp_disable_method_name, &wp_disable_method));
  EXPECT_EQ(wp_disable_method,
            RMAD_WP_DISABLE_METHOD_PHYSICAL_KEEP_DEVICE_OPEN);

  // Check if the metrics value set correctly.
  EXPECT_TRUE(MetricsUtils::GetMetricsValue(
      json_store_, kMetricsWpDisableMethod, &wp_disable_method_name));
  EXPECT_TRUE(
      WpDisableMethod_Parse(wp_disable_method_name, &wp_disable_method));
  EXPECT_EQ(wp_disable_method,
            RMAD_WP_DISABLE_METHOD_PHYSICAL_KEEP_DEVICE_OPEN);
}

TEST_F(WriteProtectDisablePhysicalStateHandlerTest,
       TryGetNextStateCaseAtBoot_Failed) {
  // Set up environment for not wiping the device.
  EXPECT_TRUE(json_store_->SetValue(kWipeDevice, false));

  // WP is still enabled.
  auto handler = CreateStateHandler({.wp_status_list = {true},
                                     .chassis_open_list = {false},
                                     .factory_mode_enabled = true});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto [error, state_case] = handler->TryGetNextStateCaseAtBoot();
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kWpDisablePhysical);
}

TEST_F(WriteProtectDisablePhysicalStateHandlerTest,
       GetNextStateCase_EnableFactoryModeSuccess) {
  // Set up environment for wiping the device and the device has not rebooted
  // yet.
  EXPECT_TRUE(json_store_->SetValue(kWipeDevice, true));

  // Factory mode is disabled so we should enable it and do reboot.
  bool factory_mode_toggled = false, powerwash_requested = false;
  auto handler =
      CreateStateHandler({.wp_status_list = {true, true, false},
                          .chassis_open_list = {false, false},
                          .factory_mode_enabled = false,
                          .factory_mode_toggled = &factory_mode_toggled,
                          .powerwash_requested = &powerwash_requested});

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  handler->RunState();
  EXPECT_FALSE(handler->GetState().wp_disable_physical().keep_device_open());

  RmadState state;
  state.set_allocated_wp_disable_physical(new WriteProtectDisablePhysicalState);

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_WAIT);
  EXPECT_EQ(state_case, RmadState::StateCase::kWpDisablePhysical);

  bool signal_sent = false;
  EXPECT_CALL(signal_sender_, SendHardwareWriteProtectSignal(IsFalse()))
      .WillOnce(Assign(&signal_sent, true));

  EXPECT_FALSE(factory_mode_toggled);
  EXPECT_FALSE(signal_sent);
  EXPECT_FALSE(powerwash_requested);
  EXPECT_FALSE(reboot_called_);
  // First call to |mock_crossystem_utils_| during polling, get 1.
  task_environment_.FastForwardBy(
      WriteProtectDisablePhysicalStateHandler::kPollInterval);
  EXPECT_FALSE(factory_mode_toggled);
  EXPECT_FALSE(signal_sent);
  EXPECT_FALSE(powerwash_requested);
  EXPECT_FALSE(reboot_called_);
  // Second call to |mock_crossystem_utils_| during polling, get 1.
  task_environment_.FastForwardBy(
      WriteProtectDisablePhysicalStateHandler::kPollInterval);
  EXPECT_FALSE(factory_mode_toggled);
  EXPECT_FALSE(signal_sent);
  EXPECT_FALSE(powerwash_requested);
  EXPECT_FALSE(reboot_called_);
  // Third call to |mock_crossystem_utils_| during polling, get 0.
  // Try to enable factory mode and send the signal.
  task_environment_.FastForwardBy(
      WriteProtectDisablePhysicalStateHandler::kPollInterval);
  EXPECT_TRUE(factory_mode_toggled);
  EXPECT_TRUE(signal_sent);
  EXPECT_FALSE(powerwash_requested);
  EXPECT_FALSE(reboot_called_);
  // Request powerwash and reboot after a delay.
  task_environment_.FastForwardBy(
      WriteProtectDisablePhysicalStateHandler::kRebootDelay);
  EXPECT_TRUE(powerwash_requested);
  EXPECT_TRUE(reboot_called_);
}

TEST_F(WriteProtectDisablePhysicalStateHandlerTest,
       GetNextStateCase_EnableFactoryModeSuccess_ChassisOpen) {
  // After b/257255419 HWWP on Ti50 devices will by default not follow
  // CHASSIS_OPEN, so we check CHASSIS_OPEN as one of the conditions to enter
  // factory mode.

  // Set up environment for wiping the device and the device has not rebooted
  // yet.
  EXPECT_TRUE(json_store_->SetValue(kWipeDevice, true));

  // Factory mode is disabled so we should enable it and do reboot.
  bool factory_mode_toggled = false, powerwash_requested = false;
  auto handler =
      CreateStateHandler({.wp_status_list = {true, true},
                          .chassis_open_list = {false, true},
                          .factory_mode_enabled = false,
                          .factory_mode_toggled = &factory_mode_toggled,
                          .powerwash_requested = &powerwash_requested});

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  handler->RunState();
  EXPECT_FALSE(handler->GetState().wp_disable_physical().keep_device_open());

  RmadState state;
  state.set_allocated_wp_disable_physical(new WriteProtectDisablePhysicalState);

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_WAIT);
  EXPECT_EQ(state_case, RmadState::StateCase::kWpDisablePhysical);

  bool signal_sent = false;
  EXPECT_CALL(signal_sender_, SendHardwareWriteProtectSignal(IsFalse()))
      .WillOnce(Assign(&signal_sent, true));

  EXPECT_FALSE(factory_mode_toggled);
  EXPECT_FALSE(signal_sent);
  EXPECT_FALSE(powerwash_requested);
  EXPECT_FALSE(reboot_called_);

  // First call to |mock_crossystem_utils_| during polling, get
  // HWWP == enabled and CHASSIS_OPEN == false.
  task_environment_.FastForwardBy(
      WriteProtectDisablePhysicalStateHandler::kPollInterval);
  EXPECT_FALSE(factory_mode_toggled);
  EXPECT_FALSE(signal_sent);
  EXPECT_FALSE(powerwash_requested);
  EXPECT_FALSE(reboot_called_);
  // Second call to |mock_crossystem_utils_| during polling, get
  // HWWP == enabled and CHASSIS_OPEN == true.
  // Try to enable factory mode and send the signal.
  task_environment_.FastForwardBy(
      WriteProtectDisablePhysicalStateHandler::kPollInterval);
  EXPECT_TRUE(factory_mode_toggled);
  EXPECT_TRUE(signal_sent);
  EXPECT_FALSE(powerwash_requested);
  EXPECT_FALSE(reboot_called_);
  // Request powerwash and reboot after a delay.
  task_environment_.FastForwardBy(
      WriteProtectDisablePhysicalStateHandler::kRebootDelay);
  EXPECT_TRUE(powerwash_requested);
  EXPECT_TRUE(reboot_called_);
}

TEST_F(WriteProtectDisablePhysicalStateHandlerTest,
       GetNextStateCase_FactoryModeSuccess_PowerwashDisabled_CrosDebug) {
  // Set up environment for wiping the device and the device has not rebooted
  // yet.
  EXPECT_TRUE(json_store_->SetValue(kWipeDevice, true));

  // Powerwash is disabled manually.
  brillo::TouchFile(GetTempDirPath().AppendASCII(kDisablePowerwashFilePath));

  // Factory mode is disabled so we should enable it and do reboot.
  bool factory_mode_toggled = false, powerwash_requested = false;
  auto handler =
      CreateStateHandler({.wp_status_list = {false},
                          .factory_mode_enabled = false,
                          .is_cros_debug = true,
                          .factory_mode_toggled = &factory_mode_toggled,
                          .powerwash_requested = &powerwash_requested});

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  handler->RunState();
  EXPECT_FALSE(handler->GetState().wp_disable_physical().keep_device_open());

  RmadState state;
  state.set_allocated_wp_disable_physical(new WriteProtectDisablePhysicalState);

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_WAIT);
  EXPECT_EQ(state_case, RmadState::StateCase::kWpDisablePhysical);

  bool signal_sent = false;
  EXPECT_CALL(signal_sender_, SendHardwareWriteProtectSignal(IsFalse()))
      .WillOnce(Assign(&signal_sent, true));

  EXPECT_FALSE(factory_mode_toggled);
  EXPECT_FALSE(signal_sent);
  EXPECT_FALSE(powerwash_requested);
  EXPECT_FALSE(reboot_called_);
  // Call to |mock_crossystem_utils_| during polling, get 0.
  // Try to enable factory mode and send the signal.
  task_environment_.FastForwardBy(
      WriteProtectDisablePhysicalStateHandler::kPollInterval);
  EXPECT_TRUE(factory_mode_toggled);
  EXPECT_TRUE(signal_sent);
  EXPECT_FALSE(powerwash_requested);
  EXPECT_FALSE(reboot_called_);
  // Reboot after a delay.
  task_environment_.FastForwardBy(
      WriteProtectDisablePhysicalStateHandler::kRebootDelay);
  EXPECT_FALSE(powerwash_requested);
  EXPECT_TRUE(reboot_called_);
}

TEST_F(WriteProtectDisablePhysicalStateHandlerTest,
       GetNextStateCase_FactoryModeSuccess_PowerwashDisabled_NonCrosDebug) {
  // Set up environment for wiping the device and the device has not rebooted
  // yet.
  EXPECT_TRUE(json_store_->SetValue(kWipeDevice, true));

  // Powerwash is disabled manually.
  brillo::TouchFile(GetTempDirPath().AppendASCII(kDisablePowerwashFilePath));

  // Factory mode is disabled so we should enable it and do reboot. cros_debug
  // is not turned on so we still do a powerwash.
  bool factory_mode_toggled = false, powerwash_requested = false;
  auto handler =
      CreateStateHandler({.wp_status_list = {false},
                          .factory_mode_enabled = false,
                          .is_cros_debug = false,
                          .factory_mode_toggled = &factory_mode_toggled,
                          .powerwash_requested = &powerwash_requested});

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  handler->RunState();
  EXPECT_FALSE(handler->GetState().wp_disable_physical().keep_device_open());

  RmadState state;
  state.set_allocated_wp_disable_physical(new WriteProtectDisablePhysicalState);

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_WAIT);
  EXPECT_EQ(state_case, RmadState::StateCase::kWpDisablePhysical);

  bool signal_sent = false;
  EXPECT_CALL(signal_sender_, SendHardwareWriteProtectSignal(IsFalse()))
      .WillOnce(Assign(&signal_sent, true));

  EXPECT_FALSE(factory_mode_toggled);
  EXPECT_FALSE(signal_sent);
  EXPECT_FALSE(powerwash_requested);
  EXPECT_FALSE(reboot_called_);
  // Call to |mock_crossystem_utils_| during polling, get 0.
  // Try to enable factory mode and send the signal.
  task_environment_.FastForwardBy(
      WriteProtectDisablePhysicalStateHandler::kPollInterval);
  EXPECT_TRUE(factory_mode_toggled);
  EXPECT_TRUE(signal_sent);
  EXPECT_FALSE(powerwash_requested);
  EXPECT_FALSE(reboot_called_);
  // Request powerwash and reboot after a delay.
  task_environment_.FastForwardBy(
      WriteProtectDisablePhysicalStateHandler::kRebootDelay);
  EXPECT_TRUE(powerwash_requested);
  EXPECT_TRUE(reboot_called_);
}

TEST_F(WriteProtectDisablePhysicalStateHandlerTest,
       GetNextStateCase_FactoryModeFailed) {
  // Set up environment for wiping the device and the device has not rebooted
  // yet.
  EXPECT_TRUE(json_store_->SetValue(kWipeDevice, true));

  // Factory mode is disabled so we should enable it, but it fails.
  bool factory_mode_toggled = false, powerwash_requested = false;
  auto handler =
      CreateStateHandler({.wp_status_list = {false},
                          .factory_mode_enabled = false,
                          .enable_factory_mode_succeeded = false,
                          .factory_mode_toggled = &factory_mode_toggled,
                          .powerwash_requested = &powerwash_requested});

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  handler->RunState();
  EXPECT_FALSE(handler->GetState().wp_disable_physical().keep_device_open());

  RmadState state;
  state.set_allocated_wp_disable_physical(new WriteProtectDisablePhysicalState);

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_WAIT);
  EXPECT_EQ(state_case, RmadState::StateCase::kWpDisablePhysical);

  bool signal_sent = false;
  EXPECT_CALL(signal_sender_, SendHardwareWriteProtectSignal(IsFalse()))
      .WillOnce(Assign(&signal_sent, true));

  EXPECT_FALSE(factory_mode_toggled);
  EXPECT_FALSE(signal_sent);
  EXPECT_FALSE(powerwash_requested);
  EXPECT_FALSE(reboot_called_);
  // Call to |mock_crossystem_utils_| during polling, get 0.
  // Try to enable factory mode and send the signal.
  task_environment_.FastForwardBy(
      WriteProtectDisablePhysicalStateHandler::kPollInterval);
  EXPECT_TRUE(factory_mode_toggled);
  EXPECT_TRUE(signal_sent);
  EXPECT_FALSE(powerwash_requested);
  EXPECT_FALSE(reboot_called_);
  // Request powerwash and reboot after a delay.
  task_environment_.FastForwardBy(
      WriteProtectDisablePhysicalStateHandler::kRebootDelay);
  EXPECT_TRUE(powerwash_requested);
  EXPECT_TRUE(reboot_called_);
}

TEST_F(WriteProtectDisablePhysicalStateHandlerTest,
       GetNextStateCase_MissingState) {
  // Set up environment for not wiping the device and the device has not
  // rebooted yet.
  EXPECT_TRUE(json_store_->SetValue(kWipeDevice, false));

  auto handler = CreateStateHandler({});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // No WriteProtectDisablePhysicalState.
  RmadState state;

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kWpDisablePhysical);
}

}  // namespace rmad
