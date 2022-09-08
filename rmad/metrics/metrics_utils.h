// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_METRICS_METRICS_UTILS_H_
#define RMAD_METRICS_METRICS_UTILS_H_

#include <map>
#include <string>
#include <utility>

#include <base/memory/scoped_refptr.h>

#include "rmad/metrics/metrics_constants.h"
#include "rmad/utils/json_store.h"
#include "rmad/utils/type_conversions.h"

namespace rmad {

struct StateMetricsData {
 public:
  StateMetricsData() = default;
  ~StateMetricsData() = default;

  bool operator==(const StateMetricsData& other) const;
  base::Value ToValue() const;
  bool FromValue(const base::Value* data);

  RmadState::StateCase state_case;
  bool is_aborted;
  double setup_timestamp;
  double overall_time;
  int transition_count;
  int get_log_count;
  int save_log_count;
};

base::Value ConvertToValue(const StateMetricsData& value);

bool ConvertFromValue(const base::Value* data, StateMetricsData* result);

class MetricsUtils {
 public:
  MetricsUtils() = default;
  virtual ~MetricsUtils() = default;

  // Record the metrics to the event-based metrics file, and wait for upload.
  virtual bool Record(scoped_refptr<JsonStore> json_store,
                      bool is_complete) = 0;

  template <typename T>
  static bool GetMetricsValue(scoped_refptr<JsonStore> json_store,
                              const std::string& key,
                              T* result) {
    base::Value metrics = base::Value(base::Value::Type::DICT);
    if (json_store->GetValue(kMetrics, &metrics)) {
      CHECK(metrics.is_dict());
    }
    return ConvertFromValue(metrics.FindKey(key), result);
  }

  template <typename T>
  static bool SetMetricsValue(scoped_refptr<JsonStore> json_store,
                              const std::string& key,
                              const T& v) {
    base::Value metrics = base::Value(base::Value::Type::DICT);
    if (json_store->GetValue(kMetrics, &metrics)) {
      CHECK(metrics.is_dict());
    }
    base::Value&& value = ConvertToValue(v);

    const base::Value* result = metrics.FindKey(key);
    if (!result || *result != value) {
      std::optional<base::Value> result_backup =
          result ? std::make_optional(result->Clone()) : std::nullopt;
      metrics.SetKey(key, std::move(value));

      return json_store->SetValue(kMetrics, std::move(metrics));
    }
    return true;
  }

  static bool UpdateStateMetricsOnAbort(scoped_refptr<JsonStore> json_store,
                                        RmadState::StateCase state_case,
                                        double timestamp);

  static bool UpdateStateMetricsOnStateTransition(
      scoped_refptr<JsonStore> json_store,
      RmadState::StateCase from,
      RmadState::StateCase to,
      double timestamp);

  static bool UpdateStateMetricsOnGetLog(scoped_refptr<JsonStore> json_store,
                                         RmadState::StateCase state_case);

  static bool UpdateStateMetricsOnSaveLog(scoped_refptr<JsonStore> json_store,
                                          RmadState::StateCase state_case);

  static std::string GetMetricsSummaryAsString(
      scoped_refptr<JsonStore> json_store);

 private:
  static bool SetStateSetupTimestamp(
      std::map<int, StateMetricsData>* state_metrics,
      RmadState::StateCase state_case,
      double setup_timestamp);

  static bool CalculateStateOverallTime(
      std::map<int, StateMetricsData>* state_metrics,
      RmadState::StateCase state_case,
      double leave_timestamp);

  static base::Value::Dict RefineStateMetricsReadability(
      const base::Value::Dict& original_state_metrics);
};

}  // namespace rmad

#endif  // RMAD_METRICS_METRICS_UTILS_H_
