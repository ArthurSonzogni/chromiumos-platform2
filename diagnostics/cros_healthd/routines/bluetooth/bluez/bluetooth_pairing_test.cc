// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/bluetooth/bluez/bluetooth_pairing.h"

#include <memory>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/hash/hash.h>
#include <base/json/json_reader.h>
#include <base/strings/string_number_conversions.h>
#include <base/test/gmock_callback_support.h>
#include <base/test/task_environment.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diagnostics/cros_healthd/routines/bluetooth/bluetooth_constants.h"
#include "diagnostics/cros_healthd/routines/routine_test_utils.h"
#include "diagnostics/cros_healthd/system/fake_bluez_event_hub.h"
#include "diagnostics/cros_healthd/system/mock_bluez_controller.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/dbus_bindings/bluez/dbus-proxy-mocks.h"

namespace diagnostics::bluez {
namespace {

namespace mojom = ::ash::cros_healthd::mojom;

using ::testing::_;
using ::testing::InSequence;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::StrictMock;
using ::testing::WithArg;
using ::testing::WithArgs;

class BluezBluetoothPairingRoutineTest : public testing::Test {
 public:
  BluezBluetoothPairingRoutineTest(const BluezBluetoothPairingRoutineTest&) =
      delete;
  BluezBluetoothPairingRoutineTest& operator=(
      const BluezBluetoothPairingRoutineTest&) = delete;

 protected:
  BluezBluetoothPairingRoutineTest() = default;

  void SetUp() override {
    SetUpGetAdaptersCall(/*adapters=*/{&mock_adapter_proxy_});
    routine_ = std::make_unique<BluetoothPairingRoutine>(
        &mock_context_, base::NumberToString(base::FastHash(target_address_)));
  }

  MockExecutor* mock_executor() { return mock_context_.mock_executor(); }

  MockBluezController* mock_bluez_controller() {
    return mock_context_.mock_bluez_controller();
  }

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
    routine_ = std::make_unique<BluetoothPairingRoutine>(
        &mock_context_, base::NumberToString(base::FastHash(target_address_)));
  }

  // Change the powered from |current_powered| to |target_powered|.
  void SetChangePoweredCall(bool current_powered,
                            bool target_powered,
                            bool is_success = true) {
    EXPECT_CALL(mock_adapter_proxy_, powered())
        .WillOnce(Return(current_powered));
    if (current_powered != target_powered) {
      EXPECT_CALL(mock_adapter_proxy_, set_powered(target_powered, _))
          .WillOnce(base::test::RunOnceCallback<1>(is_success));
    }
  }

  // The adapter starts discovery and send device added events for each device
  // in |added_devices|.
  void SetStartDiscoveryCall(
      bool is_success,
      const std::vector<org::bluez::Device1ProxyInterface*>& added_devices) {
    if (is_success) {
      EXPECT_CALL(mock_adapter_proxy_, StartDiscoveryAsync(_, _, _))
          .WillOnce(
              WithArg<0>([=, this](base::OnceCallback<void()> on_success) {
                std::move(on_success).Run();
                // Send out peripheral in |added_devices|.
                for (const auto& device : added_devices)
                  fake_bluez_event_hub()->SendDeviceAdded(device);
              }));
    } else {
      EXPECT_CALL(mock_adapter_proxy_, StartDiscoveryAsync(_, _, _))
          .WillOnce(base::test::RunOnceCallback<1>(nullptr));
    }
  }

  // The adapter stops discovery.
  void SetStopDiscoveryCall(bool is_success) {
    if (is_success) {
      EXPECT_CALL(mock_adapter_proxy_, StopDiscoveryAsync(_, _, _))
          .WillOnce(base::test::RunOnceCallback<0>());
    } else {
      EXPECT_CALL(mock_adapter_proxy_, StopDiscoveryAsync(_, _, _))
          .WillOnce(base::test::RunOnceCallback<1>(nullptr));
    }
  }

  void SetChangeAliasCall(bool is_success, const std::string& expected_alias) {
    EXPECT_CALL(mock_target_device_, set_alias(expected_alias, _))
        .WillOnce(base::test::RunOnceCallback<1>(is_success));
  }

