// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/metrics/metrics_utils.h"

#include <map>
#include <string>

#include <base/logging.h>
#include <base/memory/scoped_refptr.h>
#include <base/strings/string_number_conversions.h>

#include "rmad/proto_bindings/rmad.pb.h"
#include "rmad/utils/json_store.h"

namespace rmad {

bool MetricsUtils::SetStateSetupTimestamp(scoped_refptr<JsonStore> json_store,
                                          RmadState::StateCase state_case,
                                          double setup_timestamp) {
  std::string key = base::NumberToString(static_cast<int>(state_case));
  std::map<std::string, std::map<std::string, double>> state_metrics;
  // At the beginning, we may have no data, so ignore the return value.
  GetMetricsValue(json_store, kStateMetrics, &state_metrics);

  state_metrics[key][kStateSetupTimestamp] = setup_timestamp;
  return SetMetricsValue(json_store, kStateMetrics, state_metrics);
}

bool MetricsUtils::CalculateStateOverallTime(
    scoped_refptr<JsonStore> json_store,
    RmadState::StateCase state_case,
    double leave_timestamp) {
  std::string key = base::NumberToString(static_cast<int>(state_case));
  std::map<std::string, std::map<std::string, double>> state_metrics;

  if (!GetMetricsValue(json_store, kStateMetrics, &state_metrics) ||
      state_metrics.find(key) == state_metrics.end()) {
    LOG(ERROR) << "Failed to get state metrics to calculate.";
    return false;
  }

  if (state_metrics[key].find(kStateSetupTimestamp) ==
      state_metrics[key].end()) {
    LOG(ERROR) << "Failed to get timestamp when state is setup.";
    return false;
  }

  double setup_timestamp = state_metrics[key][kStateSetupTimestamp];
  double time_spent_sec = leave_timestamp - setup_timestamp;
  if (time_spent_sec < 0) {
    LOG(ERROR) << "Failed to calculate time spent.";
    return false;
  }

  state_metrics[key][kStateOverallTime] += time_spent_sec;
  state_metrics[key][kStateSetupTimestamp] = leave_timestamp;

  return SetMetricsValue(json_store, kStateMetrics, state_metrics);
}

}  // namespace rmad
