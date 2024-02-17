// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/bluetooth/floss/bluetooth_scanning.h"

#include <algorithm>
#include <utility>
#include <vector>

#include <base/hash/hash.h>
#include <base/strings/string_number_conversions.h>
#include <base/test/gmock_callback_support.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <brillo/variant_dictionary.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diagnostics/cros_healthd/routines/bluetooth/bluetooth_constants.h"
#include "diagnostics/cros_healthd/routines/routine_observer_for_testing.h"
#include "diagnostics/cros_healthd/routines/routine_v2_test_utils.h"
#include "diagnostics/cros_healthd/system/fake_floss_event_hub.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/cros_healthd/system/mock_floss_controller.h"
#include "diagnostics/dbus_bindings/bluetooth_manager/dbus-proxy-mocks.h"
#include "diagnostics/dbus_bindings/floss/dbus-proxy-mocks.h"
#include "diagnostics/mojom/public/cros_healthd_exception.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics::floss {
namespace {

const dbus::ObjectPath kDefaultAdapterPath{
    "/org/chromium/bluetooth/hci0/adapter"};
const int32_t kDefaultHciInterface = 0;

namespace mojom = ::ash::cros_healthd::mojom;

using ::testing::_;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::Sequence;
using ::testing::StrictMock;
using ::testing::WithArg;

struct FakeScannedPeripheral {
  std::string address;
  std::string name;
  brillo::VariantDictionary device_dict;
  std::vector<int16_t> rssi_history;
  bool is_high_signal;
};

class BluetoothScanningRoutineTest : public ::testing::Test {
 public:
  BluetoothScanningRoutineTest(const BluetoothScanningRoutineTest&) = delete;
  BluetoothScanningRoutineTest& operator=(const BluetoothScanningRoutineTest&) =
      delete;

 protected:
  BluetoothScanningRoutineTest() = default;

  MockFlossController* mock_floss_controller() {
    return mock_context_.mock_floss_controller();
  }
  FakeFlossEventHub* fake_floss_event_hub() {
    return mock_context_.fake_floss_event_hub();
  }
  MockExecutor* mock_executor() { return mock_context_.mock_executor(); }

  void SetUp() override {
    EXPECT_CALL(*mock_floss_controller(), GetManager())
        .WillRepeatedly(Return(&mock_manager_proxy_));
    EXPECT_CALL(*mock_floss_controller(), GetAdapters())
        .WillRepeatedly(Return(
            std::vector<org::chromium::bluetooth::BluetoothProxyInterface*>{
                &mock_adapter_proxy_}));
    EXPECT_CALL(mock_manager_proxy_, GetFlossEnabledAsync(_, _, _))
        .WillRepeatedly(
            base::test::RunOnceCallbackRepeatedly<0>(/*enabled=*/true));
    EXPECT_CALL(mock_manager_proxy_, GetDefaultAdapterAsync(_, _, _))
        .WillRepeatedly(base::test::RunOnceCallbackRepeatedly<0>(
            /*hci_interface=*/kDefaultHciInterface));
    EXPECT_CALL(mock_adapter_proxy_, GetObjectPath)
        .WillRepeatedly(ReturnRef(kDefaultAdapterPath));

    auto arg = mojom::BluetoothScanningRoutineArgument::New();
    arg->exec_duration = kScanningRoutineDefaultRuntime;
    base::test::TestFuture<BluetoothScanningRoutine::CreateResult> future;
    BluetoothScanningRoutine::Create(&mock_context_, std::move(arg),
                                     future.GetCallback());
    routine_ = future.Take().value();
  }

  void SetSwitchDiscoveryCall() {
    EXPECT_CALL(mock_adapter_proxy_, StartDiscoveryAsync(_, _, _))
        .WillRepeatedly(WithArg<0>(
            [=, this](base::OnceCallback<void(bool is_success)> on_success) {
              std::move(on_success).Run(/*is_success=*/true);
              for (const auto& peripheral : fake_peripherals_) {
                fake_floss_event_hub()->SendDeviceAdded(peripheral.device_dict);
                // Send out the RSSIs.
                for (int i = 0; i < peripheral.rssi_history.size(); ++i) {
                  fake_floss_event_hub()->SendDevicePropertiesChanged(
                      peripheral.device_dict,
                      {static_cast<uint32_t>(BtPropertyType::kRemoteRssi)});
                }
              }
            }));

    SetupGetRssiCall();

    EXPECT_CALL(mock_adapter_proxy_, CancelDiscoveryAsync(_, _, _))
        .WillRepeatedly(base::test::RunOnceCallback<0>(/*is_success=*/true));
  }

