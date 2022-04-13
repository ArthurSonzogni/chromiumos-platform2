// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetch_aggregator.h"

#include <functional>
#include <iterator>
#include <map>
#include <utility>
#include <vector>

#include <base/callback.h>
#include <base/check.h>
#include <base/logging.h>

namespace diagnostics {

FetchAggregator::FetchAggregator(Context* context)
    : audio_fetcher_(context),
      backlight_fetcher_(context),
      battery_fetcher_(context),
      bluetooth_fetcher_(context),
      boot_performance_fetcher_(context),
      bus_fetcher_(context),
      cpu_fetcher_(context),
      disk_fetcher_(context),
      display_fetcher_(context),
      fan_fetcher_(context),
      graphics_fetcher_(context),
      memory_fetcher_(context),
      network_fetcher_(context),
      stateful_partition_fetcher_(context),
      system_fetcher_(context),
      timezone_fetcher_(context),
      tpm_fetcher_(context),
      network_interface_fetcher_(context) {}

FetchAggregator::~FetchAggregator() = default;

void FetchAggregator::Run(
    const std::vector<mojom::ProbeCategoryEnum>& categories_to_probe,
    mojom::CrosHealthdProbeService::ProbeTelemetryInfoCallback callback) {
  const auto& state =
      CreateProbeState(categories_to_probe, std::move(callback));
  auto& info = state->result;
  // Clone the set because we will modify it during for-loop.
  const auto remaining_categories = state->remaining_categories;

  for (const auto category : remaining_categories) {
    switch (category) {
      case mojom::ProbeCategoryEnum::kUnknown: {
        // Got unknown category. Just complete it and continue.
        CompleteProbe(category, state);
        break;
      }
      case mojom::ProbeCategoryEnum::kBattery: {
        WrapFetchProbeData(category, state, &info->battery_result,
                           battery_fetcher_.FetchBatteryInfo());
        break;
      }
      case mojom::ProbeCategoryEnum::kCpu: {
        WrapFetchProbeData(category, state, &info->cpu_result,
                           cpu_fetcher_.FetchCpuInfo());
        break;
      }
      case mojom::ProbeCategoryEnum::kNonRemovableBlockDevices: {
        WrapFetchProbeData(category, state, &info->block_device_result,
                           disk_fetcher_.FetchNonRemovableBlockDevicesInfo());
        break;
      }
      case mojom::ProbeCategoryEnum::kTimezone: {
        WrapFetchProbeData(category, state, &info->timezone_result,
                           timezone_fetcher_.FetchTimezoneInfo());
        break;
      }
      case mojom::ProbeCategoryEnum::kMemory: {
        memory_fetcher_.FetchMemoryInfo(base::BindOnce(
            &FetchAggregator::WrapFetchProbeData<mojom::MemoryResultPtr>,
            weak_factory_.GetWeakPtr(), category, std::cref(state),
            &info->memory_result));
        break;
      }
      case mojom::ProbeCategoryEnum::kBacklight: {
        WrapFetchProbeData(category, state, &info->backlight_result,
                           backlight_fetcher_.FetchBacklightInfo());
        break;
      }
      case mojom::ProbeCategoryEnum::kFan: {
        fan_fetcher_.FetchFanInfo(base::BindOnce(
            &FetchAggregator::WrapFetchProbeData<mojom::FanResultPtr>,
            weak_factory_.GetWeakPtr(), category, std::cref(state),
            &info->fan_result));
        break;
      }
      case mojom::ProbeCategoryEnum::kStatefulPartition: {
        WrapFetchProbeData(
            category, state, &info->stateful_partition_result,
            stateful_partition_fetcher_.FetchStatefulPartitionInfo());
        break;
      }
      case mojom::ProbeCategoryEnum::kBluetooth: {
        WrapFetchProbeData(category, state, &info->bluetooth_result,
                           bluetooth_fetcher_.FetchBluetoothInfo());
        break;
      }
      case mojom::ProbeCategoryEnum::kSystem: {
        system_fetcher_.FetchSystemInfo(base::BindOnce(
            &FetchAggregator::WrapFetchProbeData<mojom::SystemResultPtr>,
            weak_factory_.GetWeakPtr(), category, std::cref(state),
            &info->system_result));
        break;
      }
      case mojom::ProbeCategoryEnum::kSystem2: {
        system_fetcher_.FetchSystemInfoV2(base::BindOnce(
            &FetchAggregator::WrapFetchProbeData<mojom::SystemResultV2Ptr>,
            weak_factory_.GetWeakPtr(), category, std::cref(state),
            &info->system_result_v2));
        break;
      }
      case mojom::ProbeCategoryEnum::kNetwork: {
        network_fetcher_.FetchNetworkInfo(base::BindOnce(
            &FetchAggregator::WrapFetchProbeData<mojom::NetworkResultPtr>,
            weak_factory_.GetWeakPtr(), category, std::cref(state),
            &info->network_result));
        break;
      }
      case mojom::ProbeCategoryEnum::kAudio: {
        WrapFetchProbeData(category, state, &info->audio_result,
                           audio_fetcher_.FetchAudioInfo());
        break;
      }
      case mojom::ProbeCategoryEnum::kBootPerformance: {
        WrapFetchProbeData(
            category, state, &info->boot_performance_result,
            boot_performance_fetcher_.FetchBootPerformanceInfo());
        break;
      }
      case mojom::ProbeCategoryEnum::kBus: {
        bus_fetcher_.FetchBusDevices(base::BindOnce(
            &FetchAggregator::WrapFetchProbeData<mojom::BusResultPtr>,
            weak_factory_.GetWeakPtr(), category, std::cref(state),
            &info->bus_result));
        break;
      }
      case mojom::ProbeCategoryEnum::kTpm: {
        tpm_fetcher_.FetchTpmInfo(base::BindOnce(
            &FetchAggregator::WrapFetchProbeData<mojom::TpmResultPtr>,
            weak_factory_.GetWeakPtr(), category, std::cref(state),
            &info->tpm_result));
        break;
      }
      case mojom::ProbeCategoryEnum::kNetworkInterface: {
        network_interface_fetcher_.FetchNetworkInterfaceInfo(
            base::BindOnce(&FetchAggregator::WrapFetchProbeData<
                               mojom::NetworkInterfaceResultPtr>,
                           weak_factory_.GetWeakPtr(), category,
                           std::cref(state), &info->network_interface_result));
        break;
      }
      case mojom::ProbeCategoryEnum::kGraphics: {
        WrapFetchProbeData(category, state, &info->graphics_result,
                           graphics_fetcher_.FetchGraphicsInfo());
        break;
      }
      case mojom::ProbeCategoryEnum::kDisplay: {
        display_fetcher_.FetchDisplayInfo(base::BindOnce(
            &FetchAggregator::WrapFetchProbeData<mojom::DisplayResultPtr>,
            weak_factory_.GetWeakPtr(), category, std::cref(state),
            &info->display_result));
        break;
      }
    }
  }
}

const std::unique_ptr<FetchAggregator::ProbeState>&
FetchAggregator::CreateProbeState(
    const std::vector<mojom::ProbeCategoryEnum>& categories_to_probe,
    mojom::CrosHealthdProbeService::ProbeTelemetryInfoCallback callback) {
  const auto [it, success] =
      probe_states_.insert(std::make_unique<ProbeState>());
  DCHECK(success);
  const auto& state = *it;
  DCHECK(state);
  state->remaining_categories = std::set<mojom::ProbeCategoryEnum>(
      categories_to_probe.begin(), categories_to_probe.end());
  state->result = mojom::TelemetryInfo::New();
  state->callback = std::move(callback);
  return state;
}

template <class T>
void FetchAggregator::WrapFetchProbeData(
    mojom::ProbeCategoryEnum category,
    const std::unique_ptr<ProbeState>& state,
    T* response_data,
    T fetched_data) {
  DCHECK(state);
  DCHECK(response_data);

  *response_data = std::move(fetched_data);
  CompleteProbe(category, state);
}

void FetchAggregator::CompleteProbe(
    chromeos::cros_healthd::mojom::ProbeCategoryEnum category,
    const std::unique_ptr<ProbeState>& state) {
  DCHECK(state);

  auto& remaining_categories = state->remaining_categories;
  remaining_categories.erase(category);
  // Check for any unfetched categories - if one exists, we can't run the
  // callback yet.
  if (!remaining_categories.empty())
    return;

  std::move(state->callback).Run(std::move(state->result));
  probe_states_.erase(state);
}

}  // namespace diagnostics
