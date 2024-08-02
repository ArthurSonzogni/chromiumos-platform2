// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetch_delegate_impl.h"

#include <utility>

#include <base/check.h>
#include <base/functional/callback.h>

#include "diagnostics/cros_healthd/fetchers/audio_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/audio_hardware_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/backlight_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/battery_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/bluetooth_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/bus_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/cpu_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/disk_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/fan_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/input_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/memory_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/network_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/network_interface_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/sensor_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/stateful_partition_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/system_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/thermal_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/timezone_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/tpm_fetcher.h"
#include "diagnostics/cros_healthd/mojom/executor.mojom.h"
#include "diagnostics/cros_healthd/system/context.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

}  // namespace

FetchDelegateImpl::FetchDelegateImpl(Context* context) : context_(context) {
  CHECK(context_);
}

FetchDelegateImpl::~FetchDelegateImpl() = default;

void FetchDelegateImpl::FetchAudioResult(
    base::OnceCallback<void(mojom::AudioResultPtr)> callback) {
  FetchAudioInfo(context_, std::move(callback));
}

void FetchDelegateImpl::FetchAudioHardwareResult(
    base::OnceCallback<void(mojom::AudioHardwareResultPtr)> callback) {
  FetchAudioHardwareInfo(context_, std::move(callback));
}

mojom::BacklightResultPtr FetchDelegateImpl::FetchBacklightResult() {
  return FetchBacklightInfo(context_);
}

void FetchDelegateImpl::FetchBatteryResult(
    base::OnceCallback<void(mojom::BatteryResultPtr)> callback) {
  FetchBatteryInfo(context_, std::move(callback));
}

void FetchDelegateImpl::FetchBluetoothResult(
    base::OnceCallback<void(mojom::BluetoothResultPtr)> callback) {
  FetchBluetoothInfo(context_, std::move(callback));
}

void FetchDelegateImpl::FetchBootPerformanceResult(
    base::OnceCallback<void(mojom::BootPerformanceResultPtr)> callback) {
  context_->executor()->FetchBootPerformance(std::move(callback));
}

void FetchDelegateImpl::FetchBusResult(
    base::OnceCallback<void(mojom::BusResultPtr)> callback) {
  FetchBusDevices(context_, std::move(callback));
}

void FetchDelegateImpl::FetchCpuResult(
    base::OnceCallback<void(mojom::CpuResultPtr)> callback) {
  FetchCpuInfo(context_, std::move(callback));
}

void FetchDelegateImpl::FetchDisplayResult(
    base::OnceCallback<void(mojom::DisplayResultPtr)> callback) {
  context_->executor()->FetchDisplayInfo(std::move(callback));
}

void FetchDelegateImpl::FetchFanResult(
    base::OnceCallback<void(mojom::FanResultPtr)> callback) {
  FetchFanInfo(context_, std::move(callback));
}

void FetchDelegateImpl::FetchGraphicsResult(
    base::OnceCallback<void(mojom::GraphicsResultPtr)> callback) {
  context_->executor()->FetchGraphicsInfo(std::move(callback));
}

void FetchDelegateImpl::FetchInputResult(
    base::OnceCallback<void(mojom::InputResultPtr)> callback) {
  FetchInputInfo(context_, std::move(callback));
}

void FetchDelegateImpl::FetchMemoryResult(
    base::OnceCallback<void(mojom::MemoryResultPtr)> callback) {
  FetchMemoryInfo(context_, std::move(callback));
}

void FetchDelegateImpl::FetchNetworkResult(
    base::OnceCallback<void(mojom::NetworkResultPtr)> callback) {
  FetchNetworkInfo(context_, std::move(callback));
}

void FetchDelegateImpl::FetchNetworkInterfaceResult(
    base::OnceCallback<void(mojom::NetworkInterfaceResultPtr)> callback) {
  FetchNetworkInterfaceInfo(context_, std::move(callback));
}

mojom::NonRemovableBlockDeviceResultPtr
FetchDelegateImpl::FetchNonRemovableBlockDevicesResult() {
  return disk_fetcher_.FetchNonRemovableBlockDevicesInfo();
}

void FetchDelegateImpl::FetchSensorResult(
    base::OnceCallback<void(mojom::SensorResultPtr)> callback) {
  FetchSensorInfo(context_, std::move(callback));
}

void FetchDelegateImpl::FetchStatefulPartitionResult(
    base::OnceCallback<void(mojom::StatefulPartitionResultPtr)> callback) {
  FetchStatefulPartitionInfo(context_, std::move(callback));
}

void FetchDelegateImpl::FetchSystemResult(
    base::OnceCallback<void(mojom::SystemResultPtr)> callback) {
  FetchSystemInfo(context_, std::move(callback));
}

void FetchDelegateImpl::FetchThermalResult(
    base::OnceCallback<void(mojom::ThermalResultPtr)> callback) {
  FetchThermalInfo(context_, std::move(callback));
}

mojom::TimezoneResultPtr FetchDelegateImpl::FetchTimezoneResult() {
  return FetchTimezoneInfo();
}

void FetchDelegateImpl::FetchTpmResult(
    base::OnceCallback<void(mojom::TpmResultPtr)> callback) {
  FetchTpmInfo(context_, std::move(callback));
}

}  // namespace diagnostics