  void SetupGetRssiCall() {
    for (const auto& peripheral : fake_peripherals_) {
      Sequence s;
      for (const auto& rssi : peripheral.rssi_history) {
        EXPECT_CALL(mock_adapter_proxy_,
                    GetRemoteRSSIAsync(peripheral.device_dict, _, _, _))
            .InSequence(s)
            .WillOnce(base::test::RunOnceCallback<1>(rssi));
      }
      EXPECT_CALL(mock_adapter_proxy_,
                  GetRemoteRSSIAsync(peripheral.device_dict, _, _, _))
          .InSequence(s)
          .WillRepeatedly(
              base::test::RunOnceCallbackRepeatedly<1>(/*invalid_rssi=*/127));
    }
  }

  // Setup the call to reset the powered state.
  void SetupResetPoweredCall(bool initial_powered) {
    if (initial_powered) {
      EXPECT_CALL(mock_manager_proxy_,
                  StartAsync(kDefaultHciInterface, _, _, _));
    } else {
      EXPECT_CALL(mock_manager_proxy_,
                  StopAsync(kDefaultHciInterface, _, _, _));
    }
  }

  // Setup the required calls to ensure the powered state is on.
  void SetupEnsurePoweredOnSuccessCall(bool initial_powered) {
    EXPECT_CALL(mock_manager_proxy_,
                GetAdapterEnabledAsync(kDefaultHciInterface, _, _, _))
        .WillRepeatedly(base::test::RunOnceCallbackRepeatedly<1>(
            /*enabled=*/initial_powered));

    if (!initial_powered) {
      EXPECT_CALL(*mock_floss_controller(), GetAdapters())
          .WillOnce(
              Return(std::vector<
                     org::chromium::bluetooth::BluetoothProxyInterface*>{}))
          .WillRepeatedly(Return(
              std::vector<org::chromium::bluetooth::BluetoothProxyInterface*>{
                  &mock_adapter_proxy_}));
      EXPECT_CALL(mock_manager_proxy_,
                  StartAsync(kDefaultHciInterface, _, _, _))
          .WillRepeatedly(
              WithArg<1>([&](base::OnceCallback<void()> on_success) {
                EXPECT_CALL(mock_adapter_proxy_, RegisterCallbackAsync);
                EXPECT_CALL(mock_adapter_proxy_,
                            RegisterConnectionCallbackAsync);
                std::move(on_success).Run();
                fake_floss_event_hub()->SendAdapterAdded(&mock_adapter_proxy_);
                fake_floss_event_hub()->SendAdapterPoweredChanged(
                    kDefaultHciInterface, /*powered=*/true);
              }));
    }
  }

  // Setup the required calls for running the scanning routine successfully. The
  // initial powered sate is on.
  void SetupRoutineSuccessCall(bool initial_powered = true) {
    // Check the powered state and ensure powered state is on.
    SetupEnsurePoweredOnSuccessCall(initial_powered);

    // Check the discovering state if the powered state is on.
    if (initial_powered) {
      EXPECT_CALL(mock_adapter_proxy_, IsDiscoveringAsync(_, _, _))
          .WillRepeatedly(
              base::test::RunOnceCallbackRepeatedly<0>(/*discovering=*/false));
    }

    SetSwitchDiscoveryCall();

    // Stop discovery.
    EXPECT_CALL(mock_adapter_proxy_, CancelDiscoveryAsync(_, _, _))
        .WillRepeatedly(
            base::test::RunOnceCallbackRepeatedly<0>(/*is_success=*/true));

    // Reset powered.
    SetupResetPoweredCall(initial_powered);
  }

  mojom::RoutineStatePtr RunRoutineAndWaitForExit() {
    routine_->SetOnExceptionCallback(UnexpectedRoutineExceptionCallback());
    RoutineObserverForTesting observer;
    routine_->SetObserver(observer.receiver_.BindNewPipeAndPassRemote());
    routine_->Start();
    observer.WaitUntilRoutineFinished();
    return std::move(observer.state_);
  }

  void RunRoutineAndWaitForException(const std::string& expected_reason) {
    base::test::TestFuture<uint32_t, const std::string&> future;
    routine_->SetOnExceptionCallback(future.GetCallback());
    routine_->Start();
    EXPECT_EQ(future.Get<std::string>(), expected_reason)
        << "Unexpected reason in exception.";
  }

  void AddScannedDeviceData(std::string address,
                            std::string name,
                            std::vector<int16_t> rssi_history,
                            bool is_high_signal = true) {
    fake_peripherals_.push_back(
        {.address = address,
         .name = name,
         .device_dict = {{"address", address}, {"name", name}},
         .rssi_history = rssi_history,
         .is_high_signal = is_high_signal});
  }

