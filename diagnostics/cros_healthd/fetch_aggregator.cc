// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetch_aggregator.h"

#include <memory>
#include <set>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <metrics/metrics_library.h>
#include <mojo/public/cpp/bindings/callback_helpers.h>

#include "diagnostics/cros_healthd/fetch_delegate.h"
#include "diagnostics/cros_healthd/utils/callback_barrier.h"
#include "diagnostics/cros_healthd/utils/metrics_utils.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

// Creates a callback which assigns the result to |target| and adds it to the
// dependencies of barrier. If the created callback is destructed without being
// called, an error will be assigned to |target|. The callers must make sure
// that |target| is valid until the callback is called.
template <typename T>
base::OnceCallback<void(T)> CreateFetchCallback(CallbackBarrier* barrier,
                                                T* target) {
  return mojo::WrapCallbackWithDefaultInvokeIfNotRun(
      barrier->Depend(base::BindOnce(
          [](T* target, T result) { *target = std::move(result); }, target)),
      T::Struct::NewError(
          mojom::ProbeError::New(mojom::ErrorType::kServiceUnavailable,
                                 "The fetch callback was dropped")));
}

void OnFinish(
    std::set<mojom::ProbeCategoryEnum> categories,
    mojom::CrosHealthdProbeService::ProbeTelemetryInfoCallback callback,
    std::unique_ptr<mojom::TelemetryInfoPtr> result,
    bool all_callbacks_called) {
  CHECK(all_callbacks_called);
  MetricsLibrary metrics;
  SendTelemetryResultToUMA(&metrics, categories, *result);
  std::move(callback).Run(std::move(*result));
}

}  // namespace

FetchAggregator::FetchAggregator(FetchDelegate* delegate)
    : delegate_(delegate) {
  CHECK(delegate_);
}

FetchAggregator::~FetchAggregator() = default;

void FetchAggregator::Run(
    const std::vector<mojom::ProbeCategoryEnum>& categories_to_probe,
    mojom::CrosHealthdProbeService::ProbeTelemetryInfoCallback callback) {
  // Use a set to eliminate duplicate categories.
  std::set<mojom::ProbeCategoryEnum> category_set(categories_to_probe.begin(),
                                                  categories_to_probe.end());

  // Use unique_ptr so the pointer |info| remains valid after std::move.
  auto result =
      std::make_unique<mojom::TelemetryInfoPtr>(mojom::TelemetryInfo::New());
  mojom::TelemetryInfo* info = result->get();
  // Let the on_finish callback take the |result| so it remains valid until all
  // the async fetch completes.
  CallbackBarrier barrier{base::BindOnce(
      &OnFinish, category_set, std::move(callback), std::move(result))};

  for (const auto category : category_set) {
    switch (category) {
      case mojom::ProbeCategoryEnum::kUnknown:
        // For interface backward compatibility.
        break;
      case mojom::ProbeCategoryEnum::kBattery:
        delegate_->FetchBatteryResult(
            CreateFetchCallback(&barrier, &info->battery_result));
        break;
      case mojom::ProbeCategoryEnum::kCpu:
        delegate_->FetchCpuResult(
            CreateFetchCallback(&barrier, &info->cpu_result));
        break;
      case mojom::ProbeCategoryEnum::kNonRemovableBlockDevices:
        info->block_device_result =
            delegate_->FetchNonRemovableBlockDevicesResult();
        break;
      case mojom::ProbeCategoryEnum::kTimezone:
        info->timezone_result = delegate_->FetchTimezoneResult();
        break;
      case mojom::ProbeCategoryEnum::kMemory:
        delegate_->FetchMemoryResult(
            CreateFetchCallback(&barrier, &info->memory_result));
        break;
      case mojom::ProbeCategoryEnum::kBacklight:
        info->backlight_result = delegate_->FetchBacklightResult();
        break;
      case mojom::ProbeCategoryEnum::kFan:
        delegate_->FetchFanResult(
            CreateFetchCallback(&barrier, &info->fan_result));
        break;
      case mojom::ProbeCategoryEnum::kStatefulPartition:
        delegate_->FetchStatefulPartitionResult(
            CreateFetchCallback(&barrier, &info->stateful_partition_result));
        break;
      case mojom::ProbeCategoryEnum::kBluetooth:
        delegate_->FetchBluetoothResult(
            CreateFetchCallback(&barrier, &info->bluetooth_result));
        break;
      case mojom::ProbeCategoryEnum::kSystem:
        delegate_->FetchSystemResult(
            CreateFetchCallback(&barrier, &info->system_result));
        break;
      case mojom::ProbeCategoryEnum::kNetwork:
        delegate_->FetchNetworkResult(
            CreateFetchCallback(&barrier, &info->network_result));
        break;
      case mojom::ProbeCategoryEnum::kAudio:
        delegate_->FetchAudioResult(
            CreateFetchCallback(&barrier, &info->audio_result));
        break;
      case mojom::ProbeCategoryEnum::kBootPerformance:
        delegate_->FetchBootPerformanceResult(
            CreateFetchCallback(&barrier, &info->boot_performance_result));
        break;
      case mojom::ProbeCategoryEnum::kBus:
        delegate_->FetchBusResult(
            CreateFetchCallback(&barrier, &info->bus_result));
        break;
      case mojom::ProbeCategoryEnum::kTpm:
        delegate_->FetchTpmResult(
            CreateFetchCallback(&barrier, &info->tpm_result));
        break;
      case mojom::ProbeCategoryEnum::kNetworkInterface:
        delegate_->FetchNetworkInterfaceResult(
            CreateFetchCallback(&barrier, &info->network_interface_result));
        break;
      case mojom::ProbeCategoryEnum::kGraphics:
        delegate_->FetchGraphicsResult(
            CreateFetchCallback(&barrier, &info->graphics_result));
        break;
      case mojom::ProbeCategoryEnum::kDisplay:
        delegate_->FetchDisplayResult(
            CreateFetchCallback(&barrier, &info->display_result));
        break;
      case mojom::ProbeCategoryEnum::kInput:
        delegate_->FetchInputResult(
            CreateFetchCallback(&barrier, &info->input_result));
        break;
      case mojom::ProbeCategoryEnum::kAudioHardware:
        delegate_->FetchAudioHardwareResult(
            CreateFetchCallback(&barrier, &info->audio_hardware_result));
        break;
      case mojom::ProbeCategoryEnum::kSensor:
        delegate_->FetchSensorResult(
            CreateFetchCallback(&barrier, &info->sensor_result));
        break;
      case mojom::ProbeCategoryEnum::kThermal:
        delegate_->FetchThermalResult(
            CreateFetchCallback(&barrier, &info->thermal_result));
        break;
    }
  }
}

}  // namespace diagnostics