  // The |mock_target_device_| with the address |target_address_| is expected to
  // be added.
  void SetDeviceAddedCall() {
    // Function call in BluezEventHub::OnDeviceAdded.
    EXPECT_CALL(mock_target_device_, SetPropertyChangedCallback(_));

    // Function call in device added callback.
    EXPECT_CALL(mock_target_device_, address())
        .WillOnce(ReturnRef(target_address_));
    EXPECT_CALL(mock_target_device_, address_type())
        .WillOnce(ReturnRef(target_address_type_));
    // Bluetooth class of device (CoD).
    if (target_bluetooth_class_.has_value()) {
      EXPECT_CALL(mock_target_device_, is_bluetooth_class_valid())
          .WillOnce(Return(true));
      EXPECT_CALL(mock_target_device_, bluetooth_class())
          .WillOnce(Return(target_bluetooth_class_.value()));
    } else {
      EXPECT_CALL(mock_target_device_, is_bluetooth_class_valid())
          .WillOnce(Return(false));
    }
    // UUIDs.
    if (!target_uuids_.empty()) {
      EXPECT_CALL(mock_target_device_, is_uuids_valid()).WillOnce(Return(true));
      EXPECT_CALL(mock_target_device_, uuids())
          .WillOnce(ReturnRef(target_uuids_));
    } else {
      EXPECT_CALL(mock_target_device_, is_uuids_valid())
          .WillOnce(Return(false));
    }
  }

  // Successfully connect the |mock_target_device_| and report the connection
  // result.
  void SetConnectDeviceCall(bool connected_result = true) {
    EXPECT_CALL(mock_target_device_, ConnectAsync(_, _, _))
        .WillOnce(WithArg<0>([&](base::OnceCallback<void()> on_success) {
          std::move(on_success).Run();
          // Send out connected status changed event.
          fake_bluez_event_hub()->SendDevicePropertyChanged(
              &mock_target_device_, mock_target_device_.ConnectedName());
        }));
    EXPECT_CALL(mock_target_device_, connected())
        .WillOnce(Return(connected_result));
  }

  // Successfully pair the |mock_target_device_|.
  void SetPairDeviceCall(bool paired_result = true) {
    // Return false to call Pair function.
    EXPECT_CALL(mock_target_device_, paired()).WillOnce(Return(false));
    EXPECT_CALL(mock_target_device_, PairAsync(_, _, _))
        .WillOnce(WithArg<0>([&](base::OnceCallback<void()> on_success) {
          std::move(on_success).Run();
          // Send out paired status changed event.
          fake_bluez_event_hub()->SendDevicePropertyChanged(
              &mock_target_device_, mock_target_device_.PairedName());
        }));
    // Return false to monitor paired changed event.
    EXPECT_CALL(mock_target_device_, paired()).WillOnce(Return(false));
    EXPECT_CALL(mock_target_device_, paired()).WillOnce(Return(paired_result));
  }

  // The adapter removes the |mock_target_device_|.
  void SetRemoveDeviceCall() {
    EXPECT_CALL(mock_target_device_, GetObjectPath())
        .WillOnce(ReturnRef(target_device_path_));
    EXPECT_CALL(mock_adapter_proxy_, RemoveDeviceAsync(_, _, _, _))
        .WillOnce(WithArgs<0, 1>([&](const dbus::ObjectPath& in_device,
                                     base::OnceCallback<void()> on_success) {
          EXPECT_EQ(in_device, target_device_path_);
          std::move(on_success).Run();
        }));
  }

  base::Value::Dict ConstructOutputDict(
      const brillo::Error* connect_error = nullptr,
      const brillo::Error* pair_error = nullptr) {
    base::Value::Dict output_pairing_peripheral;
    output_pairing_peripheral.Set("address_type", target_address_type_);
    output_pairing_peripheral.Set("is_address_valid", true);

    if (target_bluetooth_class_.has_value()) {
      output_pairing_peripheral.Set(
          "bluetooth_class",
          base::NumberToString(target_bluetooth_class_.value()));
    }
    if (!target_uuids_.empty()) {
      base::Value::List out_uuids;
      for (const auto& uuid : target_uuids_)
        out_uuids.Append(uuid);
      output_pairing_peripheral.Set("uuids", std::move(out_uuids));
    }

    if (connect_error)
      output_pairing_peripheral.Set("connect_error", connect_error->GetCode());
    if (pair_error)
      output_pairing_peripheral.Set("pair_error", pair_error->GetCode());

    base::Value::Dict output_dict;
    output_dict.Set("pairing_peripheral", std::move(output_pairing_peripheral));
    return output_dict;
  }