  mojom::BluetoothScanningRoutineDetailPtr ConstructScanningDetail() {
    auto detail = mojom::BluetoothScanningRoutineDetail::New();
    for (const auto& peripheral : fake_peripherals_) {
      auto out_peripheral = mojom::BluetoothScannedPeripheralInfo::New();
      out_peripheral->rssi_history = peripheral.rssi_history;
      if (peripheral.is_high_signal) {
        out_peripheral->name = peripheral.name;
        out_peripheral->peripheral_id =
            base::NumberToString(base::FastHash(peripheral.address));
      }
      detail->peripherals.push_back(std::move(out_peripheral));
    }
    return detail;
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  MockContext mock_context_;
  std::unique_ptr<BaseRoutineControl> routine_;
  StrictMock<org::chromium::bluetooth::BluetoothProxyMock> mock_adapter_proxy_;
  StrictMock<org::chromium::bluetooth::ManagerProxyMock> mock_manager_proxy_;
  std::vector<FakeScannedPeripheral> fake_peripherals_;
};

// Test that the Bluetooth scanning routine can pass successfully when the
// adapter powered is on at first.
TEST_F(BluetoothScanningRoutineTest, RoutineSuccessWhenPoweredOn) {
  // Set up fake data.
  AddScannedDeviceData(/*address=*/"70:88:6B:92:34:70", /*name=*/"GID6B",
                       /*rssi_history=*/{-54, -56, -52});
  AddScannedDeviceData(/*address=*/"70:D6:9F:0B:4F:D8", /*name=*/"",
                       /*rssi_history=*/{-54});
  // Low signal RSSI history.
  AddScannedDeviceData(
      /*address=*/"6F:92:B8:03:F3:4E", /*name=*/"Low signal device name",
      /*rssi_history=*/
      {kNearbyPeripheralMinimumAverageRssi, kNearbyPeripheralMinimumAverageRssi,
       kNearbyPeripheralMinimumAverageRssi, kNearbyPeripheralMinimumAverageRssi,
       kNearbyPeripheralMinimumAverageRssi - 1},
      /*is_high_signal=*/false);

  SetupRoutineSuccessCall(/*initial_powered=*/true);

  task_environment_.FastForwardBy(kScanningRoutineDefaultRuntime);
  mojom::RoutineStatePtr result = RunRoutineAndWaitForExit();
  EXPECT_EQ(result->percentage, 100);
  EXPECT_TRUE(result->state_union->is_finished());

  const auto& state = result->state_union->get_finished();
  EXPECT_TRUE(state->has_passed);
  EXPECT_TRUE(state->detail->is_bluetooth_scanning());

  const auto& detail = state->detail->get_bluetooth_scanning();
  auto expected_detail = ConstructScanningDetail();
  std::sort(detail->peripherals.begin(), detail->peripherals.end());
  std::sort(expected_detail->peripherals.begin(),
            expected_detail->peripherals.end());
  EXPECT_EQ(detail, expected_detail);
}

// Test that the Bluetooth scanning routine can pass successfully when the
// adapter powered is off at first.
TEST_F(BluetoothScanningRoutineTest, RoutineSuccessWhenPoweredOff) {
  SetupRoutineSuccessCall(/*initial_powered=*/false);

  task_environment_.FastForwardBy(kScanningRoutineDefaultRuntime);
  mojom::RoutineStatePtr result = RunRoutineAndWaitForExit();
  EXPECT_EQ(result->percentage, 100);
  EXPECT_TRUE(result->state_union->is_finished());

  const auto& state = result->state_union->get_finished();
  EXPECT_TRUE(state->has_passed);
  EXPECT_TRUE(state->detail->is_bluetooth_scanning());

  const auto& detail = state->detail->get_bluetooth_scanning();
  EXPECT_EQ(detail, ConstructScanningDetail());
}

// Test that the Bluetooth scanning routine can handle the error when the
// initialization is failed.
TEST_F(BluetoothScanningRoutineTest, RoutineErrorInitialization) {
  EXPECT_CALL(*mock_floss_controller(), GetManager()).WillOnce(Return(nullptr));
  RunRoutineAndWaitForException("Failed to initialize Bluetooth routine.");
}

// Test that the Bluetooth scanning routine can handle the error when the
// adapter is already in discovery mode.
TEST_F(BluetoothScanningRoutineTest, PreCheckErrorAlreadyDiscoveryMode) {
  SetupRoutineSuccessCall(/*initial_powered=*/true);

  // The adapter is in discovery mode.
  EXPECT_CALL(mock_adapter_proxy_, IsDiscoveringAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<0>(/*discovering=*/true));

  RunRoutineAndWaitForException(kBluetoothRoutineFailedDiscoveryMode);
}

// Test that the Bluetooth scanning routine can handle the error when it fails
// to power on the adapter.
TEST_F(BluetoothScanningRoutineTest, PowerOnAdapterError) {
  SetupRoutineSuccessCall(/*initial_powered=*/false);

  // Power on.
  auto error = brillo::Error::Create(FROM_HERE, "", "", "");
  EXPECT_CALL(mock_manager_proxy_, StartAsync(kDefaultHciInterface, _, _, _))
      .WillOnce(base::test::RunOnceCallback<2>(error.get()));

  RunRoutineAndWaitForException(
      "Failed to ensure default adapter is powered on.");
}

// Test that the Bluetooth scanning routine can handle the error when adapter
// fails to start discovery.
TEST_F(BluetoothScanningRoutineTest, StartDiscoveryError) {
  SetupRoutineSuccessCall(/*initial_powered=*/true);

  // Start discovery.
  auto error = brillo::Error::Create(FROM_HERE, "", "", "");
  EXPECT_CALL(mock_adapter_proxy_, StartDiscoveryAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<1>(error.get()));

  RunRoutineAndWaitForException("Failed to update discovery mode.");
}

// Test that the Bluetooth scanning routine can handle the error when adapter
// fails to stop discovery.
TEST_F(BluetoothScanningRoutineTest, StopDiscoveryError) {
  SetupRoutineSuccessCall(/*initial_powered=*/true);

  // Stop discovery.
  auto error = brillo::Error::Create(FROM_HERE, "", "", "");
  EXPECT_CALL(mock_adapter_proxy_, CancelDiscoveryAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<1>(error.get()));

  task_environment_.FastForwardBy(kScanningRoutineDefaultRuntime);
  RunRoutineAndWaitForException("Failed to update discovery mode.");
}

// Test that the Bluetooth scanning routine can handle the error when parsing
// device info.
TEST_F(BluetoothScanningRoutineTest, ParseDeviceInfoError) {
  SetupRoutineSuccessCall(/*initial_powered=*/true);

  // Start discovery.
  EXPECT_CALL(mock_adapter_proxy_, StartDiscoveryAsync(_, _, _))
      .WillOnce(
          WithArg<0>([&](base::OnceCallback<void(bool is_success)> on_success) {
            std::move(on_success).Run(/*is_success=*/true);
            fake_floss_event_hub()->SendDeviceAdded(
                brillo::VariantDictionary{{"no_address", ""}, {"no_name", ""}});
          }));

  RunRoutineAndWaitForException("Failed to parse device info.");
}

// Test that the Bluetooth scanning routine can handle the error when getting
// device RSSI.
TEST_F(BluetoothScanningRoutineTest, GetDeviceRssiError) {
  SetupRoutineSuccessCall(/*initial_powered=*/true);

  // Start discovery.
  EXPECT_CALL(mock_adapter_proxy_, StartDiscoveryAsync(_, _, _))
      .WillOnce(
          WithArg<0>([&](base::OnceCallback<void(bool is_success)> on_success) {
            std::move(on_success).Run(/*is_success=*/true);
            fake_floss_event_hub()->SendDevicePropertiesChanged(
                brillo::VariantDictionary{{"address", ""}, {"name", ""}},
                {static_cast<uint32_t>(BtPropertyType::kRemoteRssi)});
          }));
  auto error = brillo::Error::Create(FROM_HERE, "", "", "");
  EXPECT_CALL(mock_adapter_proxy_, GetRemoteRSSIAsync(_, _, _, _))
      .WillOnce(base::test::RunOnceCallback<2>(error.get()));

  RunRoutineAndWaitForException("Failed to get device RSSI");
}

// Test that the Bluetooth scanning routine can handle the error when timeout
// occurred.
TEST_F(BluetoothScanningRoutineTest, RoutineTimeoutError) {
  SetupRoutineSuccessCall(/*initial_powered=*/true);

  // Failed to get response of |StartDiscovery| method before timeout.
  EXPECT_CALL(mock_adapter_proxy_, StartDiscoveryAsync(_, _, _));

  task_environment_.FastForwardBy(kScanningRoutineDefaultRuntime +
                                  kScanningRoutineTimeout);
  RunRoutineAndWaitForException(
      "Bluetooth routine failed to complete before timeout.");
}

}  // namespace
}  // namespace diagnostics::floss
