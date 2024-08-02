// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetch_aggregator.h"

#include <vector>

#include <base/test/gmock_callback_support.h>
#include <base/test/test_future.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diagnostics/cros_healthd/fetch_delegate.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {
namespace {

namespace mojom = ::ash::cros_healthd::mojom;

using ::testing::_;
using ::testing::Return;

class MockFetchDelegate : public FetchDelegate {
 public:
  MockFetchDelegate() = default;
  MockFetchDelegate(const MockFetchDelegate&) = delete;
  MockFetchDelegate& operator=(const MockFetchDelegate&) = delete;
  ~MockFetchDelegate() override = default;

  // FetchDelegate overrides:
  MOCK_METHOD(void,
              FetchAudioResult,
              (base::OnceCallback<void(mojom::AudioResultPtr)> callback),
              (override));
  MOCK_METHOD(
      void,
      FetchAudioHardwareResult,
      (base::OnceCallback<void(mojom::AudioHardwareResultPtr)> callback),
      (override));
  MOCK_METHOD(mojom::BacklightResultPtr, FetchBacklightResult, (), (override));
  MOCK_METHOD(void,
              FetchBatteryResult,
              (base::OnceCallback<void(mojom::BatteryResultPtr)> callback),
              (override));
  MOCK_METHOD(void,
              FetchBluetoothResult,
              (base::OnceCallback<void(mojom::BluetoothResultPtr)> callback),
              (override));
  MOCK_METHOD(
      void,
      FetchBootPerformanceResult,
      (base::OnceCallback<void(mojom::BootPerformanceResultPtr)> callback),
      (override));
  MOCK_METHOD(void,
              FetchBusResult,
              (base::OnceCallback<void(mojom::BusResultPtr)> callback),
              (override));
  MOCK_METHOD(void,
              FetchCpuResult,
              (base::OnceCallback<void(mojom::CpuResultPtr)> callback),
              (override));
  MOCK_METHOD(void,
              FetchDisplayResult,
              (base::OnceCallback<void(mojom::DisplayResultPtr)> callback),
              (override));
  MOCK_METHOD(void,
              FetchFanResult,
              (base::OnceCallback<void(mojom::FanResultPtr)> callback),
              (override));
  MOCK_METHOD(void,
              FetchGraphicsResult,
              (base::OnceCallback<void(mojom::GraphicsResultPtr)> callback),
              (override));
  MOCK_METHOD(void,
              FetchInputResult,
              (base::OnceCallback<void(mojom::InputResultPtr)> callback),
              (override));
  MOCK_METHOD(void,
              FetchMemoryResult,
              (base::OnceCallback<void(mojom::MemoryResultPtr)> callback),
              (override));
  MOCK_METHOD(void,
              FetchNetworkResult,
              (base::OnceCallback<void(mojom::NetworkResultPtr)> callback),
              (override));
  MOCK_METHOD(
      void,
      FetchNetworkInterfaceResult,
      (base::OnceCallback<void(mojom::NetworkInterfaceResultPtr)> callback),
      (override));
  MOCK_METHOD(mojom::NonRemovableBlockDeviceResultPtr,
              FetchNonRemovableBlockDevicesResult,
              (),
              (override));
  MOCK_METHOD(void,
              FetchSensorResult,
              (base::OnceCallback<void(mojom::SensorResultPtr)> callback),
              (override));
  MOCK_METHOD(
      void,
      FetchStatefulPartitionResult,
      (base::OnceCallback<void(mojom::StatefulPartitionResultPtr)> callback),
      (override));
  MOCK_METHOD(void,
              FetchSystemResult,
              (base::OnceCallback<void(mojom::SystemResultPtr)> callback),
              (override));
  MOCK_METHOD(void,
              FetchThermalResult,
              (base::OnceCallback<void(mojom::ThermalResultPtr)> callback),
              (override));
  MOCK_METHOD(mojom::TimezoneResultPtr, FetchTimezoneResult, (), (override));
  MOCK_METHOD(void,
              FetchTpmResult,
              (base::OnceCallback<void(mojom::TpmResultPtr)> callback),
              (override));
};

class FetchAggregatorTest : public ::testing::Test {
 public:
  FetchAggregatorTest(const FetchAggregatorTest&) = delete;
  FetchAggregatorTest& operator=(const FetchAggregatorTest&) = delete;

 protected:
  FetchAggregatorTest() = default;

  mojom::TelemetryInfoPtr FetchSync(
      const std::vector<mojom::ProbeCategoryEnum>& categories) {
    base::test::TestFuture<mojom::TelemetryInfoPtr> future;
    fetch_aggregator.Run(categories, future.GetCallback());
    return future.Take();
  }

  MockFetchDelegate& mock_delegate() { return mock_delegate_; }

