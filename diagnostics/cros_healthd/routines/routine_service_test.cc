// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include <base/test/bind.h>
#include <base/test/gmock_callback_support.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "diagnostics/base/file_test_utils.h"
#include "diagnostics/base/paths.h"
#include "diagnostics/cros_healthd/routines/base_routine_control.h"
#include "diagnostics/cros_healthd/routines/routine_service.h"
#include "diagnostics/cros_healthd/routines/routine_v2_test_utils.h"
#include "diagnostics/cros_healthd/system/cros_config_constants.h"
#include "diagnostics/cros_healthd/system/ground_truth_constants.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/dbus_bindings/bluetooth_manager/dbus-proxy-mocks.h"
#include "diagnostics/mojom/public/cros_healthd_exception.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics {
namespace {

namespace mojom = ::ash::cros_healthd::mojom;

using ::testing::_;
using ::testing::Return;
using ::testing::StrictMock;

class RoutineServiceTest : public BaseFileTest {
 protected:
  void CheckCreateRoutine(
      const mojom::SupportStatusPtr& expected_support_status,
      mojom::RoutineArgumentPtr routine_arg) {
    mojo::Remote<mojom::RoutineControl> control;
    FakeRoutineObserver observer;

    // Create receiver so we can set disconnect handler before we really call
    // `CreateRoutine`.
    auto control_receiver = control.BindNewPipeAndPassReceiver();
    std::optional<uint32_t> error;
    std::optional<std::string> message;
    control.set_disconnect_with_reason_handler(base::BindLambdaForTesting(
        [&](uint32_t error_in, const std::string& message_in) {
          error = error_in;
          message = message_in;
        }));
    base::test::TestFuture<mojom::RoutineStatePtr> get_state_future;
    // Call this first before actually call `CreateRoutine` to make sure it
    // never return if routine is not supported.
    control->GetState(get_state_future.GetCallback());

    routine_service_.CreateRoutine(
        std::move(routine_arg), std::move(control_receiver),
        observer.receiver().BindNewPipeAndPassRemote());

    // Flush all mojo pipe to run all async tasks.
    control.FlushForTesting();
    observer.receiver().FlushForTesting();

    if (expected_support_status->is_supported()) {
      // Check if routine initialize successfully and return initialized state.
      EXPECT_TRUE(control.is_connected());
      auto init_state = mojom::RoutineState::New(
          /*percentage=*/0, mojom::RoutineStateUnion::NewInitialized(
                                mojom::RoutineStateInitialized::New()));
      EXPECT_EQ(observer.last_routine_state(), init_state);
      EXPECT_EQ(get_state_future.Get(), init_state);
      return;
    }
    // Check if routine raise expected exception.
    EXPECT_FALSE(control.is_connected());
    EXPECT_FALSE(get_state_future.IsReady())
        << "Routine shouldn't return any state if it fails to initialize.";
    EXPECT_TRUE(observer.last_routine_state().is_null())
        << "Routine shouldn't return any state if it fails to initialize.";
    // Check we set correct disconnection error code.
    if (expected_support_status->is_unsupported()) {
      EXPECT_EQ(error,
                static_cast<uint32_t>(mojom::Exception::Reason::kUnsupported));
      EXPECT_EQ(message,
                expected_support_status->get_unsupported()->debug_message);
    } else if (expected_support_status->is_exception()) {
      EXPECT_EQ(error,
                static_cast<uint32_t>(mojom::Exception::Reason::kUnexpected));
      EXPECT_EQ(message,
                expected_support_status->get_exception()->debug_message);
    } else {
      EXPECT_TRUE(false) << "Don't expect to get other status type.";
    }
  }

  void CheckIsRoutineArgumentSupported(
      const mojom::SupportStatusPtr& expected_support_status,
      mojom::RoutineArgumentPtr routine_arg) {
    base::test::TestFuture<mojom::SupportStatusPtr> future;
    routine_service_.IsRoutineArgumentSupported(std::move(routine_arg),
                                                future.GetCallback());
    mojom::SupportStatusPtr support_status = future.Take();
    EXPECT_EQ(expected_support_status->which(), support_status->which());
    switch (support_status->which()) {
      case mojom::SupportStatus::Tag::kUnmappedUnionField:
        EXPECT_TRUE(false) << "Don't expect to get this.";
        break;
      case mojom::SupportStatus::Tag::kSupported:
        break;
      case mojom::SupportStatus::Tag::kUnsupported:
        EXPECT_EQ(expected_support_status->get_unsupported()->debug_message,
                  support_status->get_unsupported()->debug_message);
        break;
      case mojom::SupportStatus::Tag::kException:
        EXPECT_EQ(expected_support_status->get_exception()->debug_message,
                  support_status->get_exception()->debug_message);
        break;
    }
  }

