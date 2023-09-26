// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdlib>
#include <memory>
#include <utility>
#include <vector>

#include <base/strings/strcat.h>
#include <base/test/gmock_callback_support.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diagnostics/cros_healthd/executor/utils/fake_process_control.h"
#include "diagnostics/cros_healthd/mojom/executor.mojom.h"
#include "diagnostics/cros_healthd/routines/bluetooth/bluetooth_constants.h"
#include "diagnostics/cros_healthd/routines/bluetooth/bluetooth_discovery_v2.h"
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

class BluetoothDiscoveryRoutineV2Test : public testing::Test {
 protected:
  BluetoothDiscoveryRoutineV2Test() = default;
  BluetoothDiscoveryRoutineV2Test(const BluetoothDiscoveryRoutineV2Test&) =
      delete;
  BluetoothDiscoveryRoutineV2Test& operator=(
      const BluetoothDiscoveryRoutineV2Test&) = delete;

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

  void SetupStartBtmonCall() {
    EXPECT_CALL(*mock_executor(), StartBtmon(kDefaultHciInterface, _))
        .WillOnce(WithArg<1>(
            [&](mojo::PendingReceiver<ash::cros_healthd::mojom::ProcessControl>
                    receiver) {
              fake_process_control_.BindReceiver(std::move(receiver));
            }));
  }

  void SetupReadBtmonLogResponseInit() {
    btmon_read_result_->return_code = EXIT_SUCCESS;
    btmon_read_result_->out = "Bluetooth monitor ver 5.54\n";
    EXPECT_CALL(*mock_executor(), ReadBtmonLog(_))
        .WillOnce(base::test::RunOnceCallback<0>(btmon_read_result_.Clone()));

    base::StrAppend(&btmon_read_result_->out,
                    {"= Note: Linux version ..... (x86_64) ...\n",
                     "= Note: Bluetooth subsystem version 2.22 ...\n",
                     "= New Index: C4:23:60:59:2B:75 (...) ...\n",
                     "= Open Index: C4:23:60:59:2B:75 ...\n",
                     "= Index Info: C4:23:60:59:2B:75 (...) ...\n"});
    EXPECT_CALL(*mock_executor(), ReadBtmonLog(_))
        .WillOnce(base::test::RunOnceCallback<0>(btmon_read_result_.Clone()));
  }

  void SetupStartDiscoveryCall(bool result_discovering = true) {
    EXPECT_CALL(mock_adapter_proxy_, StartDiscoveryAsync(_, _, _))
        .WillOnce(WithArg<0>(
            [=](base::OnceCallback<void(bool discovering)> on_success) {
              std::move(on_success).Run(/*discovering=*/true);
              fake_floss_event_hub()->SendAdapterDiscoveringChanged(
                  kDefaultAdapterPath, result_discovering);
            }));
  }

  void SetupCancelDiscoveryCall(bool result_discovering = false) {
    EXPECT_CALL(mock_adapter_proxy_, CancelDiscoveryAsync(_, _, _))
        .WillOnce(WithArg<0>(
            [=](base::OnceCallback<void(bool discovering)> on_success) {
              std::move(on_success).Run(/*discovering=*/false);
              fake_floss_event_hub()->SendAdapterDiscoveringChanged(
                  kDefaultAdapterPath, result_discovering);
            }));
  }

