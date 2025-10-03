// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/bluetooth/bluez/bluetooth_power.h"

#include <memory>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/json/json_reader.h>
#include <base/test/gmock_callback_support.h>
#include <base/test/task_environment.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diagnostics/cros_healthd/mojom/executor.mojom.h"
#include "diagnostics/cros_healthd/routines/bluetooth/bluetooth_constants.h"
#include "diagnostics/cros_healthd/routines/routine_test_utils.h"
#include "diagnostics/cros_healthd/system/fake_bluez_event_hub.h"
#include "diagnostics/cros_healthd/system/mock_bluez_controller.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/cros_healthd/system/mock_floss_controller.h"
#include "diagnostics/dbus_bindings/bluetooth_manager/dbus-proxy-mocks.h"
#include "diagnostics/dbus_bindings/bluez/dbus-proxy-mocks.h"

namespace diagnostics::bluez {
namespace {

constexpr int32_t kDefaultHciInterface = 0;

namespace mojom = ::ash::cros_healthd::mojom;

using ::testing::_;
using ::testing::InSequence;
using ::testing::Return;
using ::testing::StrictMock;
using ::testing::WithArg;

class BluezBluetoothPowerRoutineTest : public testing::Test {
 public:
  BluezBluetoothPowerRoutineTest(const BluezBluetoothPowerRoutineTest&) =
      delete;
  BluezBluetoothPowerRoutineTest& operator=(
      const BluezBluetoothPowerRoutineTest&) = delete;

 protected:
  BluezBluetoothPowerRoutineTest() = default;

  void SetUp() override {
    EXPECT_CALL(*mock_context_.mock_floss_controller(), GetManager())
        .WillRepeatedly(Return(&mock_manager_proxy_));
    EXPECT_CALL(mock_manager_proxy_, GetDefaultAdapterAsync(_, _, _))
        .WillRepeatedly(
            base::test::RunOnceCallbackRepeatedly<0>(kDefaultHciInterface));
    SetUpGetAdaptersCall(/*adapters=*/{&mock_adapter_proxy_});
    routine_ = std::make_unique<BluetoothPowerRoutine>(&mock_context_);
  }

  MockExecutor* mock_executor() { return mock_context_.mock_executor(); }

  FakeBluezEventHub* fake_bluez_event_hub() {
    return mock_context_.fake_bluez_event_hub();
  }

  void SetUpGetAdaptersCall(
      const std::vector<org::bluez::Adapter1ProxyInterface*>& adapters) {
    EXPECT_CALL(*mock_context_.mock_bluez_controller(), GetAdapters())
        .WillOnce(Return(adapters));
  }

  void SetUpNullAdapter() {
    SetUpGetAdaptersCall(/*adapters=*/{nullptr});
    routine_ = std::make_unique<BluetoothPowerRoutine>(&mock_context_);
  }

  // Change the powered from |current_powered| to |target_powered|.
  void SetChangePoweredCall(bool current_powered,
                            bool target_powered,
                            bool is_success = true) {
    EXPECT_CALL(mock_adapter_proxy_, powered())
        .WillOnce(Return(current_powered));
    if (current_powered != target_powered) {
      EXPECT_CALL(mock_adapter_proxy_, set_powered(_, _))
          .WillOnce([=, this](bool powered,
                              base::OnceCallback<void(bool)> on_finish) {
            std::move(on_finish).Run(is_success);
            if (is_success) {
              fake_bluez_event_hub()->SendAdapterPropertyChanged(
                  &mock_adapter_proxy_, mock_adapter_proxy_.PoweredName());
            }
          });
    }
  }

  // Setup the powered status after changing in HCI level and D-Bus level.
  void SetVerifyPoweredCall(bool hci_result_powered, bool dbus_result_powered) {
    auto result = mojom::ExecutedProcessResult::New();
    result->return_code = EXIT_SUCCESS;
    if (hci_result_powered) {
      result->out = "UP RUNNING\n";
    } else {
      result->out = "DOWN\n";
    }
    EXPECT_CALL(*mock_executor(), GetHciDeviceConfig(kDefaultHciInterface, _))
        .WillOnce(base::test::RunOnceCallback<1>(std::move(result)));
    EXPECT_CALL(mock_adapter_proxy_, powered())
        .WillOnce(Return(dbus_result_powered));
  }