  void CheckRoutineUpdate(uint32_t progress_percent,
                          mojom::DiagnosticRoutineStatusEnum status,
                          std::string status_message,
                          base::Value::Dict output_dict = base::Value::Dict()) {
    routine_->PopulateStatusUpdate(&update_, true);
    EXPECT_EQ(update_.progress_percent, progress_percent);
    VerifyNonInteractiveUpdate(update_.routine_update_union, status,
                               status_message);
    EXPECT_EQ(output_dict, base::JSONReader::Read(
                               GetStringFromValidReadOnlySharedMemoryMapping(
                                   std::move(update_.output))));
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  std::unique_ptr<DiagnosticRoutine> routine_;
  StrictMock<org::bluez::Adapter1ProxyMock> mock_adapter_proxy_;
  StrictMock<org::bluez::Device1ProxyMock> mock_target_device_;

  const std::string target_address_ = "70:88:6B:92:34:70";
  const dbus::ObjectPath target_device_path_ =
      dbus::ObjectPath("/org/bluez/dev_70_88_6B_92_34_70");

 private:
  MockContext mock_context_;
  const std::string target_address_type_ = "random";
  const std::optional<uint32_t> target_bluetooth_class_ = 2360344;
  const std::vector<std::string> target_uuids_ = {
      "0000110b-0000-1000-8000-00805f9b34fb",
      "0000110c-0000-1000-8000-00805f9b34fb",
      "0000110e-0000-1000-8000-00805f9b34fb",
      "0000111e-0000-1000-8000-00805f9b34fb",
      "00001200-0000-1000-8000-00805f9b34fb"};
  mojom::RoutineUpdate update_{0, mojo::ScopedHandle(),
                               mojom::RoutineUpdateUnionPtr()};
};

// Test that the BluetoothPairingRoutine can be run successfully.
TEST_F(BluezBluetoothPairingRoutineTest, RoutineSuccess) {
  InSequence s;
  // Pre-check.
  EXPECT_CALL(mock_adapter_proxy_, powered()).WillOnce(Return(false));
  // Ensure adapter is powered on.
  SetChangePoweredCall(/*current_powered=*/false, /*target_powered=*/true);

  // Check if the target peripheral is cached. If so, remove it.
  EXPECT_CALL(*mock_bluez_controller(), GetDevices())
      .WillOnce(Return(std::vector<org::bluez::Device1ProxyInterface*>{
          &mock_target_device_, nullptr}));
  EXPECT_CALL(mock_target_device_, address())
      .WillOnce(ReturnRef(target_address_));
  EXPECT_CALL(mock_target_device_, alias()).WillOnce(ReturnRef(""));
  EXPECT_CALL(mock_target_device_, paired()).WillOnce(Return(false));
  SetRemoveDeviceCall();

  // Start discovery.
  SetStartDiscoveryCall(/*is_success=*/true,
                        /*added_devices=*/{&mock_target_device_});
  SetDeviceAddedCall();
  SetChangeAliasCall(/*is_success=*/true,
                     /*expected_alias=*/kHealthdBluetoothDiagnosticsTag);
  SetConnectDeviceCall();
  SetPairDeviceCall();
  SetChangeAliasCall(/*is_success=*/true, /*expected_alias=*/"");
  SetRemoveDeviceCall();
  // Stop Discovery.
  SetStopDiscoveryCall(/*is_success=*/true);
  // Reset powered.
  SetUpGetAdaptersCall(/*adapters=*/{&mock_adapter_proxy_});
  SetChangePoweredCall(/*current_powered=*/true, /*target_powered=*/false);

  routine_->Start();
  CheckRoutineUpdate(100, mojom::DiagnosticRoutineStatusEnum::kPassed,
                     kBluetoothRoutinePassedMessage, ConstructOutputDict());
}

// Test that the BluetoothPairingRoutine can be run successfully when the device
// is paired automatically during connecting.
TEST_F(BluezBluetoothPairingRoutineTest, RoutineSuccessOnlyConnect) {
  InSequence s;
  // Pre-check.
  EXPECT_CALL(mock_adapter_proxy_, powered()).WillOnce(Return(false));
  // Ensure adapter is powered on.
  SetChangePoweredCall(/*current_powered=*/false, /*target_powered=*/true);

  // Check existing devices when the target peripheral is cached.
  EXPECT_CALL(*mock_bluez_controller(), GetDevices())
      .WillOnce(Return(std::vector<org::bluez::Device1ProxyInterface*>{}));

  // Start discovery.
  SetStartDiscoveryCall(/*is_success=*/true,
                        /*added_devices=*/{&mock_target_device_});
  SetDeviceAddedCall();
  SetChangeAliasCall(/*is_success=*/true,
                     /*expected_alias=*/kHealthdBluetoothDiagnosticsTag);
  SetConnectDeviceCall();
  // The device is paired automatically after connecting.
  EXPECT_CALL(mock_target_device_, paired()).WillOnce(Return(true));
  // Return true to avoid monitoring paired status changed event.
  EXPECT_CALL(mock_target_device_, paired()).WillOnce(Return(true));

  SetChangeAliasCall(/*is_success=*/true, /*expected_alias=*/"");
  SetRemoveDeviceCall();
  // Stop Discovery.
  SetStopDiscoveryCall(/*is_success=*/true);
  // Reset powered.
  SetUpGetAdaptersCall(/*adapters=*/{&mock_adapter_proxy_});
  SetChangePoweredCall(/*current_powered=*/true, /*target_powered=*/false);

  routine_->Start();
  CheckRoutineUpdate(100, mojom::DiagnosticRoutineStatusEnum::kPassed,
                     kBluetoothRoutinePassedMessage, ConstructOutputDict());
}

// Test that the BluetoothPairingRoutine returns a kError status when it fails
// to power on the adapter.
TEST_F(BluezBluetoothPairingRoutineTest, FailedPowerOnAdapter) {
  InSequence s;
  // Pre-check.
  EXPECT_CALL(mock_adapter_proxy_, powered()).WillOnce(Return(false));
  // Failed to power on.
  SetChangePoweredCall(/*current_powered=*/false, /*target_powered=*/true,
                       /*is_success=*/false);
  // Reset powered.
  SetUpGetAdaptersCall(/*adapters=*/{&mock_adapter_proxy_});
  SetChangePoweredCall(/*current_powered=*/true, /*target_powered=*/false);

  routine_->Start();
  CheckRoutineUpdate(100, mojom::DiagnosticRoutineStatusEnum::kError,
                     kBluetoothRoutineFailedChangePowered);
}

// Test that the BluetoothPairingRoutine returns a kFailed status when it fails
// to remove cached peripheral.
TEST_F(BluezBluetoothPairingRoutineTest, FailedRemoveCachedPeripheral) {
  InSequence s;
  // Pre-check.
  EXPECT_CALL(mock_adapter_proxy_, powered()).WillOnce(Return(false));
  // Ensure adapter is powered on.
  SetChangePoweredCall(/*current_powered=*/false, /*target_powered=*/true);

  // Check cached devices.
  EXPECT_CALL(*mock_bluez_controller(), GetDevices())
      .WillOnce(Return(std::vector<org::bluez::Device1ProxyInterface*>{
          &mock_target_device_}));
  EXPECT_CALL(mock_target_device_, address())
      .WillOnce(ReturnRef(target_address_));
  EXPECT_CALL(mock_target_device_, alias()).WillOnce(ReturnRef(""));
  EXPECT_CALL(mock_target_device_, paired()).WillOnce(Return(false));

  // Failed to remove device.
  EXPECT_CALL(mock_target_device_, GetObjectPath())
      .WillOnce(ReturnRef(target_device_path_));
  EXPECT_CALL(mock_adapter_proxy_,
              RemoveDeviceAsync(target_device_path_, _, _, _))
      .WillOnce(base::test::RunOnceCallback<2>(nullptr));
  // Reset powered.
  SetUpGetAdaptersCall(/*adapters=*/{&mock_adapter_proxy_});
  SetChangePoweredCall(/*current_powered=*/true, /*target_powered=*/false);

  routine_->Start();
  CheckRoutineUpdate(100, mojom::DiagnosticRoutineStatusEnum::kFailed,
                     "Bluetooth routine failed to remove target peripheral.");
}

// Test that the BluetoothPairingRoutine returns a kFailed status when the
// target peripheral is already paired.
TEST_F(BluezBluetoothPairingRoutineTest, FailedPeripheralAlreadyPaired) {
  InSequence s;
  // Pre-check.
  EXPECT_CALL(mock_adapter_proxy_, powered()).WillOnce(Return(false));
  // Failed to power on.
  SetChangePoweredCall(/*current_powered=*/false, /*target_powered=*/true);

  // Check cached devices.
  EXPECT_CALL(*mock_bluez_controller(), GetDevices())
      .WillOnce(Return(std::vector<org::bluez::Device1ProxyInterface*>{
          &mock_target_device_}));
  EXPECT_CALL(mock_target_device_, address())
      .WillOnce(ReturnRef(target_address_));
  EXPECT_CALL(mock_target_device_, alias()).WillOnce(ReturnRef(""));
  EXPECT_CALL(mock_target_device_, paired()).WillOnce(Return(true));
  // Reset powered.
  SetUpGetAdaptersCall(/*adapters=*/{&mock_adapter_proxy_});
  SetChangePoweredCall(/*current_powered=*/true, /*target_powered=*/false);

  routine_->Start();
  CheckRoutineUpdate(100, mojom::DiagnosticRoutineStatusEnum::kFailed,
                     "The target peripheral is already paired.");
}

// Test that the BluetoothPairingRoutine returns a kError status when it fails
// to start discovery.
TEST_F(BluezBluetoothPairingRoutineTest, FailedStartDiscovery) {
  InSequence s;
  // Pre-check.
  EXPECT_CALL(mock_adapter_proxy_, powered()).WillOnce(Return(false));
  // Ensure adapter is powered on.
  SetChangePoweredCall(/*current_powered=*/false, /*target_powered=*/true);

  EXPECT_CALL(*mock_bluez_controller(), GetDevices())
      .WillOnce(Return(std::vector<org::bluez::Device1ProxyInterface*>{}));

  // Failed to start and stop discovery.
  SetStartDiscoveryCall(/*is_success=*/false, /*added_devices=*/{});
  SetStopDiscoveryCall(/*is_success=*/false);
  // Reset powered.
  SetUpGetAdaptersCall(/*adapters=*/{&mock_adapter_proxy_});
  SetChangePoweredCall(/*current_powered=*/true, /*target_powered=*/false);

  routine_->Start();
  CheckRoutineUpdate(100, mojom::DiagnosticRoutineStatusEnum::kError,
                     kBluetoothRoutineFailedSwitchDiscovery);
}

// Test that the BluetoothPairingRoutine returns a kFailed status when it fails
// to find target peripheral.
TEST_F(BluezBluetoothPairingRoutineTest, FailedFindTargetPeripheral) {
  InSequence s;
  // Pre-check.
  EXPECT_CALL(mock_adapter_proxy_, powered()).WillOnce(Return(false));
  // Ensure adapter is powered on.
  SetChangePoweredCall(/*current_powered=*/false, /*target_powered=*/true);

  EXPECT_CALL(*mock_bluez_controller(), GetDevices())
      .WillOnce(Return(std::vector<org::bluez::Device1ProxyInterface*>{}));

  // Start discovery but not sending target peripheral.
  SetStartDiscoveryCall(/*is_success=*/true, /*added_devices=*/{});
  // Stop Discovery.
  SetStopDiscoveryCall(/*is_success=*/true);
  // Reset powered.
  SetUpGetAdaptersCall(/*adapters=*/{&mock_adapter_proxy_});
  SetChangePoweredCall(/*current_powered=*/true, /*target_powered=*/false);

  routine_->Start();
  CheckRoutineUpdate(27, mojom::DiagnosticRoutineStatusEnum::kRunning,
                     kBluetoothRoutineRunningMessage);
  // Failed to find target peripheral before timeout.
  task_environment_.FastForwardBy(kPairingRoutineTimeout);
  CheckRoutineUpdate(100, mojom::DiagnosticRoutineStatusEnum::kFailed,
                     kBluetoothRoutineFailedFindTargetPeripheral);
}

// Test that the BluetoothPairingRoutine returns a kFailed status when it fails
// to set alias of target peripheral.
TEST_F(BluezBluetoothPairingRoutineTest, FailedTagTargetPeripheral) {
  InSequence s;
  // Pre-check.
  EXPECT_CALL(mock_adapter_proxy_, powered()).WillOnce(Return(false));
  // Ensure adapter is powered on.
  SetChangePoweredCall(/*current_powered=*/false, /*target_powered=*/true);

  EXPECT_CALL(*mock_bluez_controller(), GetDevices())
      .WillOnce(Return(std::vector<org::bluez::Device1ProxyInterface*>{}));

  // Start discovery.
  SetStartDiscoveryCall(/*is_success=*/true,
                        /*added_devices=*/{&mock_target_device_});
  SetDeviceAddedCall();
  SetChangeAliasCall(/*is_success=*/false,
                     /*expected_alias=*/kHealthdBluetoothDiagnosticsTag);
  // Stop Discovery.
  SetStopDiscoveryCall(/*is_success=*/true);
  // Reset powered.
  SetUpGetAdaptersCall(/*adapters=*/{&mock_adapter_proxy_});
  SetChangePoweredCall(/*current_powered=*/true, /*target_powered=*/false);

  routine_->Start();
  CheckRoutineUpdate(100, mojom::DiagnosticRoutineStatusEnum::kFailed,
                     "Bluetooth routine failed to set target device's alias.",
                     ConstructOutputDict());
}

// Test that the BluetoothPairingRoutine returns a kFailed status when it fails
// to create baseband connection.
TEST_F(BluezBluetoothPairingRoutineTest, FailedCreateBasebandConnection) {
  InSequence s;
  // Pre-check.
  EXPECT_CALL(mock_adapter_proxy_, powered()).WillOnce(Return(false));
  // Ensure adapter is powered on.
  SetChangePoweredCall(/*current_powered=*/false, /*target_powered=*/true);

  EXPECT_CALL(*mock_bluez_controller(), GetDevices())
      .WillOnce(Return(std::vector<org::bluez::Device1ProxyInterface*>{}));

  // Start discovery.
  SetStartDiscoveryCall(/*is_success=*/true,
                        /*added_devices=*/{&mock_target_device_});
  SetDeviceAddedCall();
  SetChangeAliasCall(/*is_success=*/true,
                     /*expected_alias=*/kHealthdBluetoothDiagnosticsTag);

  // Failed to connect.
  auto error = brillo::Error::Create(
      FROM_HERE, /*domain=*/"", /*code=*/"org.bluez.Error.Failed",
      /*message=*/"br-connection-profile-unavailable");
  EXPECT_CALL(mock_target_device_, ConnectAsync(_, _, _))
      .WillOnce(
          WithArg<1>([&](base::OnceCallback<void(brillo::Error*)> on_error) {
            std::move(on_error).Run(error.get());
          }));

  // Stop Discovery.
  SetStopDiscoveryCall(/*is_success=*/true);
  // Reset powered.
  SetUpGetAdaptersCall(/*adapters=*/{&mock_adapter_proxy_});
  SetChangePoweredCall(/*current_powered=*/true, /*target_powered=*/false);

  routine_->Start();
  CheckRoutineUpdate(100, mojom::DiagnosticRoutineStatusEnum::kFailed,
                     kBluetoothRoutineFailedCreateBasebandConnection,
                     ConstructOutputDict(/*connect_error=*/error.get()));
}

// Test that the BluetoothPairingRoutine returns a kFailed status when it fails
// to verify connected status after connection.
TEST_F(BluezBluetoothPairingRoutineTest, FailedVerifyConnected) {
  InSequence s;
  // Pre-check.
  EXPECT_CALL(mock_adapter_proxy_, powered()).WillOnce(Return(false));
  // Ensure adapter is powered on.
  SetChangePoweredCall(/*current_powered=*/false, /*target_powered=*/true);

  EXPECT_CALL(*mock_bluez_controller(), GetDevices())
      .WillOnce(Return(std::vector<org::bluez::Device1ProxyInterface*>{}));

  // Start discovery.
  SetStartDiscoveryCall(/*is_success=*/true,
                        /*added_devices=*/{&mock_target_device_});
  SetDeviceAddedCall();
  SetChangeAliasCall(/*is_success=*/true,
                     /*expected_alias=*/kHealthdBluetoothDiagnosticsTag);
  // Failed to verify connected status.
  SetConnectDeviceCall(/*connected_result=*/false);
  // Stop Discovery.
  SetStopDiscoveryCall(/*is_success=*/true);
  // Reset powered.
  SetUpGetAdaptersCall(/*adapters=*/{&mock_adapter_proxy_});
  SetChangePoweredCall(/*current_powered=*/true, /*target_powered=*/false);

  routine_->Start();
  CheckRoutineUpdate(100, mojom::DiagnosticRoutineStatusEnum::kFailed,
                     kBluetoothRoutineFailedCreateBasebandConnection,
                     ConstructOutputDict());
}

// Test that the BluetoothPairingRoutine returns a kFailed status when it fails
// to pair.
TEST_F(BluezBluetoothPairingRoutineTest, FailedToPair) {
  InSequence s;
  // Pre-check.
  EXPECT_CALL(mock_adapter_proxy_, powered()).WillOnce(Return(false));
  // Ensure adapter is powered on.
  SetChangePoweredCall(/*current_powered=*/false, /*target_powered=*/true);

  EXPECT_CALL(*mock_bluez_controller(), GetDevices())
      .WillOnce(Return(std::vector<org::bluez::Device1ProxyInterface*>{}));

  // Start discovery.
  SetStartDiscoveryCall(/*is_success=*/true,
                        /*added_devices=*/{&mock_target_device_});
  SetDeviceAddedCall();
  SetChangeAliasCall(/*is_success=*/true,
                     /*expected_alias=*/kHealthdBluetoothDiagnosticsTag);
  SetConnectDeviceCall();

  // Failed to pair.
  EXPECT_CALL(mock_target_device_, paired()).WillOnce(Return(false));
  auto error = brillo::Error::Create(
      FROM_HERE, /*domain=*/"", /*code=*/"org.bluez.Error.AuthenticationFailed",
      /*message=*/"Authentication Failed");
  EXPECT_CALL(mock_target_device_, PairAsync(_, _, _))
      .WillOnce(
          WithArg<1>([&](base::OnceCallback<void(brillo::Error*)> on_error) {
            std::move(on_error).Run(error.get());
          }));

  // Stop Discovery.
  SetStopDiscoveryCall(/*is_success=*/true);
  // Reset powered.
  SetUpGetAdaptersCall(/*adapters=*/{&mock_adapter_proxy_});
  SetChangePoweredCall(/*current_powered=*/true, /*target_powered=*/false);

  routine_->Start();
  CheckRoutineUpdate(100, mojom::DiagnosticRoutineStatusEnum::kFailed,
                     kBluetoothRoutineFailedFinishPairing,
                     ConstructOutputDict(/*connect_error=*/nullptr,
                                         /*pair_error=*/error.get()));
}

// Test that the BluetoothPairingRoutine returns a kFailed status when it fails
// to verify paired status.
TEST_F(BluezBluetoothPairingRoutineTest, FailedVerifyPaired) {
  InSequence s;
  // Pre-check.
  EXPECT_CALL(mock_adapter_proxy_, powered()).WillOnce(Return(false));
  // Ensure adapter is powered on.
  SetChangePoweredCall(/*current_powered=*/false, /*target_powered=*/true);

  EXPECT_CALL(*mock_bluez_controller(), GetDevices())
      .WillOnce(Return(std::vector<org::bluez::Device1ProxyInterface*>{}));

  // Start discovery.
  SetStartDiscoveryCall(/*is_success=*/true,
                        /*added_devices=*/{&mock_target_device_});
  SetDeviceAddedCall();
  SetChangeAliasCall(/*is_success=*/true,
                     /*expected_alias=*/kHealthdBluetoothDiagnosticsTag);
  SetConnectDeviceCall();
  SetPairDeviceCall(/*paired_result=*/false);
  // Stop Discovery.
  SetStopDiscoveryCall(/*is_success=*/true);
  // Reset powered.
  SetUpGetAdaptersCall(/*adapters=*/{&mock_adapter_proxy_});
  SetChangePoweredCall(/*current_powered=*/true, /*target_powered=*/false);

  routine_->Start();
  CheckRoutineUpdate(100, mojom::DiagnosticRoutineStatusEnum::kFailed,
                     kBluetoothRoutineFailedFinishPairing,
                     ConstructOutputDict());
}

// Test that the BluetoothPairingRoutine returns a kFailed status when it fails
// to remove paired peripheral after pairing.
TEST_F(BluezBluetoothPairingRoutineTest, FailedRemovePairedPeripheral) {
  InSequence s;
  // Pre-check.
  EXPECT_CALL(mock_adapter_proxy_, powered()).WillOnce(Return(false));
  // Ensure adapter is powered on.
  SetChangePoweredCall(/*current_powered=*/false, /*target_powered=*/true);

  EXPECT_CALL(*mock_bluez_controller(), GetDevices())
      .WillOnce(Return(std::vector<org::bluez::Device1ProxyInterface*>{}));

  // Start discovery.
  SetStartDiscoveryCall(/*is_success=*/true,
                        /*added_devices=*/{&mock_target_device_});
  SetDeviceAddedCall();
  SetChangeAliasCall(/*is_success=*/true,
                     /*expected_alias=*/kHealthdBluetoothDiagnosticsTag);
  SetConnectDeviceCall();
  SetPairDeviceCall();
  SetChangeAliasCall(/*is_success=*/true, /*expected_alias=*/"");

  // Failed to remove device.
  EXPECT_CALL(mock_target_device_, GetObjectPath())
      .WillOnce(ReturnRef(target_device_path_));
  EXPECT_CALL(mock_adapter_proxy_, RemoveDeviceAsync(_, _, _, _))
      .WillOnce(WithArgs<0, 2>(
          [&](const dbus::ObjectPath& in_device,
              base::OnceCallback<void(brillo::Error*)> on_error) {
            EXPECT_EQ(in_device, target_device_path_);
            std::move(on_error).Run(nullptr);
          }));

  // Stop Discovery.
  SetStopDiscoveryCall(/*is_success=*/true);
  // Reset powered.
  SetUpGetAdaptersCall(/*adapters=*/{&mock_adapter_proxy_});
  SetChangePoweredCall(/*current_powered=*/true, /*target_powered=*/false);

  routine_->Start();
  CheckRoutineUpdate(100, mojom::DiagnosticRoutineStatusEnum::kFailed,
                     "Bluetooth routine failed to remove target peripheral.",
                     ConstructOutputDict());
}

// Test that the BluetoothPairingRoutine returns a kError status when it fails
// to stop discovery.
TEST_F(BluezBluetoothPairingRoutineTest, FailedStopDiscovery) {
  InSequence s;
  // Pre-check.
  EXPECT_CALL(mock_adapter_proxy_, powered()).WillOnce(Return(false));
  // Ensure adapter is powered on.
  SetChangePoweredCall(/*current_powered=*/false, /*target_powered=*/true);

  EXPECT_CALL(*mock_bluez_controller(), GetDevices())
      .WillOnce(Return(std::vector<org::bluez::Device1ProxyInterface*>{}));

  // Start discovery.
  SetStartDiscoveryCall(/*is_success=*/true,
                        /*added_devices=*/{&mock_target_device_});
  SetDeviceAddedCall();
  SetChangeAliasCall(/*is_success=*/true,
                     /*expected_alias=*/kHealthdBluetoothDiagnosticsTag);
  SetConnectDeviceCall();
  SetPairDeviceCall();
  SetChangeAliasCall(/*is_success=*/true, /*expected_alias=*/"");
  SetRemoveDeviceCall();

  // Failed to stop discovery.
  SetStopDiscoveryCall(/*is_success=*/false);
  // Reset powered.
  SetUpGetAdaptersCall(/*adapters=*/{&mock_adapter_proxy_});
  SetChangePoweredCall(/*current_powered=*/true, /*target_powered=*/false);

  routine_->Start();
  CheckRoutineUpdate(100, mojom::DiagnosticRoutineStatusEnum::kError,
                     kBluetoothRoutineFailedSwitchDiscovery,
                     ConstructOutputDict());
}

// Test that the BluetoothPairingRoutine returns a kError status when it fails
// to get adapter.
TEST_F(BluezBluetoothPairingRoutineTest, GetAdapterError) {
  SetUpNullAdapter();
  routine_->Start();
  CheckRoutineUpdate(100, mojom::DiagnosticRoutineStatusEnum::kError,
                     kBluetoothRoutineFailedGetAdapter);
}

// Test that the BluetoothPairingRoutine returns a kFailed status when the
// adapter is in discovery mode.
TEST_F(BluezBluetoothPairingRoutineTest, PreCheckFailed) {
  InSequence s;
  // Pre-check.
  EXPECT_CALL(mock_adapter_proxy_, powered()).WillOnce(Return(true));
  // The adapter is in discovery mode.
  EXPECT_CALL(mock_adapter_proxy_, discovering()).WillOnce(Return(true));

  routine_->Start();
  CheckRoutineUpdate(100, mojom::DiagnosticRoutineStatusEnum::kFailed,
                     kBluetoothRoutineFailedDiscoveryMode);
}

}  // namespace
}  // namespace diagnostics::bluez