  void SetupReadBtmonLogResponseDiscoveringOn(bool inquiry_on_success = true,
                                              bool lescan_on_success = true) {
    // Inquiry on.
    base::StrAppend(&btmon_read_result_->out,
                    {"< HCI Command: Inquiry (0x01|0x0001) plen 5 ...\n",
                     "> HCI Event: Command Status (0x0f) plen 4\n",
                     "      Inquiry (0x01|0x0001) ncmd 2\n"});
    if (inquiry_on_success) {
      base::StrAppend(&btmon_read_result_->out, {"  Status: Success (0x00)\n"});
    } else {
      base::StrAppend(&btmon_read_result_->out, {"  Status: Others (0xFF)\n"});
    }

    // LE scan on.
    base::StrAppend(
        &btmon_read_result_->out,
        {"< HCI Command: LE Set Extended Scan Enable (0x08|0x0042) plen 6 ..\n",
         "        Extended scan: Enabled (0x01)\n",
         "> HCI Event: Command Complete (0x0e) plen 4 ...\n",
         "      LE Set Extended Scan Enable (0x08|0x0042) ncmd 2\n"});
    if (lescan_on_success) {
      base::StrAppend(&btmon_read_result_->out, {"  Status: Success (0x00)\n"});
    } else {
      base::StrAppend(&btmon_read_result_->out, {"  Status: Others (0xFF)\n"});
    }

    EXPECT_CALL(*mock_executor(), ReadBtmonLog(_))
        .WillOnce(base::test::RunOnceCallback<0>(btmon_read_result_.Clone()));
  }

  void SetupReadBtmonLogResponseDiscoveringOff(bool inquiry_off_success = true,
                                               bool lescan_off_success = true) {
    // Inquiry off.
    base::StrAppend(&btmon_read_result_->out,
                    {"< HCI Command: Inquiry Cancel (0x01|0x0002) plen 0 ...\n",
                     "> HCI Event: Command Complete (0x0e) plen 4 ...\n",
                     "      Inquiry Cancel (0x01|0x0002) ncmd 2\n"});
    if (inquiry_off_success) {
      base::StrAppend(&btmon_read_result_->out, {"  Status: Success (0x00)\n"});
    } else {
      base::StrAppend(&btmon_read_result_->out, {"  Status: Others (0xFF)\n"});
    }

    // LE scan off.
    base::StrAppend(
        &btmon_read_result_->out,
        {"< HCI Command: LE Set Extended Scan Enable (0x08|0x0042) plen 6 ..\n",
         "        Extended scan: Disabled (0x00)\n",
         "> HCI Event: Command Complete (0x0e) plen 4 ...\n",
         "      LE Set Extended Scan Enable (0x08|0x0042) ncmd 2\n"});
    if (lescan_off_success) {
      base::StrAppend(&btmon_read_result_->out, {"  Status: Success (0x00)\n"});
    } else {
      base::StrAppend(&btmon_read_result_->out, {"  Status: Others (0xFF)\n"});
    }

    EXPECT_CALL(*mock_executor(), ReadBtmonLog(_))
        .WillOnce(base::test::RunOnceCallback<0>(btmon_read_result_.Clone()));
  }

