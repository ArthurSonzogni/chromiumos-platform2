// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdlib>
#include <memory>
#include <utility>
#include <vector>

#include <base/test/gmock_callback_support.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diagnostics/cros_healthd/mojom/executor.mojom.h"
#include "diagnostics/cros_healthd/routines/bluetooth/bluetooth_constants.h"
#include "diagnostics/cros_healthd/routines/bluetooth/bluetooth_power_v2.h"
#include "diagnostics/cros_healthd/routines/routine_observer_for_testing.h"
#include "diagnostics/cros_healthd/routines/routine_v2_test_utils.h"
#include "diagnostics/cros_healthd/system/fake_floss_event_hub.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/cros_healthd/system/mock_floss_controller.h"
#include "diagnostics/dbus_bindings/bluetooth_manager/dbus-proxy-mocks.h"
#include "diagnostics/dbus_bindings/floss/dbus-proxy-mocks.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics {
namespace {

const dbus::ObjectPath kDefaultAdapterPath{
    "/org/chromium/bluetooth/hci0/adapter"};
const int32_t kDefaultHciInterface = 0;

namespace mojom = ash::cros_healthd::mojom;

using ::testing::_;
using ::testing::InSequence;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::StrictMock;
using ::testing::WithArg;

class BluetoothPowerRoutineV2Test : public testing::Test {
 protected:
  BluetoothPowerRoutineV2Test() = default;
  BluetoothPowerRoutineV2Test(const BluetoothPowerRoutineV2Test&) = delete;
  BluetoothPowerRoutineV2Test& operator=(const BluetoothPowerRoutineV2Test&) =
      delete;

  MockFlossController* mock_floss_controller() {
    return mock_context_.mock_floss_controller();
  }
  FakeFlossEventHub* fake_floss_event_hub() {
    return mock_context_.fake_floss_event_hub();
  }
  MockExecutor* mock_executor() { return mock_context_.mock_executor(); }

  // Get the adapter with HCI interface 0.
  void SetupGetAdaptersCall() {
    EXPECT_CALL(*mock_floss_controller(), GetAdapters())
        .WillOnce(Return(
            std::vector<org::chromium::bluetooth::BluetoothProxyInterface*>{
                &mock_adapter_proxy_}));
    EXPECT_CALL(mock_adapter_proxy_, GetObjectPath)
        .WillOnce(ReturnRef(kDefaultAdapterPath));
  }

  // Setup all the required call for calling |Initialize| successfully.
  void SetupInitializeSuccessCall(bool initial_powered) {
    EXPECT_CALL(*mock_floss_controller(), GetManager())
        .WillOnce(Return(&mock_manager_proxy_));
    EXPECT_CALL(mock_manager_proxy_, GetDefaultAdapterAsync(_, _, _))
        .WillOnce(base::test::RunOnceCallback<0>(/*hci_interface=*/0));
    if (initial_powered) {
      SetupGetAdaptersCall();
    } else {
      EXPECT_CALL(*mock_floss_controller(), GetAdapters())
          .WillOnce(
              Return(std::vector<
                     org::chromium::bluetooth::BluetoothProxyInterface*>{}));
    }
    EXPECT_CALL(mock_manager_proxy_,
                GetAdapterEnabledAsync(kDefaultHciInterface, _, _, _))
        .WillOnce(base::test::RunOnceCallback<1>(/*enabled=*/initial_powered));
  }

  // Setup the call when the adapter added event is received in Floss event hub.
  void SetupAdapterAddedCall() {
    EXPECT_CALL(mock_adapter_proxy_, GetObjectPath)
        .WillOnce(ReturnRef(kDefaultAdapterPath));
    EXPECT_CALL(mock_adapter_proxy_, RegisterCallbackAsync);
    EXPECT_CALL(mock_adapter_proxy_, RegisterConnectionCallbackAsync);
  }

  // Setup the powered status after changing in HCI level and D-Bus level.
  void SetupHciConfigCall(bool hci_result_powered) {
    auto result = mojom::ExecutedProcessResult::New();
    result->return_code = EXIT_SUCCESS;
    if (hci_result_powered) {
      result->out = "UP RUNNING\n";
    } else {
      result->out = "DOWN\n";
    }
    EXPECT_CALL(*mock_executor(), GetHciDeviceConfig(kDefaultHciInterface, _))
        .WillOnce(base::test::RunOnceCallback<1>(std::move(result)));
  }

