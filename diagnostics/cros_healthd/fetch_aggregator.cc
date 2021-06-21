// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetch_aggregator.h"

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
      memory_fetcher_(context),
      network_fetcher_(context),
      stateful_partition_fetcher_(context),
      system_fetcher_(context),
      timezone_fetcher_(context) {}

FetchAggregator::~FetchAggregator() = default;

void FetchAggregator::Run(
    const std::vector<mojo_ipc::ProbeCategoryEnum>& categories_to_probe,
    mojo_ipc::CrosHealthdProbeService::ProbeTelemetryInfoCallback callback) {
  std::unique_ptr<ProbeState> state = std::make_unique<ProbeState>();
  state->remaining_categories = std::set<mojo_ipc::ProbeCategoryEnum>(
      categories_to_probe.begin(), categories_to_probe.end());
  state->callback = std::move(callback);

  auto itr_bool_pair =
      pending_calls_.emplace(GetNextAvailableKey(), std::move(state));
  DCHECK(itr_bool_pair.second);
  auto itr = itr_bool_pair.first;

  mojo_ipc::TelemetryInfo* info = &itr->second->fetched_data;

  for (const auto category : categories_to_probe) {
    switch (category) {
      case mojo_ipc::ProbeCategoryEnum::kBattery: {
        WrapFetchProbeData(category, itr, &info->battery_result,
                           battery_fetcher_.FetchBatteryInfo());
        break;
      }
      case mojo_ipc::ProbeCategoryEnum::kCpu: {
        WrapFetchProbeData(category, itr, &info->cpu_result,
                           cpu_fetcher_.FetchCpuInfo());
        break;
      }
      case mojo_ipc::ProbeCategoryEnum::kNonRemovableBlockDevices: {
        WrapFetchProbeData(category, itr, &info->block_device_result,
                           disk_fetcher_.FetchNonRemovableBlockDevicesInfo());
        break;
      }
      case mojo_ipc::ProbeCategoryEnum::kTimezone: {
        WrapFetchProbeData(category, itr, &info->timezone_result,
                           timezone_fetcher_.FetchTimezoneInfo());
        break;
      }
      case mojo_ipc::ProbeCategoryEnum::kMemory: {
        WrapFetchProbeData(category, itr, &info->memory_result,
                           memory_fetcher_.FetchMemoryInfo());
        break;
      }
      case mojo_ipc::ProbeCategoryEnum::kBacklight: {
        WrapFetchProbeData(category, itr, &info->backlight_result,
                           backlight_fetcher_.FetchBacklightInfo());
        break;
      }
      case mojo_ipc::ProbeCategoryEnum::kFan: {
        fan_fetcher_.FetchFanInfo(base::BindOnce(
            &FetchAggregator::WrapFetchProbeData<mojo_ipc::FanResultPtr>,
            weak_factory_.GetWeakPtr(), category, itr, &info->fan_result));
        break;
      }
      case mojo_ipc::ProbeCategoryEnum::kStatefulPartition: {
        WrapFetchProbeData(
            category, itr, &info->stateful_partition_result,
            stateful_partition_fetcher_.FetchStatefulPartitionInfo());
        break;
      }
      case mojo_ipc::ProbeCategoryEnum::kBluetooth: {
        WrapFetchProbeData(category, itr, &info->bluetooth_result,
                           bluetooth_fetcher_.FetchBluetoothInfo());
        break;
      }
      case mojo_ipc::ProbeCategoryEnum::kSystem: {
        WrapFetchProbeData(category, itr, &info->system_result,
                           system_fetcher_.FetchSystemInfo());
        break;
      }
      case mojo_ipc::ProbeCategoryEnum::kSystem2: {
        WrapFetchProbeData(category, itr, &info->system_result_v2,
                           system_fetcher_.FetchSystemInfoV2());
        break;
      }
      case mojo_ipc::ProbeCategoryEnum::kNetwork: {
        network_fetcher_.FetchNetworkInfo(base::BindOnce(
            &FetchAggregator::WrapFetchProbeData<mojo_ipc::NetworkResultPtr>,
            weak_factory_.GetWeakPtr(), category, itr, &info->network_result));
        break;
      }
      case mojo_ipc::ProbeCategoryEnum::kAudio: {
        WrapFetchProbeData(category, itr, &info->audio_result,
                           audio_fetcher_.FetchAudioInfo());
        break;
      }
      case mojo_ipc::ProbeCategoryEnum::kBootPerformance: {
        WrapFetchProbeData(
            category, itr, &info->boot_performance_result,
            boot_performance_fetcher_.FetchBootPerformanceInfo());
        break;
      }
      case mojo_ipc::ProbeCategoryEnum::kBus: {
        WrapFetchProbeData(category, itr, &info->bus_result,
                           bus_fetcher_.FetchBusDevices());
        break;
      }
    }
  }
}

template <class T>
void FetchAggregator::WrapFetchProbeData(
    mojo_ipc::ProbeCategoryEnum category,
    std::map<uint32_t, std::unique_ptr<ProbeState>>::iterator itr,
    T* response_data,
    T fetched_data) {
  DCHECK(response_data);

  *response_data = std::move(fetched_data);

  base::AutoLock auto_lock(lock_);

  ProbeState* state = itr->second.get();

  auto* remaining_categories = &state->remaining_categories;
  // Remove the current category, since it's been fetched.
  remaining_categories->erase(category);

  // Check for any unfetched categories - if one exists, we can't run the
  // callback yet.
  if (!remaining_categories->empty())
    return;

  std::move(state->callback).Run(state->fetched_data.Clone());
  pending_calls_.erase(itr);
}

uint32_t FetchAggregator::GetNextAvailableKey() {
  uint32_t free_index = 0;
  for (auto it = pending_calls_.cbegin(); it != pending_calls_.cend(); it++) {
    // Since we allocate keys sequentially, if the iterator key ever skips a
    // value, then that value is free to take.
    if (free_index != it->first)
      break;
    free_index++;
  }

  return free_index;
}

}  // namespace diagnostics