  // Setup the call when the adapter added event is received in Floss event hub.
  void SetupAdapterAddedCall() {
    EXPECT_CALL(mock_adapter_proxy_, GetObjectPath)
        .WillOnce(ReturnRef(kDefaultAdapterPath));
    EXPECT_CALL(mock_adapter_proxy_, RegisterCallbackAsync);
    EXPECT_CALL(mock_adapter_proxy_, RegisterConnectionCallbackAsync);
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

  mojom::BluetoothDiscoveringDetailPtr ConstructDiscoveringDetail(
      bool hci_discovering, bool dbus_discovering) {
    auto detail = mojom::BluetoothDiscoveringDetail::New();
    detail->hci_discovering = hci_discovering;
    detail->dbus_discovering = dbus_discovering;
    return detail;
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  MockContext mock_context_;
  BluetoothDiscoveryRoutineV2 routine_{
      &mock_context_, mojom::BluetoothDiscoveryRoutineArgument::New()};
  StrictMock<org::chromium::bluetooth::BluetoothProxyMock> mock_adapter_proxy_;
  StrictMock<org::chromium::bluetooth::ManagerProxyMock> mock_manager_proxy_;
  mojom::ExecutedProcessResultPtr btmon_read_result_ =
      mojom::ExecutedProcessResult::New();
  FakeProcessControl fake_process_control_;
};

// Test that the Bluetooth discovery routine can pass successfully when the
// adapter powered is off at first.
TEST_F(BluetoothDiscoveryRoutineV2Test, RoutineSuccessWhenPoweredOff) {
  InSequence s;
  SetupInitializeSuccessCall(/*initial_powered=*/false);

  // Power on.
  EXPECT_CALL(mock_manager_proxy_, StartAsync(kDefaultHciInterface, _, _, _))
      .WillOnce(WithArg<1>([&](base::OnceCallback<void()> on_success) {
        std::move(on_success).Run();
        fake_floss_event_hub()->SendAdapterAdded(&mock_adapter_proxy_);
        fake_floss_event_hub()->SendAdapterPoweredChanged(kDefaultHciInterface,
                                                          /*powered=*/true);
      }));
  SetupAdapterAddedCall();
  EXPECT_CALL(mock_adapter_proxy_, GetObjectPath)
      .WillOnce(ReturnRef(kDefaultAdapterPath));

  // Start running btmon.
  SetupStartBtmonCall();
  SetupReadBtmonLogResponseInit();

  // Start discovery.
  SetupStartDiscoveryCall();
  SetupReadBtmonLogResponseDiscoveringOn();

  // Stop discovery.
  SetupCancelDiscoveryCall();
  SetupReadBtmonLogResponseDiscoveringOff();

  // Cleanup Logs.
  EXPECT_CALL(*mock_executor(), RemoveBtmonLog(_));
  // Reset Powered.
  SetupResetPoweredCall(/*initial_powered=*/false);

  mojom::RoutineStatePtr result = RunRoutineAndWaitForExit();
  EXPECT_EQ(result->percentage, 100);
  EXPECT_TRUE(result->state_union->is_finished());

  const auto& state = result->state_union->get_finished();
  EXPECT_TRUE(state->has_passed);
  EXPECT_TRUE(state->detail->is_bluetooth_discovery());

  const auto& detail = state->detail->get_bluetooth_discovery();
  EXPECT_EQ(detail->start_discovery_result,
            ConstructDiscoveringDetail(/*hci_discovering=*/true,
                                       /*dbus_discovering=*/true));
  EXPECT_EQ(detail->stop_discovery_result,
            ConstructDiscoveringDetail(/*hci_discovering=*/false,
                                       /*dbus_discovering=*/false));
}

// Test that the Bluetooth discovery routine can pass successfully when the
// adapter powered is on at first.
TEST_F(BluetoothDiscoveryRoutineV2Test, RoutineSuccessWhenPoweredOn) {
  InSequence s;
  SetupInitializeSuccessCall(/*initial_powered=*/true);
  EXPECT_CALL(mock_adapter_proxy_, IsDiscoveringAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<0>(/*discovering=*/false));

  // Start running btmon.
  SetupStartBtmonCall();
  SetupReadBtmonLogResponseInit();

  // Start discovery.
  SetupStartDiscoveryCall();
  SetupReadBtmonLogResponseDiscoveringOn();

  // Stop discovery.
  SetupCancelDiscoveryCall();
  SetupReadBtmonLogResponseDiscoveringOff();

  // Cleanup Logs.
  EXPECT_CALL(*mock_executor(), RemoveBtmonLog(_));
  // Reset Powered.
  SetupResetPoweredCall(/*initial_powered=*/true);

  mojom::RoutineStatePtr result = RunRoutineAndWaitForExit();
  EXPECT_EQ(result->percentage, 100);
  EXPECT_TRUE(result->state_union->is_finished());

  const auto& state = result->state_union->get_finished();
  EXPECT_TRUE(state->has_passed);
  EXPECT_TRUE(state->detail->is_bluetooth_discovery());

  const auto& detail = state->detail->get_bluetooth_discovery();
  EXPECT_EQ(detail->start_discovery_result,
            ConstructDiscoveringDetail(/*hci_discovering=*/true,
                                       /*dbus_discovering=*/true));
  EXPECT_EQ(detail->stop_discovery_result,
            ConstructDiscoveringDetail(/*hci_discovering=*/false,
                                       /*dbus_discovering=*/false));
}

// Test that the Bluetooth discovery routine can handle the error when the
// initialization is failed.
TEST_F(BluetoothDiscoveryRoutineV2Test, RoutineErrorInitialization) {
  InSequence s;
  EXPECT_CALL(*mock_floss_controller(), GetManager()).WillOnce(Return(nullptr));
  RunRoutineAndWaitForException("Failed to initialize Bluetooth routine.");
}

// Test that the Bluetooth discovery routine can handle the error when the
// adapter is already in discovery mode.
TEST_F(BluetoothDiscoveryRoutineV2Test, PreCheckErrorAlreadyDiscoveryMode) {
  InSequence s;
  SetupInitializeSuccessCall(/*initial_powered=*/true);

  // The adapter is in discovery mode.
  EXPECT_CALL(mock_adapter_proxy_, IsDiscoveringAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<0>(/*discovering=*/true));

  // Reset Powered.
  SetupResetPoweredCall(/*initial_powered=*/true);

  RunRoutineAndWaitForException(kBluetoothRoutineFailedDiscoveryMode);
}

// Test that the Bluetooth discovery routine can handle the error when it fails
// to power on the adapter.
TEST_F(BluetoothDiscoveryRoutineV2Test, FailedPowerOnAdapter) {
  InSequence s;
  SetupInitializeSuccessCall(/*initial_powered=*/false);

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
}

// Test that the Bluetooth discovery routine can handle the error when reading
// btmon log.
TEST_F(BluetoothDiscoveryRoutineV2Test, ReadBtmonLogError) {
  InSequence s;
  SetupInitializeSuccessCall(/*initial_powered=*/true);
  EXPECT_CALL(mock_adapter_proxy_, IsDiscoveringAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<0>(/*discovering=*/false));

  // Start running btmon.
  SetupStartBtmonCall();
  btmon_read_result_->return_code = EXIT_FAILURE;
  EXPECT_CALL(*mock_executor(), ReadBtmonLog(_))
      .WillOnce(base::test::RunOnceCallback<0>(btmon_read_result_.Clone()));

  // Cleanup Logs.
  EXPECT_CALL(*mock_executor(), RemoveBtmonLog(_));
  // Reset Powered.
  SetupResetPoweredCall(/*initial_powered=*/true);

  RunRoutineAndWaitForException("Failed to check btmon log file.");
}

// Test that the Bluetooth discovery routine can handle unexpected discovering
// status in HCI level.
TEST_F(BluetoothDiscoveryRoutineV2Test, FailedVerifyDiscoveringHci) {
  InSequence s;
  SetupInitializeSuccessCall(/*initial_powered=*/true);
  EXPECT_CALL(mock_adapter_proxy_, IsDiscoveringAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<0>(/*discovering=*/false));

  // Start running btmon.
  SetupStartBtmonCall();
  SetupReadBtmonLogResponseInit();

  // Start discovery, but get unexpected discovering in HCI level.
  SetupStartDiscoveryCall();
  SetupReadBtmonLogResponseDiscoveringOn(/*inquiry_on_success=*/false,
                                         /*lescan_on_success=*/false);

  // Stop discovery.
  EXPECT_CALL(mock_adapter_proxy_, CancelDiscoveryAsync(_, _, _));

  // Cleanup Logs.
  EXPECT_CALL(*mock_executor(), RemoveBtmonLog(_));
  // Reset Powered.
  SetupResetPoweredCall(/*initial_powered=*/true);

  mojom::RoutineStatePtr result = RunRoutineAndWaitForExit();
  EXPECT_EQ(result->percentage, 100);
  EXPECT_TRUE(result->state_union->is_finished());

  const auto& state = result->state_union->get_finished();
  EXPECT_FALSE(state->has_passed);
  EXPECT_TRUE(state->detail->is_bluetooth_discovery());

  const auto& detail = state->detail->get_bluetooth_discovery();
  EXPECT_EQ(detail->start_discovery_result,
            ConstructDiscoveringDetail(/*hci_discovering=*/false,
                                       /*dbus_discovering=*/true));
  EXPECT_FALSE(detail->stop_discovery_result);
}

// Test that the Bluetooth discovery routine can handle unexpected discovering
// status in D-Bus level.
TEST_F(BluetoothDiscoveryRoutineV2Test, FailedVerifyDiscoveringDbus) {
  InSequence s;
  SetupInitializeSuccessCall(/*initial_powered=*/true);
  EXPECT_CALL(mock_adapter_proxy_, IsDiscoveringAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<0>(/*discovering=*/false));

  // Start running btmon.
  SetupStartBtmonCall();
  SetupReadBtmonLogResponseInit();

  // Start discovery.
  SetupStartDiscoveryCall();
  SetupReadBtmonLogResponseDiscoveringOn();

  // Stop discovery.
  SetupCancelDiscoveryCall(/*result_discovering=*/true);
  SetupReadBtmonLogResponseDiscoveringOff();

  // Cleanup Logs.
  EXPECT_CALL(*mock_executor(), RemoveBtmonLog(_));
  // Reset Powered.
  SetupResetPoweredCall(/*initial_powered=*/true);

  mojom::RoutineStatePtr result = RunRoutineAndWaitForExit();
  EXPECT_EQ(result->percentage, 100);
  EXPECT_TRUE(result->state_union->is_finished());

  const auto& state = result->state_union->get_finished();
  EXPECT_FALSE(state->has_passed);
  EXPECT_TRUE(state->detail->is_bluetooth_discovery());

  const auto& detail = state->detail->get_bluetooth_discovery();
  EXPECT_EQ(detail->start_discovery_result,
            ConstructDiscoveringDetail(/*hci_discovering=*/true,
                                       /*dbus_discovering=*/true));
  EXPECT_EQ(detail->stop_discovery_result,
            ConstructDiscoveringDetail(/*hci_discovering=*/false,
                                       /*dbus_discovering=*/true));
}

// Test that the Bluetooth discovery routine can handle the error when adapter
// fails to start discovery.
TEST_F(BluetoothDiscoveryRoutineV2Test, FailedStartDiscovery) {
  InSequence s;
  SetupInitializeSuccessCall(/*initial_powered=*/true);
  EXPECT_CALL(mock_adapter_proxy_, IsDiscoveringAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<0>(/*discovering=*/false));

  // Start running btmon.
  SetupStartBtmonCall();
  SetupReadBtmonLogResponseInit();

  // Start discovery.
  auto error = brillo::Error::Create(FROM_HERE, "", "", "");
  EXPECT_CALL(mock_adapter_proxy_, StartDiscoveryAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<1>(error.get()));

  // Stop discovery.
  EXPECT_CALL(mock_adapter_proxy_, CancelDiscoveryAsync(_, _, _));

  // Cleanup Logs.
  EXPECT_CALL(*mock_executor(), RemoveBtmonLog(_));
  // Reset Powered.
  SetupResetPoweredCall(/*initial_powered=*/true);

  mojom::RoutineStatePtr result = RunRoutineAndWaitForExit();
  EXPECT_EQ(result->percentage, 100);
  EXPECT_TRUE(result->state_union->is_finished());

  const auto& state = result->state_union->get_finished();
  EXPECT_FALSE(state->has_passed);
  EXPECT_TRUE(state->detail->is_bluetooth_discovery());

  const auto& detail = state->detail->get_bluetooth_discovery();
  EXPECT_FALSE(detail->start_discovery_result);
  EXPECT_FALSE(detail->stop_discovery_result);
}

// Test that the Bluetooth discovery routine can handle the error when adapter
// fails to stop discovery.
TEST_F(BluetoothDiscoveryRoutineV2Test, FailedStopDiscovery) {
  InSequence s;
  SetupInitializeSuccessCall(/*initial_powered=*/true);
  EXPECT_CALL(mock_adapter_proxy_, IsDiscoveringAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<0>(/*discovering=*/false));

  // Start running btmon.
  SetupStartBtmonCall();
  SetupReadBtmonLogResponseInit();

  // Start discovery.
  SetupStartDiscoveryCall();
  SetupReadBtmonLogResponseDiscoveringOn();

  // Stop discovery.
  auto error = brillo::Error::Create(FROM_HERE, "", "", "");
  EXPECT_CALL(mock_adapter_proxy_, CancelDiscoveryAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<1>(error.get()));

  // Cleanup Logs.
  EXPECT_CALL(*mock_executor(), RemoveBtmonLog(_));
  // Reset Powered.
  SetupResetPoweredCall(/*initial_powered=*/true);

  mojom::RoutineStatePtr result = RunRoutineAndWaitForExit();
  EXPECT_EQ(result->percentage, 100);
  EXPECT_TRUE(result->state_union->is_finished());

  const auto& state = result->state_union->get_finished();
  EXPECT_FALSE(state->has_passed);
  EXPECT_TRUE(state->detail->is_bluetooth_discovery());

  const auto& detail = state->detail->get_bluetooth_discovery();
  EXPECT_EQ(detail->start_discovery_result,
            ConstructDiscoveringDetail(/*hci_discovering=*/true,
                                       /*dbus_discovering=*/true));
  EXPECT_FALSE(detail->stop_discovery_result);
}

// Test that the Bluetooth discovery routine can handle the error when btmon is
// terminated unexpectedly.
TEST_F(BluetoothDiscoveryRoutineV2Test, BtmonTerminatedError) {
  InSequence s;
  SetupInitializeSuccessCall(/*initial_powered=*/true);
  EXPECT_CALL(mock_adapter_proxy_, IsDiscoveringAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<0>(/*discovering=*/false));

  // Start running btmon.
  SetupStartBtmonCall();
  SetupReadBtmonLogResponseInit();

  // Start discovery.
  EXPECT_CALL(mock_adapter_proxy_, StartDiscoveryAsync(_, _, _))
      .WillOnce([&]() { fake_process_control_.receiver().reset(); });

  // Stop discovery.
  EXPECT_CALL(mock_adapter_proxy_, CancelDiscoveryAsync(_, _, _));

  // Cleanup Logs.
  EXPECT_CALL(*mock_executor(), RemoveBtmonLog(_));
  // Reset Powered.
  SetupResetPoweredCall(/*initial_powered=*/true);

  RunRoutineAndWaitForException("Btmon is terminated unexpectedly.");
}

// Test that the Bluetooth discovery routine can handle the error when timeout
// occurred.
TEST_F(BluetoothDiscoveryRoutineV2Test, RoutineTimeoutOccurred) {
  InSequence s;
  SetupInitializeSuccessCall(/*initial_powered=*/true);
  EXPECT_CALL(mock_adapter_proxy_, IsDiscoveringAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<0>(/*discovering=*/false));

  // Start running btmon.
  SetupStartBtmonCall();
  SetupReadBtmonLogResponseInit();

  // Start discovery.
  EXPECT_CALL(mock_adapter_proxy_, StartDiscoveryAsync(_, _, _));
  // Stop discovery.
  EXPECT_CALL(mock_adapter_proxy_, CancelDiscoveryAsync(_, _, _));

  // Cleanup Logs.
  EXPECT_CALL(*mock_executor(), RemoveBtmonLog(_));
  // Reset Powered.
  SetupResetPoweredCall(/*initial_powered=*/true);

  task_environment_.FastForwardBy(kDiscoveryRoutineTimeout);
  RunRoutineAndWaitForException(
      "Bluetooth routine failed to complete before timeout.");
}

}  // namespace
}  // namespace diagnostics