  void SetupResetPoweredCall(bool initial_powered) {
    EXPECT_CALL(*mock_floss_controller(), GetManager())
        .WillOnce(Return(&mock_manager_proxy_));
    if (initial_powered) {
      EXPECT_CALL(mock_manager_proxy_,
                  StartAsync(kDefaultHciInterface, _, _, _));
    } else {
      EXPECT_CALL(mock_manager_proxy_,
                  StopAsync(kDefaultHciInterface, _, _, _));
    }
  }

  mojom::RoutineStatePtr RunRoutineAndWaitForExit() {
    routine_.SetOnExceptionCallback(UnexpectedRoutineExceptionCallback());
    base::test::TestFuture<void> signal;
    RoutineObserverForTesting observer{signal.GetCallback()};
    routine_.SetObserver(observer.receiver_.BindNewPipeAndPassRemote());
    routine_.Start();
    EXPECT_TRUE(signal.Wait());
    return std::move(observer.state_);
  }

  void RunRoutineAndWaitForException(const std::string& expected_reason) {
    base::test::TestFuture<uint32_t, const std::string&> future;
    routine_.SetOnExceptionCallback(future.GetCallback());
    routine_.Start();
    EXPECT_EQ(future.Get<std::string>(), expected_reason)
        << "Unexpected reason in exception.";
  }

