// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/bluetooth/bluetooth_pairing_v2.h"

#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include <base/strings/string_number_conversions.h>
#include <base/test/gmock_callback_support.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <base/uuid.h>
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
#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics {
namespace {

const dbus::ObjectPath kDefaultAdapterPath{
    "/org/chromium/bluetooth/hci0/adapter"};
constexpr int32_t kDefaultHciInterface = 0;

constexpr char kTestTargetDeviceAddress[] = "C1:D3:95:8F:A9:0B";
const brillo::VariantDictionary kTestTargetDevice = {
    {"name", std::string("Test device")},
    {"address", std::string(kTestTargetDeviceAddress)}};
const std::vector<uint8_t> kTestUuidBytes = {0x00, 0x00, 0x11, 0x0a, 0x00, 0x00,
                                             0x10, 0x00, 0x80, 0x00, 0x00, 0x80,
                                             0x5f, 0x9b, 0x34, 0xfb};
constexpr char kTestUuidString[] = "0000110a-0000-1000-8000-00805f9b34fb";
constexpr uint32_t kTargetBluetoothClass = 2360344;
// Value of public address type.
constexpr uint32_t kTargetAddressTypeValue = 0;

namespace mojom = ash::cros_healthd::mojom;

using ::testing::_;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::StrictMock;
using ::testing::WithArg;

class BluetoothPairingRoutineV2Test : public testing::Test {
 protected:
  BluetoothPairingRoutineV2Test() = default;
  BluetoothPairingRoutineV2Test(const BluetoothPairingRoutineV2Test&) = delete;
  BluetoothPairingRoutineV2Test& operator=(
      const BluetoothPairingRoutineV2Test&) = delete;

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
        .WillRepeatedly(base::test::RunOnceCallback<0>(/*floss_enabled=*/true));
    EXPECT_CALL(mock_manager_proxy_, GetDefaultAdapterAsync(_, _, _))
        .WillRepeatedly(base::test::RunOnceCallback<0>(
            /*hci_interface=*/kDefaultHciInterface));
    EXPECT_CALL(mock_adapter_proxy_, GetObjectPath)
        .WillRepeatedly(ReturnRef(kDefaultAdapterPath));
  }

  // Setup the call |GetBondedDevices|.
  void SetupGetBondedDevicesCall(
      const std::vector<brillo::VariantDictionary>& bonded_devices) {
    EXPECT_CALL(mock_adapter_proxy_, GetBondedDevicesAsync(_, _, _))
        .WillRepeatedly(base::test::RunOnceCallback<0>(bonded_devices));
  }

  // The adapter starts discovery and send device added events for each device
  // in |added_devices|.
  void SetupStartDiscoveryCall(
      const std::vector<brillo::VariantDictionary>& added_devices) {
    EXPECT_CALL(mock_adapter_proxy_, StartDiscoveryAsync(_, _, _))
        .WillRepeatedly(WithArg<0>(
            [=, this](base::OnceCallback<void(bool is_success)> on_success) {
              std::move(on_success).Run(/*is_success=*/true);
              // Send out peripheral in |added_devices|.
              for (const auto& device : added_devices)
                fake_floss_event_hub()->SendDeviceAdded(device);
            }));
  }

  // Get all required device properties.
  void SetupGetDevicePropertiesCall() {
    EXPECT_CALL(mock_adapter_proxy_,
                GetRemoteUuidsAsync(kTestTargetDevice, _, _, _))
        .WillRepeatedly(base::test::RunOnceCallback<1>(
            std::vector<std::vector<uint8_t>>{kTestUuidBytes}));
    EXPECT_CALL(mock_adapter_proxy_,
                GetRemoteClassAsync(kTestTargetDevice, _, _, _))
        .WillRepeatedly(base::test::RunOnceCallback<1>(kTargetBluetoothClass));
    EXPECT_CALL(mock_adapter_proxy_,
                GetRemoteAddressTypeAsync(kTestTargetDevice, _, _, _))
        .WillRepeatedly(
            base::test::RunOnceCallback<1>(kTargetAddressTypeValue));
  }

