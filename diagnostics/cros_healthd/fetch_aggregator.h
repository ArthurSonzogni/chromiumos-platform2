// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCH_AGGREGATOR_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCH_AGGREGATOR_H_

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <vector>

#include <base/memory/weak_ptr.h>

#include "diagnostics/cros_healthd/fetchers/audio_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/backlight_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/battery_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/bluetooth_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/boot_performance_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/bus_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/cpu_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/disk_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/display_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/fan_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/graphics_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/memory_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/network_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/network_interface_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/stateful_partition_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/system_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/timezone_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/tpm_fetcher.h"
#include "diagnostics/cros_healthd/system/context.h"
#include "diagnostics/mojom/public/cros_healthd.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {

// This class is responsible for aggregating probe data from various fetchers,
// some of which may be asynchronous, and running the given callback when all
// probe data has been fetched.
class FetchAggregator final {
 public:
  explicit FetchAggregator(Context* context);
  FetchAggregator(const FetchAggregator&) = delete;
  FetchAggregator& operator=(const FetchAggregator&) = delete;
  ~FetchAggregator();

  // Runs the aggregator, which will collect all relevant data and then run the
  // callback.
  void Run(const std::vector<chromeos::cros_healthd::mojom::ProbeCategoryEnum>&
               categories_to_probe,
           chromeos::cros_healthd::mojom::CrosHealthdProbeService::
               ProbeTelemetryInfoCallback callback);

 private:
  // Each call of |Run()| creates a |ProbeState| and its lifecycle is bonded to
  // |FetchAggregator|. This allows a single |FetchAggregator| instance to have
  // multiple pending asynchronous fetches corresponding to distinct Run()
  // calls.
  struct ProbeState {
    // Contains requested categories which have not been fetched yet.
    std::set<chromeos::cros_healthd::mojom::ProbeCategoryEnum>
        remaining_categories;
    chromeos::cros_healthd::mojom::TelemetryInfoPtr result;
    // The callback to return the result.
    chromeos::cros_healthd::mojom::CrosHealthdProbeService::
        ProbeTelemetryInfoCallback callback;
  };

  const std::unique_ptr<ProbeState>& CreateProbeState(
      const std::vector<chromeos::cros_healthd::mojom::ProbeCategoryEnum>&
          categories_to_probe,
      chromeos::cros_healthd::mojom::CrosHealthdProbeService::
          ProbeTelemetryInfoCallback callback);

  // Wraps a fetch operation from either a synchronous or asynchronous fetcher.
  template <class T>
  void WrapFetchProbeData(
      chromeos::cros_healthd::mojom::ProbeCategoryEnum category,
      const std::unique_ptr<ProbeState>& state,
      T* response_data,
      T fetched_data);

  // Completes a probe category of a probe state. If all the categories are
  // probed, call the callback.
  void CompleteProbe(chromeos::cros_healthd::mojom::ProbeCategoryEnum category,
                     const std::unique_ptr<ProbeState>& state);

 private:
  // The set to keep the instances of |ProbeState|.
  std::set<std::unique_ptr<ProbeState>> probe_states_;

  AudioFetcher audio_fetcher_;
  BacklightFetcher backlight_fetcher_;
  BatteryFetcher battery_fetcher_;
  BluetoothFetcher bluetooth_fetcher_;
  BootPerformanceFetcher boot_performance_fetcher_;
  BusFetcher bus_fetcher_;
  CpuFetcher cpu_fetcher_;
  DiskFetcher disk_fetcher_;
  DisplayFetcher display_fetcher_;
  FanFetcher fan_fetcher_;
  GraphicsFetcher graphics_fetcher_;
  MemoryFetcher memory_fetcher_;
  NetworkFetcher network_fetcher_;
  StatefulPartitionFetcher stateful_partition_fetcher_;
  SystemFetcher system_fetcher_;
  TimezoneFetcher timezone_fetcher_;
  TpmFetcher tpm_fetcher_;
  NetworkInterfaceFetcher network_interface_fetcher_;

  // Must be the last member of the class.
  base::WeakPtrFactory<FetchAggregator> weak_factory_{this};
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCH_AGGREGATOR_H_
