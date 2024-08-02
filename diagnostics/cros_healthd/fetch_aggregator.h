// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCH_AGGREGATOR_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCH_AGGREGATOR_H_

#include <vector>

#include "diagnostics/mojom/public/cros_healthd.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom-forward.h"

namespace diagnostics {
class FetchDelegate;

// This class is responsible for aggregating probe data from various fetchers,
// some of which may be asynchronous, and running the given callback when all
// probe data has been fetched.
class FetchAggregator final {
 public:
  explicit FetchAggregator(FetchDelegate* delegate);
  FetchAggregator(const FetchAggregator&) = delete;
  FetchAggregator& operator=(const FetchAggregator&) = delete;
  ~FetchAggregator();

  // Runs the aggregator, which will collect all relevant data and then run the
  // callback.
  void Run(const std::vector<ash::cros_healthd::mojom::ProbeCategoryEnum>&
               categories_to_probe,
           ash::cros_healthd::mojom::CrosHealthdProbeService::
               ProbeTelemetryInfoCallback callback);

 private:
  // Responsible for fetching telemetry data. Unowned pointer that should
  // outlive this instance.
  FetchDelegate* const delegate_ = nullptr;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCH_AGGREGATOR_H_
