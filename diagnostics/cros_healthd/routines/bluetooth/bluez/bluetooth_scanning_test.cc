// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/bluetooth/bluez/bluetooth_scanning.h"

#include <memory>
#include <optional>
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

class BluezBluetoothScanningRoutineTest : public testing::Test {
 protected:
  BluezBluetoothScanningRoutineTest() = default;
  BluezBluetoothScanningRoutineTest(const BluezBluetoothScanningRoutineTest&) =
      delete;
  BluezBluetoothScanningRoutineTest& operator=(
      const BluezBluetoothScanningRoutineTest&) = delete;

  void SetUp() override { SetUpRoutine(std::nullopt); }

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

  void SetUpRoutine(const std::optional<base::TimeDelta>& exec_duration) {
    SetUpGetAdaptersCall(/*adapters=*/{&mock_adapter_proxy_});
    routine_ = std::make_unique<BluetoothScanningRoutine>(&mock_context_,
                                                          exec_duration);
  }

  void SetUpNullAdapter() {
    SetUpGetAdaptersCall(/*adapters=*/{nullptr});
    routine_ = std::make_unique<BluetoothScanningRoutine>(&mock_context_,
                                                          std::nullopt);
  }

  // Change the powered from |current_powered| to |target_powered|.
  void SetChangePoweredCall(bool current_powered,
                            bool target_powered,
                            bool is_success = true) {
    EXPECT_CALL(mock_adapter_proxy_, powered())
        .WillOnce(Return(current_powered));
    if (current_powered != target_powered) {
      EXPECT_CALL(mock_adapter_proxy_, set_powered(_, _))
          .WillOnce(base::test::RunOnceCallback<1>(is_success));
    }
  }

  void SetSwitchDiscoveryCall() {
    EXPECT_CALL(mock_adapter_proxy_, StartDiscoveryAsync(_, _, _))
        .WillOnce(WithArg<0>([&](base::OnceCallback<void()> on_success) {
          std::move(on_success).Run();
          for (const auto& [device_path, device] : fake_devices_) {
            auto mock_device = mock_device_proxies_[device_path].get();
            fake_bluez_event_hub()->SendDeviceAdded(mock_device);
            // Send out the rest RSSIs.
            for (int i = 1; i < device.rssi_history.size(); i++) {
              fake_bluez_event_hub()->SendDevicePropertyChanged(
                  mock_device, mock_device->RSSIName());
            }
          }
        }));
    for (const auto& [device_path, device] : fake_devices_) {
      SetDeviceAddedCall(device_path, device);
      for (int i = 1; i < device.rssi_history.size(); i++) {
        SetDeviceRssiChangedCall(device_path, /*rssi=*/device.rssi_history[i]);
      }
    }
    EXPECT_CALL(mock_adapter_proxy_, StopDiscoveryAsync(_, _, _))
        .WillOnce(base::test::RunOnceCallback<0>());
  }

  void SetDeviceAddedCall(const dbus::ObjectPath& device_path,
                          const ScannedPeripheralDevice& device) {
    auto mock_device = mock_device_proxies_[device_path].get();

    // Function call in BluezEventHub::OnDeviceAdded.
    EXPECT_CALL(*mock_device, SetPropertyChangedCallback(_));

    EXPECT_CALL(*mock_device, GetObjectPath()).WillOnce(ReturnRef(device_path));
    // Address.
    EXPECT_CALL(*mock_device, address())
        .WillOnce(ReturnRef(device_addresses_[device_path]));
    // Name.
    if (device.name.has_value()) {
      EXPECT_CALL(*mock_device, is_name_valid()).WillOnce(Return(true));
      EXPECT_CALL(*mock_device, name())
          .WillOnce(ReturnRef(device.name.value()));
    } else {
      EXPECT_CALL(*mock_device, is_name_valid()).WillOnce(Return(false));
    }
    // RSSI history.
    if (!device.rssi_history.empty()) {
      EXPECT_CALL(*mock_device, is_rssi_valid()).WillOnce(Return(true));
      EXPECT_CALL(*mock_device, rssi())
          .WillOnce(Return(device.rssi_history[0]));
    } else {
      EXPECT_CALL(*mock_device, is_rssi_valid()).WillOnce(Return(false));
    }
  }

