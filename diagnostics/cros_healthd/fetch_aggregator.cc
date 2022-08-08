// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetch_aggregator.h"

#include <memory>
#include <set>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/logging.h>

#include "diagnostics/cros_healthd/fetchers/audio_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/audio_hardware_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/bus_fetcher.h"
#include "diagnostics/cros_healthd/utils/callback_barrier.h"

namespace diagnostics {

namespace {

// Creates a callback which assigns the result to |target| and adds it to the
// dependencies of barrier. The callers must make sure that |target| is valid
// until the callback is called.
template <typename T>
base::OnceCallback<void(T)> CreateFetchCallback(CallbackBarrier* barrier,
                                                T* target) {
  return barrier->Depend(base::BindOnce(
      [](T* target, T result) { *target = std::move(result); }, target));
}

void OnFinish(
    mojom::CrosHealthdProbeService::ProbeTelemetryInfoCallback callback,
    std::unique_ptr<mojom::TelemetryInfoPtr> result) {
  std::move(callback).Run(std::move(*result));
}

}  // namespace

FetchAggregator::FetchAggregator(Context* context)
    : backlight_fetcher_(context),
      battery_fetcher_(context),
      bluetooth_fetcher_(context),
      boot_performance_fetcher_(context),
      cpu_fetcher_(context),
      disk_fetcher_(context),
      display_fetcher_(context),
      fan_fetcher_(context),
      graphics_fetcher_(context),
      input_fetcher_(context),
      memory_fetcher_(context),
      network_fetcher_(context),
      stateful_partition_fetcher_(context),
      system_fetcher_(context),
      timezone_fetcher_(context),
      tpm_fetcher_(context),
      network_interface_fetcher_(context),
      context_(context) {}

FetchAggregator::~FetchAggregator() = default;

void FetchAggregator::Run(
    const std::vector<mojom::ProbeCategoryEnum>& categories_to_probe,
    mojom::CrosHealthdProbeService::ProbeTelemetryInfoCallback callback) {
  // Use unique_ptr so the pointer |info| remains valid after std::move.
  auto result =
      std::make_unique<mojom::TelemetryInfoPtr>(mojom::TelemetryInfo::New());
  mojom::TelemetryInfo* info = result->get();
  // Let the on_success callback take the |result| so it remains valid until all
  // the async fetch completes.
  auto on_success =
      base::BindOnce(&OnFinish, std::move(callback), std::move(result));
  CallbackBarrier barrier{
      std::move(on_success), /*on_error=*/base::BindOnce([]() {
        LOG(ERROR) << "Some async fetchers didn't call the callback.";
      })};

  for (const auto category : std::set<mojom::ProbeCategoryEnum>(
           categories_to_probe.begin(), categories_to_probe.end())) {
    switch (category) {
      case mojom::ProbeCategoryEnum::kUnknown: {
        // For interface backward compatibility.
        break;
      }
      case mojom::ProbeCategoryEnum::kBattery: {
        info->battery_result = battery_fetcher_.FetchBatteryInfo();
        break;
      }
      case mojom::ProbeCategoryEnum::kCpu: {
        cpu_fetcher_.Fetch(CreateFetchCallback(&barrier, &info->cpu_result));
        break;
      }
      case mojom::ProbeCategoryEnum::kNonRemovableBlockDevices: {
        info->block_device_result =
            disk_fetcher_.FetchNonRemovableBlockDevicesInfo();
        break;
      }
      case mojom::ProbeCategoryEnum::kTimezone: {
        info->timezone_result = timezone_fetcher_.FetchTimezoneInfo();
        break;
      }
      case mojom::ProbeCategoryEnum::kMemory: {
        memory_fetcher_.FetchMemoryInfo(
            CreateFetchCallback(&barrier, &info->memory_result));
        break;
      }
      case mojom::ProbeCategoryEnum::kBacklight: {
        info->backlight_result = backlight_fetcher_.FetchBacklightInfo();
        break;
      }
      case mojom::ProbeCategoryEnum::kFan: {
        fan_fetcher_.FetchFanInfo(
            CreateFetchCallback(&barrier, &info->fan_result));
        break;
      }
      case mojom::ProbeCategoryEnum::kStatefulPartition: {
        info->stateful_partition_result =
            stateful_partition_fetcher_.FetchStatefulPartitionInfo();
        break;
      }
      case mojom::ProbeCategoryEnum::kBluetooth: {
        info->bluetooth_result = bluetooth_fetcher_.FetchBluetoothInfo();
        break;
      }
      case mojom::ProbeCategoryEnum::kSystem: {
        system_fetcher_.FetchSystemInfo(
            CreateFetchCallback(&barrier, &info->system_result),
            CreateFetchCallback(&barrier, &info->system_result_v2));
        break;
      }
      case mojom::ProbeCategoryEnum::kNetwork: {
        network_fetcher_.FetchNetworkInfo(
            CreateFetchCallback(&barrier, &info->network_result));
        break;
      }
      case mojom::ProbeCategoryEnum::kAudio: {
        FetchAudioInfo(context_,
                       CreateFetchCallback(&barrier, &info->audio_result));
        break;
      }
      case mojom::ProbeCategoryEnum::kBootPerformance: {
        info->boot_performance_result =
            boot_performance_fetcher_.FetchBootPerformanceInfo();
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
        network_interface_fetcher_.FetchNetworkInterfaceInfo(
            CreateFetchCallback(&barrier, &info->network_interface_result));
        break;
      }
      case mojom::ProbeCategoryEnum::kGraphics: {
        info->graphics_result = graphics_fetcher_.FetchGraphicsInfo();
        break;
      }
      case mojom::ProbeCategoryEnum::kDisplay: {
        display_fetcher_.FetchDisplayInfo(
            CreateFetchCallback(&barrier, &info->display_result));
        break;
      }
      case mojom::ProbeCategoryEnum::kInput: {
        input_fetcher_.Fetch(
            CreateFetchCallback(&barrier, &info->input_result));
        break;
      }
      case mojom::ProbeCategoryEnum::kAudioHardware: {
        FetchAudioHardwareInfo(
            context_,
            CreateFetchCallback(&barrier, &info->audio_hardware_result));
        break;
      }
    }
  }
}

}  // namespace diagnostics