  mojom::BluetoothPoweredDetailPtr ConstructPoweredDetail(bool hci_powered,
                                                          bool dbus_powered) {
    auto detail = mojom::BluetoothPoweredDetail::New();
    detail->hci_powered = hci_powered;
    detail->dbus_powered = dbus_powered;
    return detail;
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  MockContext mock_context_;
  BluetoothPowerRoutineV2 routine_{&mock_context_,
                                   mojom::BluetoothPowerRoutineArgument::New()};
  StrictMock<org::chromium::bluetooth::BluetoothProxyMock> mock_adapter_proxy_;
  StrictMock<org::chromium::bluetooth::ManagerProxyMock> mock_manager_proxy_;
};

// Test that the Bluetooth power routine can pass successfully when the adapter
// powered is on at first.
TEST_F(BluetoothPowerRoutineV2Test, RoutineSuccessWhenPoweredOn) {
  InSequence s;
  SetupInitializeSuccessCall(/*initial_powered=*/true);
  EXPECT_CALL(mock_adapter_proxy_, IsDiscoveringAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<0>(/*discovering=*/false));

  // Power off.
  EXPECT_CALL(mock_manager_proxy_, StopAsync(kDefaultHciInterface, _, _, _))
      .WillOnce(WithArg<1>([&](base::OnceCallback<void()> on_success) {
        std::move(on_success).Run();
        fake_floss_event_hub()->SendAdapterRemoved(kDefaultAdapterPath);
        fake_floss_event_hub()->SendAdapterPoweredChanged(/*hci_interface=*/0,
                                                          /*powered=*/false);
      }));
  SetupHciConfigCall(/*hci_result_powered=*/false);

  // Power on.
  EXPECT_CALL(mock_manager_proxy_, StartAsync(kDefaultHciInterface, _, _, _))
      .WillOnce(WithArg<1>([&](base::OnceCallback<void()> on_success) {
        std::move(on_success).Run();
        fake_floss_event_hub()->SendAdapterAdded(&mock_adapter_proxy_);
        fake_floss_event_hub()->SendAdapterPoweredChanged(/*hci_interface=*/0,
                                                          /*powered=*/true);
      }));
  SetupAdapterAddedCall();
  EXPECT_CALL(mock_adapter_proxy_, GetObjectPath)
      .WillOnce(ReturnRef(kDefaultAdapterPath));
  SetupHciConfigCall(/*hci_result_powered=*/true);

  // Reset Powered.
  SetupResetPoweredCall(/*initial_powered=*/true);

  mojom::RoutineStatePtr result = RunRoutineAndWaitForExit();
  EXPECT_EQ(result->percentage, 100);
  EXPECT_TRUE(result->state_union->is_finished());

  const auto& state = result->state_union->get_finished();
  EXPECT_TRUE(state->has_passed);
  EXPECT_TRUE(state->detail->is_bluetooth_power());

  const auto& detail = state->detail->get_bluetooth_power();
  EXPECT_EQ(
      detail->power_off_result,
      ConstructPoweredDetail(/*hci_powered=*/false, /*dbus_powered=*/false));
  EXPECT_EQ(
      detail->power_on_result,
      ConstructPoweredDetail(/*hci_powered=*/true, /*dbus_powered=*/true));
}

// Test that the Bluetooth power routine can pass successfully when the adapter
// powered is off at first.
TEST_F(BluetoothPowerRoutineV2Test, RoutineSuccessWhenPoweredOff) {
  InSequence s;
  SetupInitializeSuccessCall(/*initial_powered=*/false);
  SetupHciConfigCall(/*hci_result_powered=*/false);

  // Power on.
  EXPECT_CALL(mock_manager_proxy_, StartAsync(kDefaultHciInterface, _, _, _))
      .WillOnce(WithArg<1>([&](base::OnceCallback<void()> on_success) {
        std::move(on_success).Run();
        fake_floss_event_hub()->SendAdapterAdded(&mock_adapter_proxy_);
        fake_floss_event_hub()->SendAdapterPoweredChanged(/*hci_interface=*/0,
                                                          /*powered=*/true);
      }));
  SetupAdapterAddedCall();
  EXPECT_CALL(mock_adapter_proxy_, GetObjectPath)
      .WillOnce(ReturnRef(kDefaultAdapterPath));
  SetupHciConfigCall(/*hci_result_powered=*/true);

  // Reset Powered.
  SetupResetPoweredCall(/*initial_powered=*/false);

  mojom::RoutineStatePtr result = RunRoutineAndWaitForExit();
  EXPECT_EQ(result->percentage, 100);
  EXPECT_TRUE(result->state_union->is_finished());

  const auto& state = result->state_union->get_finished();
  EXPECT_TRUE(state->has_passed);
  EXPECT_TRUE(state->detail->is_bluetooth_power());

  const auto& detail = state->detail->get_bluetooth_power();
  EXPECT_EQ(
      detail->power_off_result,
      ConstructPoweredDetail(/*hci_powered=*/false, /*dbus_powered=*/false));
  EXPECT_EQ(
      detail->power_on_result,
      ConstructPoweredDetail(/*hci_powered=*/true, /*dbus_powered=*/true));
}

// Test that the Bluetooth power routine can handle the error when the
// initialization is failed.
TEST_F(BluetoothPowerRoutineV2Test, RoutineErrorInitialization) {
  InSequence s;
  EXPECT_CALL(*mock_floss_controller(), GetManager()).WillOnce(Return(nullptr));
  RunRoutineAndWaitForException("Failed to initialize Bluetooth routine");
}

// Test that the Bluetooth power routine can handle the error when the adapter
// is already in discovery mode.
TEST_F(BluetoothPowerRoutineV2Test, PreCheckErrorAlreadyDiscoveryMode) {
  InSequence s;
  SetupInitializeSuccessCall(/*initial_powered=*/true);

  // The adapter is in discovery mode.
  EXPECT_CALL(mock_adapter_proxy_, IsDiscoveringAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<0>(/*discovering=*/true));

  // Reset Powered.
  SetupResetPoweredCall(/*initial_powered=*/true);

  RunRoutineAndWaitForException(kBluetoothRoutineFailedDiscoveryMode);
}

// Test that the Bluetooth power routine can handle the error when the adapter
// failed to get discovering state.
TEST_F(BluetoothPowerRoutineV2Test, PreCheckErrorGetDiscoveringState) {
  InSequence s;
  SetupInitializeSuccessCall(/*initial_powered=*/true);

  // Get error when running pre-check.
  auto error = brillo::Error::Create(FROM_HERE, "", "", "");
  EXPECT_CALL(mock_adapter_proxy_, IsDiscoveringAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<1>(error.get()));

  // Reset Powered.
  SetupResetPoweredCall(/*initial_powered=*/true);

  RunRoutineAndWaitForException("Failed to get adapter discovering state.");
}

// Test that the Bluetooth power routine can handle the error when changing
// powered state.
TEST_F(BluetoothPowerRoutineV2Test, FailedChangePoweredState) {
  InSequence s;
  SetupInitializeSuccessCall(/*initial_powered=*/false);
  SetupHciConfigCall(/*hci_result_powered=*/false);

  // Power on.
  auto error = brillo::Error::Create(FROM_HERE, "", "", "");
  EXPECT_CALL(mock_manager_proxy_, StartAsync(kDefaultHciInterface, _, _, _))
      .WillOnce(base::test::RunOnceCallback<2>(error.get()));

  // Reset Powered.
  SetupResetPoweredCall(/*initial_powered=*/false);

  mojom::RoutineStatePtr result = RunRoutineAndWaitForExit();
  EXPECT_EQ(result->percentage, 100);
  EXPECT_TRUE(result->state_union->is_finished());

  const auto& state = result->state_union->get_finished();
  EXPECT_FALSE(state->has_passed);
  EXPECT_TRUE(state->detail->is_bluetooth_power());

  const auto& detail = state->detail->get_bluetooth_power();
  EXPECT_EQ(
      detail->power_off_result,
      ConstructPoweredDetail(/*hci_powered=*/false, /*dbus_powered=*/false));
  EXPECT_FALSE(detail->power_on_result);
}

// Test that the Bluetooth power routine can handle unexpected powered status in
// HCI level.
TEST_F(BluetoothPowerRoutineV2Test, FailedVerifyPoweredHci) {
  InSequence s;
  SetupInitializeSuccessCall(/*initial_powered=*/true);
  EXPECT_CALL(mock_adapter_proxy_, IsDiscoveringAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<0>(/*discovering=*/false));

  // Power off, but get unexpected powered in HCI level.
  EXPECT_CALL(mock_manager_proxy_, StopAsync(kDefaultHciInterface, _, _, _))
      .WillOnce(WithArg<1>([&](base::OnceCallback<void()> on_success) {
        std::move(on_success).Run();
        fake_floss_event_hub()->SendAdapterRemoved(kDefaultAdapterPath);
        fake_floss_event_hub()->SendAdapterPoweredChanged(/*hci_interface=*/0,
                                                          /*powered=*/false);
      }));
  SetupHciConfigCall(/*hci_result_powered=*/true);

  // Reset Powered.
  SetupResetPoweredCall(/*initial_powered=*/true);

  mojom::RoutineStatePtr result = RunRoutineAndWaitForExit();
  EXPECT_EQ(result->percentage, 100);
  EXPECT_TRUE(result->state_union->is_finished());

  const auto& state = result->state_union->get_finished();
  EXPECT_FALSE(state->has_passed);
  EXPECT_TRUE(state->detail->is_bluetooth_power());

  const auto& detail = state->detail->get_bluetooth_power();
  EXPECT_EQ(
      detail->power_off_result,
      ConstructPoweredDetail(/*hci_powered=*/true, /*dbus_powered=*/false));
  EXPECT_FALSE(detail->power_on_result);
}

// Test that the Bluetooth power routine can handle unexpected powered status in
// D-Bus level.
TEST_F(BluetoothPowerRoutineV2Test, FailedVerifyPoweredDbus) {
  InSequence s;
  SetupInitializeSuccessCall(/*initial_powered=*/true);
  EXPECT_CALL(mock_adapter_proxy_, IsDiscoveringAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<0>(/*discovering=*/false));

  // Power off.
  EXPECT_CALL(mock_manager_proxy_, StopAsync(kDefaultHciInterface, _, _, _))
      .WillOnce(WithArg<1>([&](base::OnceCallback<void()> on_success) {
        std::move(on_success).Run();
        fake_floss_event_hub()->SendAdapterRemoved(kDefaultAdapterPath);
        fake_floss_event_hub()->SendAdapterPoweredChanged(/*hci_interface=*/0,
                                                          /*powered=*/false);
      }));
  SetupHciConfigCall(/*hci_result_powered=*/false);

  // Power on, but get unexpected powered in D-Bus level.
  EXPECT_CALL(mock_manager_proxy_, StartAsync(kDefaultHciInterface, _, _, _))
      .WillOnce(WithArg<1>([&](base::OnceCallback<void()> on_success) {
        std::move(on_success).Run();
        fake_floss_event_hub()->SendAdapterAdded(&mock_adapter_proxy_);
        fake_floss_event_hub()->SendAdapterPoweredChanged(/*hci_interface=*/0,
                                                          /*powered=*/false);
      }));
  SetupAdapterAddedCall();
  EXPECT_CALL(mock_adapter_proxy_, GetObjectPath)
      .WillOnce(ReturnRef(kDefaultAdapterPath));
  SetupHciConfigCall(/*hci_result_powered=*/true);

  // Reset Powered.
  SetupResetPoweredCall(/*initial_powered=*/true);

  mojom::RoutineStatePtr result = RunRoutineAndWaitForExit();
  EXPECT_EQ(result->percentage, 100);
  EXPECT_TRUE(result->state_union->is_finished());

  const auto& state = result->state_union->get_finished();
  EXPECT_FALSE(state->has_passed);
  EXPECT_TRUE(state->detail->is_bluetooth_power());

  const auto& detail = state->detail->get_bluetooth_power();
  EXPECT_EQ(
      detail->power_off_result,
      ConstructPoweredDetail(/*hci_powered=*/false, /*dbus_powered=*/false));
  EXPECT_EQ(
      detail->power_on_result,
      ConstructPoweredDetail(/*hci_powered=*/true, /*dbus_powered=*/false));
}

// Test that the Bluetooth power routine can handle the error when it gets
// error by calling GetHciDeviceConfig from executor.
TEST_F(BluetoothPowerRoutineV2Test, HciconfigError) {
  InSequence s;
  SetupInitializeSuccessCall(/*initial_powered=*/false);

  // Setup error return code for hciconfig.
  auto result = mojom::ExecutedProcessResult::New();
  result->return_code = EXIT_FAILURE;
  result->err = "Failed to run hciconfig";
  EXPECT_CALL(*mock_executor(), GetHciDeviceConfig(kDefaultHciInterface, _))
      .WillOnce(base::test::RunOnceCallback<1>(std::move(result)));

  // Reset Powered.
  SetupResetPoweredCall(/*initial_powered=*/false);

  RunRoutineAndWaitForException(
      "Failed to parse powered status from HCI device config.");
}

// Test that the Bluetooth power routine can handle the error when it failed to
// parse the powered status from the output of calling GetHciDeviceConfig.
TEST_F(BluetoothPowerRoutineV2Test, HciconfigUnexpectedOutput) {
  InSequence s;
  SetupInitializeSuccessCall(/*initial_powered=*/false);

  // Setup unexpected output for hciconfig.
  auto result = mojom::ExecutedProcessResult::New();
  result->return_code = EXIT_SUCCESS;
  result->out = "DOWN UP RUNNING";
  EXPECT_CALL(*mock_executor(), GetHciDeviceConfig(kDefaultHciInterface, _))
      .WillOnce(base::test::RunOnceCallback<1>(std::move(result)));

  // Reset Powered.
  SetupResetPoweredCall(/*initial_powered=*/false);

  RunRoutineAndWaitForException(
      "Failed to parse powered status from HCI device config.");
}

// Test that the Bluetooth power routine can handle the error when timeout
// occurred.
TEST_F(BluetoothPowerRoutineV2Test, RoutineTimeoutOccurred) {
  InSequence s;
  SetupInitializeSuccessCall(/*initial_powered=*/true);
  EXPECT_CALL(mock_adapter_proxy_, IsDiscoveringAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<0>(/*discovering=*/false));

  // Power off but not send adapter powered change events.
  EXPECT_CALL(mock_manager_proxy_, StopAsync(kDefaultHciInterface, _, _, _))
      .WillOnce(base::test::RunOnceCallback<1>());

  // Reset Powered.
  SetupResetPoweredCall(/*initial_powered=*/true);

  // Trigger timeout.
  task_environment_.FastForwardBy(kPowerRoutineTimeout);
  RunRoutineAndWaitForException(
      "Bluetooth routine failed to complete before timeout.");
}

}  // namespace
}  // namespace diagnostics