  void SetDeviceRssiChangedCall(const dbus::ObjectPath& device_path,
                                const int16_t& rssi) {
    auto mock_device = mock_device_proxies_[device_path].get();
    EXPECT_CALL(*mock_device, GetObjectPath()).WillOnce(ReturnRef(device_path));
    EXPECT_CALL(*mock_device, is_rssi_valid()).WillOnce(Return(true));
    EXPECT_CALL(*mock_device, rssi()).WillOnce(Return(rssi));
  }

  void SetScannedDeviceData(dbus::ObjectPath device_path,
                            std::string address,
                            std::optional<std::string> name,
                            std::vector<int16_t> rssi_history,
                            bool is_high_signal = true) {
    fake_devices_[device_path] = ScannedPeripheralDevice{
        .peripheral_id = base::NumberToString(base::FastHash(address)),
        .name = name,
        .rssi_history = rssi_history};
    device_addresses_[device_path] = address;
    mock_device_proxies_[device_path] =
        std::make_unique<StrictMock<org::bluez::Device1ProxyMock>>();
    is_high_signal_device[device_path] = is_high_signal;
  }

  base::Value::Dict ConstructOutputDict() {
    base::Value::List peripherals;
    for (const auto& [device_path, device] : fake_devices_) {
      base::Value::Dict peripheral;
      if (is_high_signal_device[device_path]) {
        peripheral.Set("peripheral_id", device.peripheral_id);
        if (device.name.has_value())
          peripheral.Set("name", device.name.value());
      }
      base::Value::List out_rssi_history;
      for (const auto& rssi : device.rssi_history)
        out_rssi_history.Append(rssi);
      peripheral.Set("rssi_history", std::move(out_rssi_history));
      peripherals.Append(std::move(peripheral));
    }
    base::Value::Dict output_dict;
    output_dict.Set("peripherals", std::move(peripherals));
    return output_dict;
  }