  // Complete the bonding process with the target peripheral.
  void SetupBondTargetPeripheralCall() {
    EXPECT_CALL(mock_adapter_proxy_,
                CreateBondAsync(kTestTargetDevice, _, _, _, _))
        .WillRepeatedly(WithArg<2>(
            [this](base::OnceCallback<void(bool is_success)> on_success) {
              std::move(on_success).Run(/*is_success=*/true);
              fake_floss_event_hub()->SendDeviceConnectedChanged(
                  kTestTargetDevice, /*connected=*/true);
              // |bt_status| is 0 for Success and |bond_state| is 1 for Bonding.
              fake_floss_event_hub()->SendDeviceBondChanged(
                  /*bt_status=*/0, kTestTargetDeviceAddress, /*bond_state=*/1);
              fake_floss_event_hub()->SendDeviceSspRequest(kTestTargetDevice);
            }));
    // |state| is 1 for |ConnectedOnly|.
    EXPECT_CALL(mock_adapter_proxy_,
                GetConnectionStateAsync(kTestTargetDevice, _, _, _))
        .WillRepeatedly(base::test::RunOnceCallback<1>(/*state=*/1));
    EXPECT_CALL(mock_adapter_proxy_,
                SetPairingConfirmationAsync(kTestTargetDevice, true, _, _, _))
        .WillRepeatedly(WithArg<2>(
            [this](base::OnceCallback<void(bool is_success)> on_success) {
              std::move(on_success).Run(/*is_success=*/true);
              // |bt_status| is 0 for Success and |bond_state| is 2 for Bonded.
              fake_floss_event_hub()->SendDeviceBondChanged(
                  /*bt_status=*/0, kTestTargetDeviceAddress, /*bond_state=*/2);
            }));
  }