  void CheckRoutineUpdate(uint32_t progress_percent,
                          mojom::DiagnosticRoutineStatusEnum status,
                          std::string status_message,
                          base::Value::Dict output_dict = base::Value::Dict()) {
    routine_->PopulateStatusUpdate(/*include_output=*/true, update_);
    EXPECT_EQ(update_.progress_percent, progress_percent);
    VerifyNonInteractiveUpdate(update_.routine_update_union, status,
                               status_message);
    EXPECT_EQ(output_dict, base::JSONReader::Read(
                               GetStringFromValidReadOnlySharedMemoryMapping(
                                   std::move(update_.output)),
                               base::JSON_PARSE_CHROMIUM_EXTENSIONS));
  }

  base::Value::Dict ConstructResult(bool hci_powered, bool dbus_powered) {
    base::Value::Dict out_result;
    out_result.Set("hci_powered", hci_powered);
    out_result.Set("dbus_powered", dbus_powered);
    return out_result;
  }

  base::Value::Dict ConstructRoutineOutput(
      base::Value::Dict power_off_result,
      std::optional<base::Value::Dict> power_on_result = std::nullopt) {
    base::Value::Dict output_dict;
    output_dict.Set("power_off_result", std::move(power_off_result));
    if (power_on_result.has_value()) {
      output_dict.Set("power_on_result", std::move(power_on_result.value()));
    }
    return output_dict;
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  std::unique_ptr<DiagnosticRoutine> routine_;
  StrictMock<org::bluez::Adapter1ProxyMock> mock_adapter_proxy_;

 private:
  MockContext mock_context_;
  mojom::RoutineUpdate update_{0, mojo::ScopedHandle(),
                               mojom::RoutineUpdateUnionPtr()};
  StrictMock<org::chromium::bluetooth::ManagerProxyMock> mock_manager_proxy_;
};

// Test that the BluetoothPowerRoutine can be run successfully.
TEST_F(BluezBluetoothPowerRoutineTest, RoutineSuccess) {
  InSequence s;
  // Pre-check.
  EXPECT_CALL(mock_adapter_proxy_, powered()).WillOnce(Return(true));
  EXPECT_CALL(mock_adapter_proxy_, discovering()).WillOnce(Return(false));
  // Power off.
  EXPECT_CALL(mock_adapter_proxy_, powered()).WillOnce(Return(true));
  SetChangePoweredCall(/*current_powered=*/true, /*target_powered=*/false);
  SetVerifyPoweredCall(/*hci_result_powered=*/false,
                       /*dbus_result_powered=*/false);
  // Power on.
  SetChangePoweredCall(/*current_powered=*/false, /*target_powered=*/true);
  SetVerifyPoweredCall(/*hci_result_powered=*/true,
                       /*dbus_result_powered=*/true);
  // Reset powered.
  SetUpGetAdaptersCall(/*adapters=*/{&mock_adapter_proxy_});
  SetChangePoweredCall(/*current_powered=*/true, /*target_powered=*/true);

  routine_->Start();
  CheckRoutineUpdate(100, mojom::DiagnosticRoutineStatusEnum::kPassed,
                     kBluetoothRoutinePassedMessage,
                     ConstructRoutineOutput(ConstructResult(false, false),
                                            ConstructResult(true, true)));
}

// Test that the BluetoothPowerRoutine can be run successfully when the powered
// is off at first.
TEST_F(BluezBluetoothPowerRoutineTest, RoutineSuccessWhenPoweredOff) {
  InSequence s;
  // Pre-check.
  EXPECT_CALL(mock_adapter_proxy_, powered()).WillOnce(Return(false));
  // Power off.
  SetChangePoweredCall(/*current_powered=*/false, /*target_powered=*/false);
  SetVerifyPoweredCall(/*hci_result_powered=*/false,
                       /*dbus_result_powered=*/false);
  // Power on.
  SetChangePoweredCall(/*current_powered=*/false, /*target_powered=*/true);
  SetVerifyPoweredCall(/*hci_result_powered=*/true,
                       /*dbus_result_powered=*/true);
  // Reset powered.
  SetUpGetAdaptersCall(/*adapters=*/{&mock_adapter_proxy_});
  SetChangePoweredCall(/*current_powered=*/true, /*target_powered=*/false);

  routine_->Start();
  CheckRoutineUpdate(100, mojom::DiagnosticRoutineStatusEnum::kPassed,
                     kBluetoothRoutinePassedMessage,
                     ConstructRoutineOutput(ConstructResult(false, false),
                                            ConstructResult(true, true)));
}

// Test that the BluetoothPowerRoutine can handle unexpected powered status in
// HCI level and return a kFailed status.
TEST_F(BluezBluetoothPowerRoutineTest, FailedVerifyPoweredHci) {
  InSequence s;
  // Pre-check.
  EXPECT_CALL(mock_adapter_proxy_, powered()).WillOnce(Return(true));
  EXPECT_CALL(mock_adapter_proxy_, discovering()).WillOnce(Return(false));
  // Power off, but get unexpected powered in HCI level.
  EXPECT_CALL(mock_adapter_proxy_, powered()).WillOnce(Return(true));
  SetChangePoweredCall(/*current_powered=*/true, /*target_powered=*/false);
  SetVerifyPoweredCall(/*hci_result_powered=*/true,
                       /*dbus_result_powered=*/false);
  // Reset powered.
  SetUpGetAdaptersCall(/*adapters=*/{&mock_adapter_proxy_});
  SetChangePoweredCall(/*current_powered=*/false, /*target_powered=*/true);

  routine_->Start();
  CheckRoutineUpdate(100, mojom::DiagnosticRoutineStatusEnum::kFailed,
                     kBluetoothRoutineFailedValidatePowered,
                     ConstructRoutineOutput(ConstructResult(true, false)));
}

// Test that the BluetoothPowerRoutine can handle unexpected powered status in
// D-Bus level and return a kFailed status.
TEST_F(BluezBluetoothPowerRoutineTest, FailedVerifyPoweredDbus) {
  InSequence s;
  // Pre-check.
  EXPECT_CALL(mock_adapter_proxy_, powered()).WillOnce(Return(true));
  EXPECT_CALL(mock_adapter_proxy_, discovering()).WillOnce(Return(false));
  // Power off.
  EXPECT_CALL(mock_adapter_proxy_, powered()).WillOnce(Return(true));
  SetChangePoweredCall(/*current_powered=*/true, /*target_powered=*/false);
  SetVerifyPoweredCall(/*hci_result_powered=*/false,
                       /*dbus_result_powered=*/false);
  // Power on, but get unexpected powered in D-Bus level.
  SetChangePoweredCall(/*current_powered=*/false, /*target_powered=*/true);
  SetVerifyPoweredCall(/*hci_result_powered=*/true,
                       /*dbus_result_powered=*/false);
  // Reset powered.
  SetUpGetAdaptersCall(/*adapters=*/{&mock_adapter_proxy_});
  SetChangePoweredCall(/*current_powered=*/true, /*target_powered=*/true);

  routine_->Start();
  CheckRoutineUpdate(100, mojom::DiagnosticRoutineStatusEnum::kFailed,
                     kBluetoothRoutineFailedValidatePowered,
                     ConstructRoutineOutput(ConstructResult(false, false),
                                            ConstructResult(true, false)));
}

// Test that the BluetoothPowerRoutine returns a kError status when it fails to
// change powered.
TEST_F(BluezBluetoothPowerRoutineTest, FailedChangePoweredOff) {
  InSequence s;
  // Pre-check.
  EXPECT_CALL(mock_adapter_proxy_, powered()).WillOnce(Return(true));
  EXPECT_CALL(mock_adapter_proxy_, discovering()).WillOnce(Return(false));
  // Failed to power off.
  EXPECT_CALL(mock_adapter_proxy_, powered()).WillOnce(Return(true));
  SetChangePoweredCall(/*current_powered=*/true, /*target_powered=*/false,
                       /*is_success=*/false);
  // Reset powered.
  SetUpGetAdaptersCall(/*adapters=*/{&mock_adapter_proxy_});
  SetChangePoweredCall(/*current_powered=*/true, /*target_powered=*/true);

  routine_->Start();
  CheckRoutineUpdate(100, mojom::DiagnosticRoutineStatusEnum::kError,
                     kBluetoothRoutineFailedChangePowered);
}

// Test that the BluetoothPowerRoutine returns a kError status when it fails to
// get adapter.
TEST_F(BluezBluetoothPowerRoutineTest, GetAdapterError) {
  SetUpNullAdapter();
  routine_->Start();
  CheckRoutineUpdate(100, mojom::DiagnosticRoutineStatusEnum::kError,
                     kBluetoothRoutineFailedGetAdapter);
}

// Test that the BluetoothPowerRoutine returns a kFailed status when the adapter
// is in discovery mode.
TEST_F(BluezBluetoothPowerRoutineTest, PreCheckFailed) {
  InSequence s;
  // Pre-check.
  EXPECT_CALL(mock_adapter_proxy_, powered()).WillOnce(Return(true));
  // The adapter is in discovery mode.
  EXPECT_CALL(mock_adapter_proxy_, discovering()).WillOnce(Return(true));

  routine_->Start();
  CheckRoutineUpdate(100, mojom::DiagnosticRoutineStatusEnum::kFailed,
                     kBluetoothRoutineFailedDiscoveryMode);
}

// Test that the BluetoothPowerRoutine returns a kError status when it gets
// error by calling GetHciDeviceConfig from executor.
TEST_F(BluezBluetoothPowerRoutineTest, GetHciDeviceConfigError) {
  InSequence s;
  // Pre-check.
  EXPECT_CALL(mock_adapter_proxy_, powered()).WillOnce(Return(true));
  EXPECT_CALL(mock_adapter_proxy_, discovering()).WillOnce(Return(false));
  // Power off.
  EXPECT_CALL(mock_adapter_proxy_, powered()).WillOnce(Return(true));
  SetChangePoweredCall(/*current_powered=*/true, /*target_powered=*/false);
  // Set error return code.
  auto result = mojom::ExecutedProcessResult::New();
  result->return_code = EXIT_FAILURE;
  result->err = "Failed to run hciconfig";
  EXPECT_CALL(*mock_executor(), GetHciDeviceConfig(kDefaultHciInterface, _))
      .WillOnce(base::test::RunOnceCallback<1>(std::move(result)));
  // Reset powered.
  SetUpGetAdaptersCall(/*adapters=*/{&mock_adapter_proxy_});
  SetChangePoweredCall(/*current_powered=*/false, /*target_powered=*/true);

  routine_->Start();
  CheckRoutineUpdate(100, mojom::DiagnosticRoutineStatusEnum::kError,
                     "GetHciConfig failed with return code: 1 and error: "
                     "Failed to run hciconfig");
}

// Test that the BluetoothPowerRoutine returns a kError status when it failed to
// parse the powered status from the output of calling GetHciDeviceConfig.
TEST_F(BluezBluetoothPowerRoutineTest, UnexpectedHciDeviceConfigError) {
  InSequence s;
  // Pre-check.
  EXPECT_CALL(mock_adapter_proxy_, powered()).WillOnce(Return(true));
  EXPECT_CALL(mock_adapter_proxy_, discovering()).WillOnce(Return(false));
  // Power off.
  EXPECT_CALL(mock_adapter_proxy_, powered()).WillOnce(Return(true));
  SetChangePoweredCall(/*current_powered=*/true, /*target_powered=*/false);
  // Set unexpected output.
  auto result = mojom::ExecutedProcessResult::New();
  result->return_code = EXIT_SUCCESS;
  result->out = "DOWN UP RUNNING";
  EXPECT_CALL(*mock_executor(), GetHciDeviceConfig(kDefaultHciInterface, _))
      .WillOnce(base::test::RunOnceCallback<1>(std::move(result)));
  // Reset powered.
  SetUpGetAdaptersCall(/*adapters=*/{&mock_adapter_proxy_});
  SetChangePoweredCall(/*current_powered=*/false, /*target_powered=*/true);

  routine_->Start();
  CheckRoutineUpdate(100, mojom::DiagnosticRoutineStatusEnum::kError,
                     "Failed to parse powered status from HCI device config.");
}

// Test that the BluetoothPowerRoutine returns a kError status when timeout
// occurred.
TEST_F(BluezBluetoothPowerRoutineTest, RoutineTimeoutOccurred) {
  InSequence s;
  // Pre-check.
  EXPECT_CALL(mock_adapter_proxy_, powered()).WillOnce(Return(true));
  EXPECT_CALL(mock_adapter_proxy_, discovering()).WillOnce(Return(false));
  // Power off.
  EXPECT_CALL(mock_adapter_proxy_, powered())
      .Times(2)
      .WillRepeatedly(Return(true));
  EXPECT_CALL(mock_adapter_proxy_, set_powered(_, _));
  // Reset powered.
  SetUpGetAdaptersCall(/*adapters=*/{&mock_adapter_proxy_});
  SetChangePoweredCall(/*current_powered=*/true, /*target_powered=*/true);

  routine_->Start();
  // Trigger timeout.
  task_environment_.FastForwardBy(kPowerRoutineTimeout);
  CheckRoutineUpdate(100, mojom::DiagnosticRoutineStatusEnum::kError,
                     "Bluetooth routine failed to complete before timeout.");
}

}  // namespace
}  // namespace diagnostics::bluez