  void CheckRoutineUpdate(uint32_t progress_percent,
                          mojom::DiagnosticRoutineStatusEnum status,
                          std::string status_message) {
    routine_->PopulateStatusUpdate(&update_, true);
    EXPECT_EQ(update_.progress_percent, progress_percent);
    VerifyNonInteractiveUpdate(update_.routine_update_union, status,
                               status_message);
    EXPECT_EQ(
        ConstructOutputDict(),
        base::JSONReader::Read(GetStringFromValidReadOnlySharedMemoryMapping(
            std::move(update_.output))));
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  std::unique_ptr<DiagnosticRoutine> routine_;
  StrictMock<org::bluez::Adapter1ProxyMock> mock_adapter_proxy_;

 private:
  MockContext mock_context_;
  std::map<dbus::ObjectPath, ScannedPeripheralDevice> fake_devices_;
  std::map<dbus::ObjectPath, std::string> device_addresses_;
  std::map<dbus::ObjectPath, std::unique_ptr<org::bluez::Device1ProxyMock>>
      mock_device_proxies_;
  std::map<dbus::ObjectPath, bool> is_high_signal_device;
  mojom::RoutineUpdate update_{0, mojo::ScopedHandle(),
                               mojom::RoutineUpdateUnionPtr()};
};

// Test that the BluetoothScanningRoutine can be run successfully.
TEST_F(BluezBluetoothScanningRoutineTest, RoutineSuccess) {
  InSequence s;
  // Pre-check.
  EXPECT_CALL(mock_adapter_proxy_, powered()).WillOnce(Return(false));
  // Ensure adapter is powered on.
  SetChangePoweredCall(/*current_powered=*/false, /*target_powered=*/true);
  // Set up fake data.
  SetScannedDeviceData(dbus::ObjectPath("/org/bluez/dev_70_88_6B_92_34_70"),
                       /*address=*/"70:88:6B:92:34:70", /*name=*/"GID6B",
                       /*rssi_history=*/{-54, -56, -52});
  SetScannedDeviceData(dbus::ObjectPath("/org/bluez/dev_70_D6_9F_0B_4F_D8"),
                       /*address=*/"70:D6:9F:0B:4F:D8", /*name=*/std::nullopt,
                       /*rssi_history=*/{-54});
  // Low signal RSSI history.
  SetScannedDeviceData(
      dbus::ObjectPath("/org/bluez/dev_6F_92_B8_03_F3_4E"),
      /*address=*/"6F:92:B8:03:F3:4E", /*name=*/"Low signal device name",
      /*rssi_history=*/{kNearbyPeripheralMinimumAverageRssi - 1},
      /*is_high_signal=*/false);

  // Start scanning.
  SetSwitchDiscoveryCall();
  // Reset powered.
  SetUpGetAdaptersCall(/*adapters=*/{&mock_adapter_proxy_});
  SetChangePoweredCall(/*current_powered=*/true, /*target_powered=*/false);

  routine_->Start();
  CheckRoutineUpdate(60, mojom::DiagnosticRoutineStatusEnum::kRunning,
                     kBluetoothRoutineRunningMessage);
  task_environment_.FastForwardBy(kScanningRoutineDefaultRuntime);
  CheckRoutineUpdate(100, mojom::DiagnosticRoutineStatusEnum::kPassed,
                     kBluetoothRoutinePassedMessage);
}

// Test that the BluetoothScanningRoutine can be run successfully without
// scanned devices.
TEST_F(BluezBluetoothScanningRoutineTest, RoutineSuccessNoDevices) {
  InSequence s;
  // Pre-check.
  EXPECT_CALL(mock_adapter_proxy_, powered()).WillOnce(Return(false));
  // Ensure adapter is powered on.
  SetChangePoweredCall(/*current_powered=*/false, /*target_powered=*/true);
  // Start scanning.
  SetSwitchDiscoveryCall();
  // Reset powered.
  SetUpGetAdaptersCall(/*adapters=*/{&mock_adapter_proxy_});
  SetChangePoweredCall(/*current_powered=*/true, /*target_powered=*/false);

  routine_->Start();
  CheckRoutineUpdate(60, mojom::DiagnosticRoutineStatusEnum::kRunning,
                     kBluetoothRoutineRunningMessage);
  task_environment_.FastForwardBy(kScanningRoutineDefaultRuntime);
  CheckRoutineUpdate(100, mojom::DiagnosticRoutineStatusEnum::kPassed,
                     kBluetoothRoutinePassedMessage);
}

// Test that the BluetoothScanningRoutine returns a kError status when it
// fails to power on the adapter.
TEST_F(BluezBluetoothScanningRoutineTest, FailedPowerOnAdapter) {
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

// Test that the BluetoothScanningRoutine returns a kError status when it
// fails to start discovery.
TEST_F(BluezBluetoothScanningRoutineTest, FailedStartDiscovery) {
  InSequence s;
  // Pre-check.
  EXPECT_CALL(mock_adapter_proxy_, powered()).WillOnce(Return(false));
  // Ensure adapter is powered on.
  SetChangePoweredCall(/*current_powered=*/false, /*target_powered=*/true);
  // Failed to start discovery.
  EXPECT_CALL(mock_adapter_proxy_, StartDiscoveryAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<1>(nullptr));
  // Reset powered.
  SetUpGetAdaptersCall(/*adapters=*/{&mock_adapter_proxy_});
  SetChangePoweredCall(/*current_powered=*/true, /*target_powered=*/false);

  routine_->Start();
  CheckRoutineUpdate(100, mojom::DiagnosticRoutineStatusEnum::kError,
                     kBluetoothRoutineFailedSwitchDiscovery);
}

// Test that the BluetoothScanningRoutine returns a kFailed status when it
// fails to stop discovery.
TEST_F(BluezBluetoothScanningRoutineTest, FailedStopDiscovery) {
  InSequence s;
  // Pre-check.
  EXPECT_CALL(mock_adapter_proxy_, powered()).WillOnce(Return(false));
  // Ensure adapter is powered on.
  SetChangePoweredCall(/*current_powered=*/false, /*target_powered=*/true);
  // Start discovery.
  EXPECT_CALL(mock_adapter_proxy_, StartDiscoveryAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<0>());
  // Failed to stop discovery.
  EXPECT_CALL(mock_adapter_proxy_, StopDiscoveryAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<1>(nullptr));
  // Reset powered.
  SetUpGetAdaptersCall(/*adapters=*/{&mock_adapter_proxy_});
  SetChangePoweredCall(/*current_powered=*/true, /*target_powered=*/false);

  routine_->Start();
  CheckRoutineUpdate(60, mojom::DiagnosticRoutineStatusEnum::kRunning,
                     kBluetoothRoutineRunningMessage);
  task_environment_.FastForwardBy(kScanningRoutineDefaultRuntime);
  CheckRoutineUpdate(100, mojom::DiagnosticRoutineStatusEnum::kError,
                     kBluetoothRoutineFailedSwitchDiscovery);
}

// Test that the BluetoothScanningRoutine returns a kError status when it fails
// to get adapter.
TEST_F(BluezBluetoothScanningRoutineTest, GetAdapterError) {
  SetUpNullAdapter();
  routine_->Start();
  CheckRoutineUpdate(100, mojom::DiagnosticRoutineStatusEnum::kError,
                     kBluetoothRoutineFailedGetAdapter);
}

// Test that the BluetoothScanningRoutine returns a kFailed status when the
// adapter is in discovery mode.
TEST_F(BluezBluetoothScanningRoutineTest, PreCheckFailed) {
  InSequence s;
  // Pre-check.
  EXPECT_CALL(mock_adapter_proxy_, powered()).WillOnce(Return(true));
  // The adapter is in discovery mode.
  EXPECT_CALL(mock_adapter_proxy_, discovering()).WillOnce(Return(true));

  routine_->Start();
  CheckRoutineUpdate(100, mojom::DiagnosticRoutineStatusEnum::kFailed,
                     kBluetoothRoutineFailedDiscoveryMode);
}

// Test that the BluetoothPowerRoutine returns a kError status when timeout
// occurred.
TEST_F(BluezBluetoothScanningRoutineTest, RoutineTimeoutOccurred) {
  InSequence s;
  // Pre-check.
  EXPECT_CALL(mock_adapter_proxy_, powered()).WillOnce(Return(false));
  // Ensure adapter is powered on.
  SetChangePoweredCall(/*current_powered=*/false, /*target_powered=*/true);
  // Start discovery.
  EXPECT_CALL(mock_adapter_proxy_, StartDiscoveryAsync(_, _, _));
  // Reset powered.
  SetUpGetAdaptersCall(/*adapters=*/{&mock_adapter_proxy_});
  SetChangePoweredCall(/*current_powered=*/true, /*target_powered=*/false);

  routine_->Start();
  // Trigger timeout.
  task_environment_.FastForwardBy(kScanningRoutineDefaultRuntime +
                                  kScanningRoutineTimeout);
  CheckRoutineUpdate(100, mojom::DiagnosticRoutineStatusEnum::kError,
                     "Bluetooth routine failed to complete before timeout.");
}

// Test that the BluetoothScanningRoutine returns a kError status when the
// routine execution time is zero.
TEST_F(BluezBluetoothScanningRoutineTest, ZeroExecutionTimeError) {
  SetUpRoutine(base::Seconds(0));
  routine_->Start();
  CheckRoutineUpdate(
      100, mojom::DiagnosticRoutineStatusEnum::kError,
      "Routine execution time should be strictly greater than zero.");
}

}  // namespace
}  // namespace diagnostics::bluez