  // Setup the call |RemoveBond|.
  void SetupRemoveBondCall(bool is_success = true) {
    EXPECT_CALL(mock_adapter_proxy_,
                RemoveBondAsync(kTestTargetDevice, _, _, _))
        .WillRepeatedly(base::test::RunOnceCallback<1>(is_success));
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
        .WillRepeatedly(
            base::test::RunOnceCallback<1>(/*enabled=*/initial_powered));

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

  // Setup the required calls for running the pairing routine successfully. The
  // initial powered sate is on.
  void SetupRoutineSuccessCall(bool initial_powered = true) {
    // Check the powered state and ensure powered state is on.
    SetupEnsurePoweredOnSuccessCall(initial_powered);

    // Check the discovering state if the powered state is on.
    if (initial_powered) {
      EXPECT_CALL(mock_adapter_proxy_, IsDiscoveringAsync(_, _, _))
          .WillOnce(base::test::RunOnceCallback<0>(/*discovering=*/false));
    }

    // Check bonded devices.
    SetupGetBondedDevicesCall(/*bonded_devices=*/{});

    // Start discovery and find the target peripheral.
    SetupStartDiscoveryCall(/*added_devices=*/{kTestTargetDevice});

    // Update the peripheral alias.
    EXPECT_CALL(mock_adapter_proxy_,
                SetRemoteAliasAsync(kTestTargetDevice, _, _, _, _))
        .WillRepeatedly(base::test::RunOnceCallback<2>());

    // Get the peripheral's properties.
    SetupGetDevicePropertiesCall();

    // Start the bonding process.
    SetupBondTargetPeripheralCall();

    // Remove the bond of the target peripheral.
    SetupRemoveBondCall();

    // Stop discovery.
    EXPECT_CALL(mock_adapter_proxy_, CancelDiscoveryAsync(_, _, _))
        .WillRepeatedly(base::test::RunOnceCallback<0>(/*is_success=*/true));

    // Reset powered.
    SetupResetPoweredCall(initial_powered);
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

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  MockContext mock_context_;
  BluetoothPairingRoutineV2 routine_{
      &mock_context_,
      mojom::BluetoothPairingRoutineArgument::New(
          base::NumberToString(base::FastHash((kTestTargetDeviceAddress))))};
  StrictMock<org::chromium::bluetooth::BluetoothProxyMock> mock_adapter_proxy_;
  StrictMock<org::chromium::bluetooth::ManagerProxyMock> mock_manager_proxy_;
};

// Test that the Bluetooth pairing routine can pass successfully.
TEST_F(BluetoothPairingRoutineV2Test, RoutineSuccess) {
  SetupRoutineSuccessCall(/*initial_powered=*/false);

  mojom::RoutineStatePtr result = RunRoutineAndWaitForExit();
  EXPECT_EQ(result->percentage, 100);
  EXPECT_TRUE(result->state_union->is_finished());

  const auto& state = result->state_union->get_finished();
  EXPECT_TRUE(state->has_passed);
  EXPECT_TRUE(state->detail->is_bluetooth_pairing());

  const auto& detail = state->detail->get_bluetooth_pairing();
  EXPECT_EQ(detail->pairing_peripheral->connect_error,
            mojom::BluetoothPairingPeripheralInfo_ConnectError::kNone);
  EXPECT_EQ(detail->pairing_peripheral->pair_error,
            mojom::BluetoothPairingPeripheralInfo_PairError::kNone);
  EXPECT_EQ(
      detail->pairing_peripheral->uuids,
      std::vector<base::Uuid>{base::Uuid::ParseLowercase(kTestUuidString)});
  EXPECT_EQ(detail->pairing_peripheral->bluetooth_class, kTargetBluetoothClass);
  // The test address is actually a valid random address. Check if address
  // validation can report correct result when we assume it is a public address.
  EXPECT_EQ(detail->pairing_peripheral->address_type,
            mojom::BluetoothPairingPeripheralInfo_AddressType::kPublic);
  EXPECT_FALSE(detail->pairing_peripheral->is_address_valid);
  EXPECT_EQ(detail->pairing_peripheral->failed_manufacturer_id, "C1:D3:95");
}

// Test that the Bluetooth pairing routine can handle the error when the
// initialization is failed.
TEST_F(BluetoothPairingRoutineV2Test, RoutineErrorInitialization) {
  EXPECT_CALL(*mock_floss_controller(), GetManager()).WillOnce(Return(nullptr));
  RunRoutineAndWaitForException("Failed to initialize Bluetooth routine.");
}

// Test that the Bluetooth pairing routine can handle the error when the
// adapter is already in discovery mode.
TEST_F(BluetoothPairingRoutineV2Test, PreCheckErrorAlreadyDiscoveryMode) {
  // Check the powered state and ensure powered state is on.
  SetupEnsurePoweredOnSuccessCall(/*initial_powered=*/true);
  // The adapter is in discovery mode.
  EXPECT_CALL(mock_adapter_proxy_, IsDiscoveringAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<0>(/*discovering=*/true));
  // Reset powered.
  EXPECT_CALL(mock_manager_proxy_, StartAsync(kDefaultHciInterface, _, _, _));

  RunRoutineAndWaitForException(kBluetoothRoutineFailedDiscoveryMode);
}

// Test that the Bluetooth pairing routine can handle the error when it fails
// to power on the adapter.
TEST_F(BluetoothPairingRoutineV2Test, PowerOnAdapterError) {
  SetupRoutineSuccessCall(/*initial_powered=*/false);

  // Power on error.
  auto error = brillo::Error::Create(FROM_HERE, "", "", "");
  EXPECT_CALL(mock_manager_proxy_, StartAsync(kDefaultHciInterface, _, _, _))
      .WillOnce(base::test::RunOnceCallback<2>(error.get()));

  RunRoutineAndWaitForException(
      "Failed to ensure default adapter is powered on.");
}

// Test that the Bluetooth pairing routine can handle the error when getting
// bonded devices.
TEST_F(BluetoothPairingRoutineV2Test, GetBondedDevicesError) {
  SetupRoutineSuccessCall(/*initial_powered=*/true);

  // Check bonded devices error.
  auto error = brillo::Error::Create(FROM_HERE, "", "", "");
  EXPECT_CALL(mock_adapter_proxy_, GetBondedDevicesAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<1>(error.get()));

  RunRoutineAndWaitForException("Failed to get bonded devices.");
}

// Test that the Bluetooth pairing routine can handle the error when parsing
// bonded devices.
TEST_F(BluetoothPairingRoutineV2Test, ParseBondedDevicesError) {
  SetupRoutineSuccessCall(/*initial_powered=*/true);

  // Check bonded devices.
  SetupGetBondedDevicesCall(/*bonded_devices=*/{brillo::VariantDictionary{
      {"no_name", std::string("Test device")},
      {"no_address", std::string(kTestTargetDeviceAddress)}}});

  RunRoutineAndWaitForException("Failed to parse device info.");
}

// Test that the Bluetooth pairing routine can handle the error when the target
// peripheral is already bonded.
TEST_F(BluetoothPairingRoutineV2Test, TargetPeripheralIsBondedError) {
  SetupRoutineSuccessCall(/*initial_powered=*/true);

  // Check bonded devices.
  SetupGetBondedDevicesCall(/*bonded_devices=*/{kTestTargetDevice});

  RunRoutineAndWaitForException("The target peripheral is already paired.");
}

// Test that the Bluetooth pairing routine can handle the error when adapter
// fails to start discovery.
TEST_F(BluetoothPairingRoutineV2Test, StartDiscoveryError) {
  SetupRoutineSuccessCall(/*initial_powered=*/true);

  // Check bonded devices.
  SetupGetBondedDevicesCall(/*bonded_devices=*/{});
  // Start discovery.
  auto error = brillo::Error::Create(FROM_HERE, "", "", "");
  // Reset powered.
  EXPECT_CALL(mock_adapter_proxy_, StartDiscoveryAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<1>(error.get()));

  RunRoutineAndWaitForException("Failed to update discovery mode.");
}

// Test that the Bluetooth pairing routine can handle the error when parsing
// scanned devices.
TEST_F(BluetoothPairingRoutineV2Test, ParseScannedDevicesError) {
  SetupRoutineSuccessCall(/*initial_powered=*/true);

  // Check bonded devices.
  SetupGetBondedDevicesCall(/*bonded_devices=*/{});
  // Start discovery.
  SetupStartDiscoveryCall(/*added_devices=*/{brillo::VariantDictionary{
      {"no_name", std::string("Test device")},
      {"no_address", std::string(kTestTargetDeviceAddress)}}});

  RunRoutineAndWaitForException("Failed to parse device info.");
}

// Test that the Bluetooth pairing routine can handle the error when updating
// the device alias.
TEST_F(BluetoothPairingRoutineV2Test, UpdateDeviceAliasError) {
  SetupRoutineSuccessCall(/*initial_powered=*/true);

  // Set the peripheral alias error.
  auto error = brillo::Error::Create(FROM_HERE, "", "", "");
  EXPECT_CALL(mock_adapter_proxy_,
              SetRemoteAliasAsync(kTestTargetDevice, _, _, _, _))
      .WillOnce(base::test::RunOnceCallback<3>(error.get()));

  RunRoutineAndWaitForException("Failed to update device alias.");
}

// Test that the Bluetooth pairing routine can handle the error when collecting
// UUIDs.
TEST_F(BluetoothPairingRoutineV2Test, GetDeviceUuidsError) {
  SetupRoutineSuccessCall(/*initial_powered=*/true);

  // Get error when collecting UUIDs and stop the routine.
  auto error = brillo::Error::Create(FROM_HERE, "", "", "");
  EXPECT_CALL(mock_adapter_proxy_,
              GetRemoteUuidsAsync(kTestTargetDevice, _, _, _))
      .WillOnce(base::test::RunOnceCallback<2>(error.get()));

  RunRoutineAndWaitForException("Failed to get device UUIDs.");
}

// Test that the Bluetooth pairing routine can handle the error when collecting
// Bluetooth class.
TEST_F(BluetoothPairingRoutineV2Test, GetDeviceClassError) {
  SetupRoutineSuccessCall(/*initial_powered=*/true);

  // Get error when collecting Bluetooth class and stop the routine.
  auto error = brillo::Error::Create(FROM_HERE, "", "", "");
  EXPECT_CALL(mock_adapter_proxy_,
              GetRemoteClassAsync(kTestTargetDevice, _, _, _))
      .WillOnce(base::test::RunOnceCallback<2>(error.get()));

  RunRoutineAndWaitForException("Failed to get device class.");
}

// Test that the Bluetooth pairing routine can handle the error when collecting
// address type.
TEST_F(BluetoothPairingRoutineV2Test, GetDeviceAddressTypeError) {
  SetupRoutineSuccessCall(/*initial_powered=*/true);

  // Get error when collecting address type and stop the routine.
  auto error = brillo::Error::Create(FROM_HERE, "", "", "");
  EXPECT_CALL(mock_adapter_proxy_,
              GetRemoteAddressTypeAsync(kTestTargetDevice, _, _, _))
      .WillOnce(base::test::RunOnceCallback<2>(error.get()));

  RunRoutineAndWaitForException("Failed to get device address type.");
}

// Test that the Bluetooth pairing routine can handle the unexpected connection
// state when creating the bond of target peripheral.
TEST_F(BluetoothPairingRoutineV2Test, UnexpectedConnectionState) {
  SetupRoutineSuccessCall(/*initial_powered=*/true);

  // Start the bonding process.
  EXPECT_CALL(mock_adapter_proxy_,
              CreateBondAsync(kTestTargetDevice, _, _, _, _))
      .WillOnce(WithArg<2>(
          [this](base::OnceCallback<void(bool is_success)> on_success) {
            std::move(on_success).Run(/*is_success=*/true);
            fake_floss_event_hub()->SendDeviceConnectedChanged(
                kTestTargetDevice, /*connected=*/true);
          }));
  // |state| is 0 for |NotConnected|.
  EXPECT_CALL(mock_adapter_proxy_,
              GetConnectionStateAsync(kTestTargetDevice, _, _, _))
      .WillOnce(base::test::RunOnceCallback<1>(/*state=*/0));

  mojom::RoutineStatePtr result = RunRoutineAndWaitForExit();
  EXPECT_EQ(result->percentage, 100);
  EXPECT_TRUE(result->state_union->is_finished());

  const auto& state = result->state_union->get_finished();
  EXPECT_FALSE(state->has_passed);
  EXPECT_TRUE(state->detail->is_bluetooth_pairing());

  const auto& detail = state->detail->get_bluetooth_pairing();
  // Usually the routine will get a pair error in this case, but we focus on
  // connect error in this case.
  EXPECT_EQ(detail->pairing_peripheral->connect_error,
            mojom::BluetoothPairingPeripheralInfo_ConnectError::kNotConnected);
}

// Test that the Bluetooth pairing routine can handle the error when getting
// connection state.
TEST_F(BluetoothPairingRoutineV2Test, GetConnectionStateError) {
  SetupRoutineSuccessCall(/*initial_powered=*/true);

  auto error = brillo::Error::Create(FROM_HERE, "", "", "");
  EXPECT_CALL(mock_adapter_proxy_,
              GetConnectionStateAsync(kTestTargetDevice, _, _, _))
      .WillOnce(base::test::RunOnceCallback<2>(error.get()));

  RunRoutineAndWaitForException("Failed to get device connection state.");
}

// Test that the Bluetooth pairing routine can handle the error when creating
// the bond of target peripheral.
TEST_F(BluetoothPairingRoutineV2Test, CreateBondError) {
  SetupRoutineSuccessCall(/*initial_powered=*/true);

  // Start the bonding process error.
  auto error = brillo::Error::Create(FROM_HERE, "", "", "");
  EXPECT_CALL(mock_adapter_proxy_,
              CreateBondAsync(kTestTargetDevice, _, _, _, _))
      .WillOnce(base::test::RunOnceCallback<3>(error.get()));

  mojom::RoutineStatePtr result = RunRoutineAndWaitForExit();
  EXPECT_EQ(result->percentage, 100);
  EXPECT_TRUE(result->state_union->is_finished());

  const auto& state = result->state_union->get_finished();
  EXPECT_FALSE(state->has_passed);
  EXPECT_TRUE(state->detail->is_bluetooth_pairing());

  const auto& detail = state->detail->get_bluetooth_pairing();
  EXPECT_EQ(
      detail->pairing_peripheral->connect_error,
      mojom::BluetoothPairingPeripheralInfo_ConnectError::kNoConnectedEvent);
  EXPECT_EQ(detail->pairing_peripheral->pair_error,
            mojom::BluetoothPairingPeripheralInfo_PairError::kBondFailed);
}

// Test that the Bluetooth pairing routine can handle the unsuccessful Bluetooth
// status when creating the bond of target peripheral.
TEST_F(BluetoothPairingRoutineV2Test, BadBluetoothStatusWhenCreatingBond) {
  SetupRoutineSuccessCall(/*initial_powered=*/true);

  // Start the bonding process.
  EXPECT_CALL(mock_adapter_proxy_,
              CreateBondAsync(kTestTargetDevice, _, _, _, _))
      .WillOnce(WithArg<2>(
          [this](base::OnceCallback<void(bool is_success)> on_success) {
            std::move(on_success).Run(/*is_success=*/true);
            // Send the unexpected |bt_status|. |bond_state| is 1 for NotBonded.
            fake_floss_event_hub()->SendDeviceBondChanged(
                /*bt_status=*/1, kTestTargetDeviceAddress, /*bond_state=*/0);
          }));

  mojom::RoutineStatePtr result = RunRoutineAndWaitForExit();
  EXPECT_EQ(result->percentage, 100);
  EXPECT_TRUE(result->state_union->is_finished());

  const auto& state = result->state_union->get_finished();
  EXPECT_FALSE(state->has_passed);
  EXPECT_TRUE(state->detail->is_bluetooth_pairing());

  const auto& detail = state->detail->get_bluetooth_pairing();
  EXPECT_EQ(
      detail->pairing_peripheral->connect_error,
      mojom::BluetoothPairingPeripheralInfo_ConnectError::kNoConnectedEvent);
  EXPECT_EQ(detail->pairing_peripheral->pair_error,
            mojom::BluetoothPairingPeripheralInfo_PairError::kBadStatus);
}

// Test that the Bluetooth pairing routine can handle error when setting pairing
// confirmation for SSP request.
TEST_F(BluetoothPairingRoutineV2Test, SspRequestError) {
  SetupRoutineSuccessCall(/*initial_powered=*/true);

  // Start the bonding process.
  EXPECT_CALL(mock_adapter_proxy_,
              CreateBondAsync(kTestTargetDevice, _, _, _, _))
      .WillOnce(WithArg<2>(
          [this](base::OnceCallback<void(bool is_success)> on_success) {
            std::move(on_success).Run(/*is_success=*/true);
            fake_floss_event_hub()->SendDeviceSspRequest(kTestTargetDevice);
          }));
  auto error = brillo::Error::Create(FROM_HERE, "", "", "");
  EXPECT_CALL(mock_adapter_proxy_,
              SetPairingConfirmationAsync(kTestTargetDevice, true, _, _, _))
      .WillOnce(base::test::RunOnceCallback<3>(error.get()));

  mojom::RoutineStatePtr result = RunRoutineAndWaitForExit();
  EXPECT_EQ(result->percentage, 100);
  EXPECT_TRUE(result->state_union->is_finished());

  const auto& state = result->state_union->get_finished();
  EXPECT_FALSE(state->has_passed);
  EXPECT_TRUE(state->detail->is_bluetooth_pairing());

  const auto& detail = state->detail->get_bluetooth_pairing();
  EXPECT_EQ(
      detail->pairing_peripheral->connect_error,
      mojom::BluetoothPairingPeripheralInfo_ConnectError::kNoConnectedEvent);
  EXPECT_EQ(detail->pairing_peripheral->pair_error,
            mojom::BluetoothPairingPeripheralInfo_PairError::kSspFailed);
}

// Test that the Bluetooth pairing routine can handle the error when removing
// the bond of target peripheral.
TEST_F(BluetoothPairingRoutineV2Test, RemoveBondError) {
  SetupRoutineSuccessCall(/*initial_powered=*/true);

  // Remove the bond of the target peripheral.
  SetupRemoveBondCall(/*is_success=*/false);

  RunRoutineAndWaitForException("Failed to remove target peripheral.");
}

// Test that the Bluetooth pairing routine can handle the failure when the
// routine fails to find target peripheral.
TEST_F(BluetoothPairingRoutineV2Test, FailedFindTargetPeripheral) {
  SetupRoutineSuccessCall(/*initial_powered=*/true);

  // Start discovery and check if the non-target peripheral can be ignored.
  SetupStartDiscoveryCall(/*added_devices=*/{brillo::VariantDictionary{
      {"name", std::string("Other device")},
      {"address", std::string("XX:XX:XX:XX:XX:XX")}}});

  task_environment_.FastForwardBy(kPairingRoutineTimeout);
  mojom::RoutineStatePtr result = RunRoutineAndWaitForExit();
  EXPECT_EQ(result->percentage, 100);
  EXPECT_TRUE(result->state_union->is_finished());

  const auto& state = result->state_union->get_finished();
  EXPECT_FALSE(state->has_passed);
  EXPECT_TRUE(state->detail->is_bluetooth_pairing());

  const auto& detail = state->detail->get_bluetooth_pairing();
  EXPECT_FALSE(detail->pairing_peripheral);
}

// Test that the Bluetooth pairing routine can handle the error when timeout
// occurred.
TEST_F(BluetoothPairingRoutineV2Test, RoutineTimeoutOccurred) {
  SetupRoutineSuccessCall(/*initial_powered=*/true);

  // Failed to get response of |StartDiscovery| method before timeout.
  EXPECT_CALL(mock_adapter_proxy_, StartDiscoveryAsync(_, _, _));

  task_environment_.FastForwardBy(kPairingRoutineTimeout);
  RunRoutineAndWaitForException(
      "Bluetooth routine failed to complete before timeout.");
}

}  // namespace
}  // namespace diagnostics
