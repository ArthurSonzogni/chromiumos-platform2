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

namespace {

namespace mojo_ipc = chromeos::cros_healthd::mojom;

}  // namespace

FetchAggregator::FetchAggregator(Context* context)
    : audio_fetcher_(context),
      backlight_fetcher_(context),
      battery_fetcher_(context),
      bluetooth_fetcher_(context),
      boot_performance_fetcher_(context),
      bus_fetcher_(context),
      cpu_fetcher_(context),
      disk_fetcher_(context),
      fan_fetcher_(context),
      graphics_fetcher_(context),
      memory_fetcher_(context),
      network_fetcher_(context),
      stateful_partition_fetcher_(context),
      system_fetcher_(context),
      timezone_fetcher_(context),
      tpm_fetcher_(context) {}

FetchAggregator::~FetchAggregator() = default;

void FetchAggregator::Run(
    const std::vector<mojo_ipc::ProbeCategoryEnum>& categories_to_probe,
    mojo_ipc::CrosHealthdProbeService::ProbeTelemetryInfoCallback callback) {
  const auto& state =
      CreateProbeState(categories_to_probe, std::move(callback));
  auto& info = state->result;

  for (const auto category : categories_to_probe) {
    switch (category) {
      case mojo_ipc::ProbeCategoryEnum::kBattery: {
        WrapFetchProbeData(category, state, &info->battery_result,
                           battery_fetcher_.FetchBatteryInfo());
        break;
      }
      case mojo_ipc::ProbeCategoryEnum::kCpu: {
        WrapFetchProbeData(category, state, &info->cpu_result,
                           cpu_fetcher_.FetchCpuInfo());
        break;
      }
      case mojo_ipc::ProbeCategoryEnum::kNonRemovableBlockDevices: {
        WrapFetchProbeData(category, state, &info->block_device_result,
                           disk_fetcher_.FetchNonRemovableBlockDevicesInfo());
        break;
      }
      case mojo_ipc::ProbeCategoryEnum::kTimezone: {
        WrapFetchProbeData(category, state, &info->timezone_result,
                           timezone_fetcher_.FetchTimezoneInfo());
        break;
      }
      case mojo_ipc::ProbeCategoryEnum::kMemory: {
        WrapFetchProbeData(category, state, &info->memory_result,
                           memory_fetcher_.FetchMemoryInfo());
        break;
      }
      case mojo_ipc::ProbeCategoryEnum::kBacklight: {
        WrapFetchProbeData(category, state, &info->backlight_result,
                           backlight_fetcher_.FetchBacklightInfo());
        break;
      }
      case mojo_ipc::ProbeCategoryEnum::kFan: {
        fan_fetcher_.FetchFanInfo(base::BindOnce(
            &FetchAggregator::WrapFetchProbeData<mojo_ipc::FanResultPtr>,
            weak_factory_.GetWeakPtr(), category, std::cref(state),
            &info->fan_result));
        break;
      }
      case mojo_ipc::ProbeCategoryEnum::kStatefulPartition: {
        WrapFetchProbeData(
            category, state, &info->stateful_partition_result,
            stateful_partition_fetcher_.FetchStatefulPartitionInfo());
        break;
      }
      case mojo_ipc::ProbeCategoryEnum::kBluetooth: {
        WrapFetchProbeData(category, state, &info->bluetooth_result,
                           bluetooth_fetcher_.FetchBluetoothInfo());
        break;
      }
      case mojo_ipc::ProbeCategoryEnum::kSystem: {
        WrapFetchProbeData(category, state, &info->system_result,
                           system_fetcher_.FetchSystemInfo());
        break;
      }
      case mojo_ipc::ProbeCategoryEnum::kSystem2: {
        WrapFetchProbeData(category, state, &info->system_result_v2,
                           system_fetcher_.FetchSystemInfoV2());
        break;
      }
      case mojo_ipc::ProbeCategoryEnum::kNetwork: {
        network_fetcher_.FetchNetworkInfo(base::BindOnce(
            &FetchAggregator::WrapFetchProbeData<mojo_ipc::NetworkResultPtr>,
            weak_factory_.GetWeakPtr(), category, std::cref(state),
            &info->network_result));
        break;
      }
      case mojo_ipc::ProbeCategoryEnum::kAudio: {
        WrapFetchProbeData(category, state, &info->audio_result,
                           audio_fetcher_.FetchAudioInfo());
        break;
      }
      case mojo_ipc::ProbeCategoryEnum::kBootPerformance: {
        WrapFetchProbeData(
            category, state, &info->boot_performance_result,
            boot_performance_fetcher_.FetchBootPerformanceInfo());
        break;
      }
      case mojo_ipc::ProbeCategoryEnum::kBus: {
        WrapFetchProbeData(category, state, &info->bus_result,
                           bus_fetcher_.FetchBusDevices());
        break;
      }
      case mojo_ipc::ProbeCategoryEnum::kTpm: {
        tpm_fetcher_.FetchTpmInfo(base::BindOnce(
            &FetchAggregator::WrapFetchProbeData<mojo_ipc::TpmResultPtr>,
            weak_factory_.GetWeakPtr(), category, std::cref(state),
            &info->tpm_result));
        break;
      }
      case mojo_ipc::ProbeCategoryEnum::kGraphics: {
        WrapFetchProbeData(category, state, &info->graphics_result,
                           graphics_fetcher_.FetchGraphicsInfo());
        break;
      }
    }
  }
}

const std::unique_ptr<FetchAggregator::ProbeState>&
FetchAggregator::CreateProbeState(
    const std::vector<mojo_ipc::ProbeCategoryEnum>& categories_to_probe,
    mojo_ipc::CrosHealthdProbeService::ProbeTelemetryInfoCallback callback) {
  const auto [it, success] =
      probe_states_.insert(std::make_unique<ProbeState>());
  DCHECK(success);
  const auto& state = *it;
  DCHECK(state);
  state->remaining_categories = std::set<mojo_ipc::ProbeCategoryEnum>(
      categories_to_probe.begin(), categories_to_probe.end());
  state->result = mojo_ipc::TelemetryInfo::New();
  state->callback = std::move(callback);
  return state;
}

template <class T>
void FetchAggregator::WrapFetchProbeData(
    mojo_ipc::ProbeCategoryEnum category,
    const std::unique_ptr<ProbeState>& state,
    T* response_data,
    T fetched_data) {
  DCHECK(state);
  DCHECK(response_data);

  *response_data = std::move(fetched_data);

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