 private:
  ::testing::StrictMock<MockFetchDelegate> mock_delegate_;
  FetchAggregator fetch_aggregator{&mock_delegate_};
};

TEST_F(FetchAggregatorTest, ProbeNoCategory) {
  auto result = FetchSync({});
  EXPECT_TRUE(result);
}

TEST_F(FetchAggregatorTest, ProbeUnknownCategory) {
  auto result = FetchSync({mojom::ProbeCategoryEnum::kUnknown});
  EXPECT_TRUE(result);
}

TEST_F(FetchAggregatorTest, ProbeDuplicatedCategories) {
  EXPECT_CALL(mock_delegate(), FetchAudioResult(_))
      .WillOnce(
          base::test::RunOnceCallback<0>(mojom::AudioResult::NewAudioInfo({})));

  auto result = FetchSync(
      {mojom::ProbeCategoryEnum::kAudio, mojom::ProbeCategoryEnum::kAudio});
  ASSERT_TRUE(result);
  ASSERT_TRUE(result->audio_result);
  EXPECT_TRUE(result->audio_result->is_audio_info());
}

TEST_F(FetchAggregatorTest, ProbeMultipleCategories) {
  EXPECT_CALL(mock_delegate(), FetchAudioResult(_))
      .WillOnce(
          base::test::RunOnceCallback<0>(mojom::AudioResult::NewAudioInfo({})));
  EXPECT_CALL(mock_delegate(), FetchBatteryResult(_))
      .WillOnce(base::test::RunOnceCallback<0>(
          mojom::BatteryResult::NewBatteryInfo({})));

  auto result = FetchSync(
      {mojom::ProbeCategoryEnum::kAudio, mojom::ProbeCategoryEnum::kBattery});
  ASSERT_TRUE(result);
  ASSERT_TRUE(result->audio_result);
  EXPECT_TRUE(result->audio_result->is_audio_info());
  ASSERT_TRUE(result->battery_result);
  EXPECT_TRUE(result->battery_result->is_battery_info());
}

// Verify the outer callback is called even if the underlying fetcher drops the
// callback for a category.
TEST_F(FetchAggregatorTest, CallbackForCategoryDropped) {
  EXPECT_CALL(mock_delegate(), FetchAudioResult(_));

  auto result = FetchSync({mojom::ProbeCategoryEnum::kAudio});
  ASSERT_TRUE(result);
  ASSERT_TRUE(result->audio_result);
  EXPECT_TRUE(result->audio_result->is_error());
}

TEST_F(FetchAggregatorTest, ProbeAudio) {
  EXPECT_CALL(mock_delegate(), FetchAudioResult(_))
      .WillOnce(
          base::test::RunOnceCallback<0>(mojom::AudioResult::NewAudioInfo({})));

  auto result = FetchSync({mojom::ProbeCategoryEnum::kAudio});
  ASSERT_TRUE(result);
  ASSERT_TRUE(result->audio_result);
  EXPECT_TRUE(result->audio_result->is_audio_info());
}

TEST_F(FetchAggregatorTest, ProbeAudioHardware) {
  EXPECT_CALL(mock_delegate(), FetchAudioHardwareResult(_))
      .WillOnce(base::test::RunOnceCallback<0>(
          mojom::AudioHardwareResult::NewAudioHardwareInfo({})));

  auto result = FetchSync({mojom::ProbeCategoryEnum::kAudioHardware});
  ASSERT_TRUE(result);
  ASSERT_TRUE(result->audio_hardware_result);
  EXPECT_TRUE(result->audio_hardware_result->is_audio_hardware_info());
}

TEST_F(FetchAggregatorTest, ProbeBacklight) {
  EXPECT_CALL(mock_delegate(), FetchBacklightResult())
      .WillOnce(Return(mojom::BacklightResult::NewBacklightInfo({})));

  auto result = FetchSync({mojom::ProbeCategoryEnum::kBacklight});
  ASSERT_TRUE(result);
  ASSERT_TRUE(result->backlight_result);
  EXPECT_TRUE(result->backlight_result->is_backlight_info());
}

TEST_F(FetchAggregatorTest, ProbeBattery) {
  EXPECT_CALL(mock_delegate(), FetchBatteryResult(_))
      .WillOnce(base::test::RunOnceCallback<0>(
          mojom::BatteryResult::NewBatteryInfo({})));

  auto result = FetchSync({mojom::ProbeCategoryEnum::kBattery});
  ASSERT_TRUE(result);
  ASSERT_TRUE(result->battery_result);
  EXPECT_TRUE(result->battery_result->is_battery_info());
}

TEST_F(FetchAggregatorTest, ProbeBootPerformance) {
  EXPECT_CALL(mock_delegate(), FetchBootPerformanceResult(_))
      .WillOnce(base::test::RunOnceCallback<0>(
          mojom::BootPerformanceResult::NewBootPerformanceInfo({})));

  auto result = FetchSync({mojom::ProbeCategoryEnum::kBootPerformance});
  ASSERT_TRUE(result);
  ASSERT_TRUE(result->boot_performance_result);
  EXPECT_TRUE(result->boot_performance_result->is_boot_performance_info());
}

TEST_F(FetchAggregatorTest, ProbeBluetooth) {
  EXPECT_CALL(mock_delegate(), FetchBluetoothResult(_))
      .WillOnce(base::test::RunOnceCallback<0>(
          mojom::BluetoothResult::NewBluetoothAdapterInfo({})));

  auto result = FetchSync({mojom::ProbeCategoryEnum::kBluetooth});
  ASSERT_TRUE(result);
  ASSERT_TRUE(result->bluetooth_result);
  EXPECT_TRUE(result->bluetooth_result->is_bluetooth_adapter_info());
}

TEST_F(FetchAggregatorTest, ProbeBus) {
  EXPECT_CALL(mock_delegate(), FetchBusResult(_))
      .WillOnce(
          base::test::RunOnceCallback<0>(mojom::BusResult::NewBusDevices({})));

  auto result = FetchSync({mojom::ProbeCategoryEnum::kBus});
  ASSERT_TRUE(result);
  ASSERT_TRUE(result->bus_result);
  EXPECT_TRUE(result->bus_result->is_bus_devices());
}

TEST_F(FetchAggregatorTest, ProbeCpu) {
  EXPECT_CALL(mock_delegate(), FetchCpuResult(_))
      .WillOnce(
          base::test::RunOnceCallback<0>(mojom::CpuResult::NewCpuInfo({})));

  auto result = FetchSync({mojom::ProbeCategoryEnum::kCpu});
  ASSERT_TRUE(result);
  ASSERT_TRUE(result->cpu_result);
  EXPECT_TRUE(result->cpu_result->is_cpu_info());
}

TEST_F(FetchAggregatorTest, ProbeDisplay) {
  EXPECT_CALL(mock_delegate(), FetchDisplayResult(_))
      .WillOnce(base::test::RunOnceCallback<0>(
          mojom::DisplayResult::NewDisplayInfo({})));

  auto result = FetchSync({mojom::ProbeCategoryEnum::kDisplay});
  ASSERT_TRUE(result);
  ASSERT_TRUE(result->display_result);
  EXPECT_TRUE(result->display_result->is_display_info());
}

TEST_F(FetchAggregatorTest, ProbeFan) {
  EXPECT_CALL(mock_delegate(), FetchFanResult(_))
      .WillOnce(
          base::test::RunOnceCallback<0>(mojom::FanResult::NewFanInfo({})));

  auto result = FetchSync({mojom::ProbeCategoryEnum::kFan});
  ASSERT_TRUE(result);
  ASSERT_TRUE(result->fan_result);
  EXPECT_TRUE(result->fan_result->is_fan_info());
}

TEST_F(FetchAggregatorTest, ProbeGraphics) {
  EXPECT_CALL(mock_delegate(), FetchGraphicsResult(_))
      .WillOnce(base::test::RunOnceCallback<0>(
          mojom::GraphicsResult::NewGraphicsInfo({})));

  auto result = FetchSync({mojom::ProbeCategoryEnum::kGraphics});
  ASSERT_TRUE(result);
  ASSERT_TRUE(result->graphics_result);
  EXPECT_TRUE(result->graphics_result->is_graphics_info());
}

TEST_F(FetchAggregatorTest, ProbeInput) {
  EXPECT_CALL(mock_delegate(), FetchInputResult(_))
      .WillOnce(
          base::test::RunOnceCallback<0>(mojom::InputResult::NewInputInfo({})));

  auto result = FetchSync({mojom::ProbeCategoryEnum::kInput});
  ASSERT_TRUE(result);
  ASSERT_TRUE(result->input_result);
  EXPECT_TRUE(result->input_result->is_input_info());
}

TEST_F(FetchAggregatorTest, ProbeMemory) {
  EXPECT_CALL(mock_delegate(), FetchMemoryResult(_))
      .WillOnce(base::test::RunOnceCallback<0>(
          mojom::MemoryResult::NewMemoryInfo({})));

  auto result = FetchSync({mojom::ProbeCategoryEnum::kMemory});
  ASSERT_TRUE(result);
  ASSERT_TRUE(result->memory_result);
  EXPECT_TRUE(result->memory_result->is_memory_info());
}

TEST_F(FetchAggregatorTest, ProbeNetwork) {
  EXPECT_CALL(mock_delegate(), FetchNetworkResult(_))
      .WillOnce(base::test::RunOnceCallback<0>(
          mojom::NetworkResult::NewNetworkHealth({})));

  auto result = FetchSync({mojom::ProbeCategoryEnum::kNetwork});
  ASSERT_TRUE(result);
  ASSERT_TRUE(result->network_result);
  EXPECT_TRUE(result->network_result->is_network_health());
}

TEST_F(FetchAggregatorTest, ProbeNetworkInterface) {
  EXPECT_CALL(mock_delegate(), FetchNetworkInterfaceResult(_))
      .WillOnce(base::test::RunOnceCallback<0>(
          mojom::NetworkInterfaceResult::NewNetworkInterfaceInfo({})));

  auto result = FetchSync({mojom::ProbeCategoryEnum::kNetworkInterface});
  ASSERT_TRUE(result);
  ASSERT_TRUE(result->network_interface_result);
  EXPECT_TRUE(result->network_interface_result->is_network_interface_info());
}

TEST_F(FetchAggregatorTest, ProbeNonRemovableBlockDevices) {
  EXPECT_CALL(mock_delegate(), FetchNonRemovableBlockDevicesResult())
      .WillOnce(
          Return(mojom::NonRemovableBlockDeviceResult::NewBlockDeviceInfo({})));

  auto result =
      FetchSync({mojom::ProbeCategoryEnum::kNonRemovableBlockDevices});
  ASSERT_TRUE(result);
  ASSERT_TRUE(result->block_device_result);
  EXPECT_TRUE(result->block_device_result->is_block_device_info());
}

TEST_F(FetchAggregatorTest, ProbeSensor) {
  EXPECT_CALL(mock_delegate(), FetchSensorResult(_))
      .WillOnce(base::test::RunOnceCallback<0>(
          mojom::SensorResult::NewSensorInfo({})));

  auto result = FetchSync({mojom::ProbeCategoryEnum::kSensor});
  ASSERT_TRUE(result);
  ASSERT_TRUE(result->sensor_result);
  EXPECT_TRUE(result->sensor_result->is_sensor_info());
}

TEST_F(FetchAggregatorTest, ProbeStatefulPartition) {
  EXPECT_CALL(mock_delegate(), FetchStatefulPartitionResult(_))
      .WillOnce(base::test::RunOnceCallback<0>(
          mojom::StatefulPartitionResult::NewPartitionInfo({})));

  auto result = FetchSync({mojom::ProbeCategoryEnum::kStatefulPartition});
  ASSERT_TRUE(result);
  ASSERT_TRUE(result->stateful_partition_result);
  EXPECT_TRUE(result->stateful_partition_result->is_partition_info());
}

TEST_F(FetchAggregatorTest, ProbeSystem) {
  EXPECT_CALL(mock_delegate(), FetchSystemResult(_))
      .WillOnce(base::test::RunOnceCallback<0>(
          mojom::SystemResult::NewSystemInfo({})));

  auto result = FetchSync({mojom::ProbeCategoryEnum::kSystem});
  ASSERT_TRUE(result);
  ASSERT_TRUE(result->system_result);
  EXPECT_TRUE(result->system_result->is_system_info());
}

TEST_F(FetchAggregatorTest, ProbeThermal) {
  EXPECT_CALL(mock_delegate(), FetchThermalResult(_))
      .WillOnce(base::test::RunOnceCallback<0>(
          mojom::ThermalResult::NewThermalInfo({})));

  auto result = FetchSync({mojom::ProbeCategoryEnum::kThermal});
  ASSERT_TRUE(result);
  ASSERT_TRUE(result->thermal_result);
  EXPECT_TRUE(result->thermal_result->is_thermal_info());
}

TEST_F(FetchAggregatorTest, ProbeTimezone) {
  EXPECT_CALL(mock_delegate(), FetchTimezoneResult())
      .WillOnce(Return(mojom::TimezoneResult::NewTimezoneInfo({})));

  auto result = FetchSync({mojom::ProbeCategoryEnum::kTimezone});
  ASSERT_TRUE(result);
  ASSERT_TRUE(result->timezone_result);
  EXPECT_TRUE(result->timezone_result->is_timezone_info());
}

TEST_F(FetchAggregatorTest, ProbeTpm) {
  EXPECT_CALL(mock_delegate(), FetchTpmResult(_))
      .WillOnce(
          base::test::RunOnceCallback<0>(mojom::TpmResult::NewTpmInfo({})));

  auto result = FetchSync({mojom::ProbeCategoryEnum::kTpm});
  ASSERT_TRUE(result);
  ASSERT_TRUE(result->tpm_result);
  EXPECT_TRUE(result->tpm_result->is_tpm_info());
}

}  // namespace
}  // namespace diagnostics
