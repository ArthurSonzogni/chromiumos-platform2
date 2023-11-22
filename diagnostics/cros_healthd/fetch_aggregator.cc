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

#include "diagnostics/cros_healthd/fetchers/audio_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/audio_hardware_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/backlight_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/battery_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/bluetooth_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/bus_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/cpu_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/fan_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/graphics_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/input_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/network_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/network_interface_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/sensor_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/stateful_partition_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/system_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/thermal_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/timezone_fetcher.h"
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

FetchAggregator::FetchAggregator(Context* context)
    : disk_fetcher_(context),
      memory_fetcher_(context),
      tpm_fetcher_(context),
      context_(context) {}

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
      case mojom::ProbeCategoryEnum::kUnknown: {
        // For interface backward compatibility.
        break;
      }
      case mojom::ProbeCategoryEnum::kBattery: {
        FetchBatteryInfo(context_,
                         CreateFetchCallback(&barrier, &info->battery_result));
        break;
      }
      case mojom::ProbeCategoryEnum::kCpu: {
        FetchCpuInfo(context_,
                     CreateFetchCallback(&barrier, &info->cpu_result));
        break;
      }
      case mojom::ProbeCategoryEnum::kNonRemovableBlockDevices: {
        info->block_device_result =
            disk_fetcher_.FetchNonRemovableBlockDevicesInfo();
        break;
      }
      case mojom::ProbeCategoryEnum::kTimezone: {
        info->timezone_result = FetchTimezoneInfo();
        break;
      }
      case mojom::ProbeCategoryEnum::kMemory: {
        memory_fetcher_.FetchMemoryInfo(
            CreateFetchCallback(&barrier, &info->memory_result));
        break;
      }
      case mojom::ProbeCategoryEnum::kBacklight: {
        info->backlight_result = FetchBacklightInfo(context_);
        break;
      }
      case mojom::ProbeCategoryEnum::kFan: {
        FetchFanInfo(context_,
                     CreateFetchCallback(&barrier, &info->fan_result));
        break;
      }
      case mojom::ProbeCategoryEnum::kStatefulPartition: {
        FetchStatefulPartitionInfo(
            context_,
            CreateFetchCallback(&barrier, &info->stateful_partition_result));
        break;
      }
      case mojom::ProbeCategoryEnum::kBluetooth: {
        FetchBluetoothInfo(
            context_, CreateFetchCallback(&barrier, &info->bluetooth_result));
        break;
      }
      case mojom::ProbeCategoryEnum::kSystem: {
        FetchSystemInfo(context_,
                        CreateFetchCallback(&barrier, &info->system_result));
        break;
      }
      case mojom::ProbeCategoryEnum::kNetwork: {
        FetchNetworkInfo(context_,
                         CreateFetchCallback(&barrier, &info->network_result));
        break;
      }
      case mojom::ProbeCategoryEnum::kAudio: {
        FetchAudioInfo(context_,
                       CreateFetchCallback(&barrier, &info->audio_result));
        break;
      }
      case mojom::ProbeCategoryEnum::kBootPerformance: {
        context_->executor()->FetchBootPerformance(
            CreateFetchCallback(&barrier, &info->boot_performance_result));
        break;
      }
      case mojom::ProbeCategoryEnum::kBus: {
        FetchBusDevices(context_,
                        CreateFetchCallback(&barrier, &info->bus_result));
        break;
      }
      case mojom::ProbeCategoryEnum::kTpm: {
        tpm_fetcher_.FetchTpmInfo(
            CreateFetchCallback(&barrier, &info->tpm_result));
        break;
      }
      case mojom::ProbeCategoryEnum::kNetworkInterface: {
        FetchNetworkInterfaceInfo(
            context_,
            CreateFetchCallback(&barrier, &info->network_interface_result));
        break;
      }
      case mojom::ProbeCategoryEnum::kGraphics: {
        info->graphics_result = FetchGraphicsInfo();
        break;
      }
      case mojom::ProbeCategoryEnum::kDisplay: {
        context_->executor()->FetchDisplayInfo(
            CreateFetchCallback(&barrier, &info->display_result));
        break;
      }
      case mojom::ProbeCategoryEnum::kInput: {
        FetchInputInfo(context_,
                       CreateFetchCallback(&barrier, &info->input_result));
        break;
      }
      case mojom::ProbeCategoryEnum::kAudioHardware: {
        FetchAudioHardwareInfo(
            context_,
            CreateFetchCallback(&barrier, &info->audio_hardware_result));
        break;
      }
      case mojom::ProbeCategoryEnum::kSensor: {
        FetchSensorInfo(context_,
                        CreateFetchCallback(&barrier, &info->sensor_result));
        break;
      }
      case mojom::ProbeCategoryEnum::kThermal: {
        FetchThermalInfo(context_,
                         CreateFetchCallback(&barrier, &info->thermal_result));
        break;
      }
    }
  }
}

}  // namespace diagnostics