  MockFlossController* mock_floss_controller() {
    return context_.mock_floss_controller();
  }

  void SetFloss(bool enable) {
    EXPECT_CALL(*mock_floss_controller(), GetManager())
        .WillRepeatedly(Return(&mock_manager_proxy_));
    EXPECT_CALL(mock_manager_proxy_, GetFlossEnabledAsync(_, _, _))
        .WillRepeatedly(base::test::RunOnceCallback<0>(enable));
  }

  base::test::TaskEnvironment task_environment_;
  MockContext context_;
  RoutineService routine_service_{&context_};
  StrictMock<org::chromium::bluetooth::ManagerProxyMock> mock_manager_proxy_;
};

mojom::SupportStatusPtr MakeSupported() {
  return mojom::SupportStatus::NewSupported(mojom::Supported::New());
}

mojom::SupportStatusPtr MakeUnsupported(const std::string& debug_message) {
  return mojom::SupportStatus::NewUnsupported(
      mojom::Unsupported::New(debug_message, /*reason=*/nullptr));
}

mojom::SupportStatusPtr MakeUnexpected(const std::string& debug_message) {
  return mojom::SupportStatus::NewException(mojom::Exception::New(
      mojom::Exception::Reason::kUnexpected, debug_message));
}

TEST_F(RoutineServiceTest, UnrecognizedArgument) {
  CheckIsRoutineArgumentSupported(
      MakeUnexpected("Got kUnrecognizedArgument"),
      mojom::RoutineArgument::NewUnrecognizedArgument(false));
  CheckCreateRoutine(
      MakeUnsupported("Routine Argument not recognized/supported"),
      mojom::RoutineArgument::NewUnrecognizedArgument(false));
}

TEST_F(RoutineServiceTest, PrimeSearch) {
  CheckIsRoutineArgumentSupported(
      MakeSupported(), mojom::RoutineArgument::NewPrimeSearch(
                           mojom::PrimeSearchRoutineArgument::New()));
  CheckCreateRoutine(MakeSupported(),
                     mojom::RoutineArgument::NewPrimeSearch(
                         mojom::PrimeSearchRoutineArgument::New()));
}

TEST_F(RoutineServiceTest, FloatingPoint) {
  CheckIsRoutineArgumentSupported(
      MakeSupported(), mojom::RoutineArgument::NewFloatingPoint(
                           mojom::FloatingPointRoutineArgument::New()));
  CheckCreateRoutine(MakeSupported(),
                     mojom::RoutineArgument::NewFloatingPoint(
                         mojom::FloatingPointRoutineArgument::New()));
}

TEST_F(RoutineServiceTest, Memory) {
  CheckIsRoutineArgumentSupported(
      MakeSupported(),
      mojom::RoutineArgument::NewMemory(mojom::MemoryRoutineArgument::New()));
  CheckCreateRoutine(MakeSupported(), mojom::RoutineArgument::NewMemory(
                                          mojom::MemoryRoutineArgument::New()));
}

TEST_F(RoutineServiceTest, AudioDriver) {
  CheckIsRoutineArgumentSupported(
      MakeSupported(), mojom::RoutineArgument::NewAudioDriver(
                           mojom::AudioDriverRoutineArgument::New()));
  CheckCreateRoutine(MakeSupported(),
                     mojom::RoutineArgument::NewAudioDriver(
                         mojom::AudioDriverRoutineArgument::New()));
}

TEST_F(RoutineServiceTest, CpuStress) {
  CheckIsRoutineArgumentSupported(MakeSupported(),
                                  mojom::RoutineArgument::NewCpuStress(
                                      mojom::CpuStressRoutineArgument::New()));
  CheckCreateRoutine(MakeSupported(),
                     mojom::RoutineArgument::NewCpuStress(
                         mojom::CpuStressRoutineArgument::New()));
}

TEST_F(RoutineServiceTest, UfsLifetime) {
  SetFakeCrosConfig(paths::cros_config::kStorageType,
                    cros_config_value::kStorageTypeUfs);

  CheckIsRoutineArgumentSupported(
      MakeSupported(), mojom::RoutineArgument::NewUfsLifetime(
                           mojom::UfsLifetimeRoutineArgument::New()));
  CheckCreateRoutine(MakeSupported(),
                     mojom::RoutineArgument::NewUfsLifetime(
                         mojom::UfsLifetimeRoutineArgument::New()));
}

TEST_F(RoutineServiceTest, UfsLifetimeWrongStorageType) {
  SetFakeCrosConfig(paths::cros_config::kStorageType, "WrongType");

  auto status = MakeUnsupported(
      "Expected cros_config property [hardware-properties/storage-type] to "
      "be [UFS], but got [WrongType]");

  CheckIsRoutineArgumentSupported(
      status, mojom::RoutineArgument::NewUfsLifetime(
                  mojom::UfsLifetimeRoutineArgument::New()));
  CheckCreateRoutine(status, mojom::RoutineArgument::NewUfsLifetime(
                                 mojom::UfsLifetimeRoutineArgument::New()));
}

TEST_F(RoutineServiceTest, DiskRead) {
  auto arg = mojom::DiskReadRoutineArgument::New();
  arg->type = mojom::DiskReadTypeEnum::kLinearRead;
  arg->disk_read_duration = base::Seconds(1);
  arg->file_size_mib = 1;

  CheckIsRoutineArgumentSupported(
      MakeSupported(), mojom::RoutineArgument::NewDiskRead(arg.Clone()));
  CheckCreateRoutine(MakeSupported(),
                     mojom::RoutineArgument::NewDiskRead(arg.Clone()));
}

TEST_F(RoutineServiceTest, DiskReadRoutineUnknownType) {
  auto arg = mojom::DiskReadRoutineArgument::New();
  arg->type = mojom::DiskReadTypeEnum::kUnmappedEnumField;
  arg->disk_read_duration = base::Seconds(1);
  arg->file_size_mib = 1;

  auto status = MakeUnsupported("Unexpected disk read type");

  CheckIsRoutineArgumentSupported(
      status, mojom::RoutineArgument::NewDiskRead(arg.Clone()));
  CheckCreateRoutine(status, mojom::RoutineArgument::NewDiskRead(arg.Clone()));
}

TEST_F(RoutineServiceTest, DiskReadRoutineZeroDuration) {
  auto arg = mojom::DiskReadRoutineArgument::New();
  arg->type = mojom::DiskReadTypeEnum::kLinearRead;
  arg->disk_read_duration = base::Seconds(0);
  arg->file_size_mib = 1;

  auto status = MakeUnsupported(
      "Disk read duration should not be zero after rounding towards zero to "
      "the nearest second");

  CheckIsRoutineArgumentSupported(
      status, mojom::RoutineArgument::NewDiskRead(arg.Clone()));
  CheckCreateRoutine(status, mojom::RoutineArgument::NewDiskRead(arg.Clone()));
}

TEST_F(RoutineServiceTest, DiskReadRoutineZeroFileSize) {
  auto arg = mojom::DiskReadRoutineArgument::New();
  arg->type = mojom::DiskReadTypeEnum::kLinearRead;
  arg->disk_read_duration = base::Seconds(1);
  arg->file_size_mib = 0;

  auto status = MakeUnsupported("Test file size should not be zero");

  CheckIsRoutineArgumentSupported(
      status, mojom::RoutineArgument::NewDiskRead(arg.Clone()));
  CheckCreateRoutine(status, mojom::RoutineArgument::NewDiskRead(arg.Clone()));
}

TEST_F(RoutineServiceTest, CpuCache) {
  CheckIsRoutineArgumentSupported(MakeSupported(),
                                  mojom::RoutineArgument::NewCpuCache(
                                      mojom::CpuCacheRoutineArgument::New()));
  CheckCreateRoutine(MakeSupported(),
                     mojom::RoutineArgument::NewCpuCache(
                         mojom::CpuCacheRoutineArgument::New()));
}

TEST_F(RoutineServiceTest, VolumeButton) {
  SetFakeCrosConfig(paths::cros_config::kHasSideVolumeButton,
                    cros_config_value::kTrue);

  CheckIsRoutineArgumentSupported(
      MakeSupported(), mojom::RoutineArgument::NewVolumeButton(
                           mojom::VolumeButtonRoutineArgument::New()));
  CheckCreateRoutine(MakeSupported(),
                     mojom::RoutineArgument::NewVolumeButton(
                         mojom::VolumeButtonRoutineArgument::New()));
}

TEST_F(RoutineServiceTest, VolumeButtonNoButton) {
  SetFakeCrosConfig(paths::cros_config::kHasSideVolumeButton, std::nullopt);

  auto status = MakeUnsupported(
      "Expected cros_config property "
      "[hardware-properties/has-side-volume-button] to be "
      "[true], but got []");
  CheckIsRoutineArgumentSupported(
      status, mojom::RoutineArgument::NewVolumeButton(
                  mojom::VolumeButtonRoutineArgument::New()));
  CheckCreateRoutine(status, mojom::RoutineArgument::NewVolumeButton(
                                 mojom::VolumeButtonRoutineArgument::New()));
}

TEST_F(RoutineServiceTest, LedLitUp) {
  SetFile(kCrosEcSysPath, "");

  CheckIsRoutineArgumentSupported(MakeSupported(),
                                  mojom::RoutineArgument::NewLedLitUp(
                                      mojom::LedLitUpRoutineArgument::New()));
  CheckCreateRoutine(MakeSupported(),
                     mojom::RoutineArgument::NewLedLitUp(
                         mojom::LedLitUpRoutineArgument::New()));
}

TEST_F(RoutineServiceTest, LedLitUpNoEc) {
  UnsetPath(kCrosEcSysPath);

  auto status = MakeUnsupported("Not supported on a non-CrosEC device");
  CheckIsRoutineArgumentSupported(status,
                                  mojom::RoutineArgument::NewLedLitUp(
                                      mojom::LedLitUpRoutineArgument::New()));
  CheckCreateRoutine(status, mojom::RoutineArgument::NewLedLitUp(
                                 mojom::LedLitUpRoutineArgument::New()));
}

TEST_F(RoutineServiceTest, BluetoothPower) {
  SetFloss(true);

  CheckIsRoutineArgumentSupported(
      MakeSupported(), mojom::RoutineArgument::NewBluetoothPower(
                           mojom::BluetoothPowerRoutineArgument::New()));
  CheckCreateRoutine(MakeSupported(),
                     mojom::RoutineArgument::NewBluetoothPower(
                         mojom::BluetoothPowerRoutineArgument::New()));
}

TEST_F(RoutineServiceTest, BluetoothPowerFlossDisable) {
  SetFloss(false);

  auto status = MakeUnsupported("Floss is not enabled");
  CheckIsRoutineArgumentSupported(
      status, mojom::RoutineArgument::NewBluetoothPower(
                  mojom::BluetoothPowerRoutineArgument::New()));
  CheckCreateRoutine(status, mojom::RoutineArgument::NewBluetoothPower(
                                 mojom::BluetoothPowerRoutineArgument::New()));
}

TEST_F(RoutineServiceTest, BluetoothDiscovery) {
  SetFloss(true);

  CheckIsRoutineArgumentSupported(
      MakeSupported(), mojom::RoutineArgument::NewBluetoothDiscovery(
                           mojom::BluetoothDiscoveryRoutineArgument::New()));
  CheckCreateRoutine(MakeSupported(),
                     mojom::RoutineArgument::NewBluetoothDiscovery(
                         mojom::BluetoothDiscoveryRoutineArgument::New()));
}

TEST_F(RoutineServiceTest, BluetoothDiscoveryFlossDisable) {
  SetFloss(false);

  auto status = MakeUnsupported("Floss is not enabled");
  CheckIsRoutineArgumentSupported(
      status, mojom::RoutineArgument::NewBluetoothDiscovery(
                  mojom::BluetoothDiscoveryRoutineArgument::New()));
  CheckCreateRoutine(status,
                     mojom::RoutineArgument::NewBluetoothDiscovery(
                         mojom::BluetoothDiscoveryRoutineArgument::New()));
}

TEST_F(RoutineServiceTest, Fan) {
  SetFakeCrosConfig(paths::cros_config::kFanCount, "1");

  CheckIsRoutineArgumentSupported(
      MakeSupported(),
      mojom::RoutineArgument::NewFan(mojom::FanRoutineArgument::New()));
  CheckCreateRoutine(MakeSupported(), mojom::RoutineArgument::NewFan(
                                          mojom::FanRoutineArgument::New()));
}

TEST_F(RoutineServiceTest, FanNoCrosConfig) {
  SetFakeCrosConfig(paths::cros_config::kFanCount, std::nullopt);

  auto status = MakeUnsupported(
      "Expected cros_config property [hardware-properties/fan-count] to be "
      "[uint8], but got []");
  CheckIsRoutineArgumentSupported(
      status, mojom::RoutineArgument::NewFan(mojom::FanRoutineArgument::New()));
  CheckCreateRoutine(
      status, mojom::RoutineArgument::NewFan(mojom::FanRoutineArgument::New()));
}

TEST_F(RoutineServiceTest, FanNoFan) {
  SetFakeCrosConfig(paths::cros_config::kFanCount, "0");

  auto status = MakeUnsupported("Doesn't support device with no fan.");
  CheckIsRoutineArgumentSupported(
      status, mojom::RoutineArgument::NewFan(mojom::FanRoutineArgument::New()));
  CheckCreateRoutine(
      status, mojom::RoutineArgument::NewFan(mojom::FanRoutineArgument::New()));
}

TEST_F(RoutineServiceTest, BluetoothScanning) {
  SetFloss(true);

  CheckIsRoutineArgumentSupported(
      MakeSupported(), mojom::RoutineArgument::NewBluetoothScanning(
                           mojom::BluetoothScanningRoutineArgument::New()));
  CheckCreateRoutine(MakeSupported(),
                     mojom::RoutineArgument::NewBluetoothScanning(
                         mojom::BluetoothScanningRoutineArgument::New()));
}

TEST_F(RoutineServiceTest, BluetoothScanningFlossDisable) {
  SetFloss(false);

  auto status = MakeUnsupported("Floss is not enabled");
  CheckIsRoutineArgumentSupported(
      status, mojom::RoutineArgument::NewBluetoothScanning(
                  mojom::BluetoothScanningRoutineArgument::New()));
  CheckCreateRoutine(status,
                     mojom::RoutineArgument::NewBluetoothScanning(
                         mojom::BluetoothScanningRoutineArgument::New()));
}

TEST_F(RoutineServiceTest, BluetoothScanningPositiveDuration) {
  SetFloss(true);

  auto arg = mojom::BluetoothScanningRoutineArgument::New();
  arg->exec_duration = base::Seconds(5);
  CheckIsRoutineArgumentSupported(
      MakeSupported(),
      mojom::RoutineArgument::NewBluetoothScanning(arg.Clone()));
  CheckCreateRoutine(MakeSupported(),
                     mojom::RoutineArgument::NewBluetoothScanning(arg.Clone()));
}

TEST_F(RoutineServiceTest, BluetoothScanningZeroDuration) {
  auto arg = mojom::BluetoothScanningRoutineArgument::New();
  arg->exec_duration = base::Seconds(0);

  auto status = MakeUnsupported(
      "Execution duration should be strictly greater than zero");
  CheckIsRoutineArgumentSupported(
      status, mojom::RoutineArgument::NewBluetoothScanning(arg.Clone()));
  CheckCreateRoutine(status,
                     mojom::RoutineArgument::NewBluetoothScanning(arg.Clone()));
}

TEST_F(RoutineServiceTest, BluetoothScanningNullDuration) {
  SetFloss(true);

  auto arg = mojom::BluetoothScanningRoutineArgument::New();
  CheckIsRoutineArgumentSupported(
      MakeSupported(),
      mojom::RoutineArgument::NewBluetoothScanning(arg.Clone()));
  CheckCreateRoutine(MakeSupported(),
                     mojom::RoutineArgument::NewBluetoothScanning(arg.Clone()));
}

TEST_F(RoutineServiceTest, BluetoothPairing) {
  SetFloss(true);

  CheckIsRoutineArgumentSupported(
      MakeSupported(), mojom::RoutineArgument::NewBluetoothPairing(
                           mojom::BluetoothPairingRoutineArgument::New()));
  CheckCreateRoutine(MakeSupported(),
                     mojom::RoutineArgument::NewBluetoothPairing(
                         mojom::BluetoothPairingRoutineArgument::New()));
}

TEST_F(RoutineServiceTest, BluetoothPairingFlossDisable) {
  SetFloss(false);

  auto status = MakeUnsupported("Floss is not enabled");
  CheckIsRoutineArgumentSupported(
      status, mojom::RoutineArgument::NewBluetoothPairing(
                  mojom::BluetoothPairingRoutineArgument::New()));
  CheckCreateRoutine(status,
                     mojom::RoutineArgument::NewBluetoothPairing(
                         mojom::BluetoothPairingRoutineArgument::New()));
}

}  // namespace
}  // namespace diagnostics
