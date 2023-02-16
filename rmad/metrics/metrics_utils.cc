// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/metrics/metrics_utils.h"

#include <map>

#include <base/json/json_string_value_serializer.h>
#include <base/logging.h>
#include <base/memory/scoped_refptr.h>

#include "rmad/constants.h"
#include "rmad/proto_bindings/rmad.pb.h"
#include "rmad/utils/json_store.h"

namespace rmad {

bool StateMetricsData::operator==(const StateMetricsData& other) const {
  return state_case == other.state_case && is_aborted == other.is_aborted &&
         setup_timestamp == other.setup_timestamp &&
         overall_time == other.overall_time &&
         transition_count == other.transition_count &&
         get_log_count == other.get_log_count &&
         save_log_count == other.save_log_count;
}

base::Value StateMetricsData::ToValue() const {
  base::Value::Dict dict;
  dict.Set(kStateCase, static_cast<int>(state_case));
  dict.Set(kStateIsAborted, is_aborted);
  dict.Set(kStateSetupTimestamp, setup_timestamp);
  dict.Set(kStateOverallTime, overall_time);
  dict.Set(kStateTransitionsCount, transition_count);
  dict.Set(kStateGetLogCount, get_log_count);
  dict.Set(kStateSaveLogCount, save_log_count);
  return base::Value(std::move(dict));
}

bool StateMetricsData::FromValue(const base::Value* value) {
  if (!value || !value->is_dict()) {
    return false;
  }
  const base::Value::Dict& dict = value->GetDict();

  if (auto state_case_it = dict.FindInt(kStateCase)) {
    state_case = static_cast<RmadState::StateCase>(*state_case_it);
  } else {
    return false;
  }
  if (auto is_aborted_it = dict.FindBool(kStateIsAborted)) {
    is_aborted = *is_aborted_it;
  } else {
    return false;
  }
  if (auto setup_timestamp_it = dict.FindDouble(kStateSetupTimestamp)) {
    setup_timestamp = *setup_timestamp_it;
  } else {
    return false;
  }
  if (auto overall_time_it = dict.FindDouble(kStateOverallTime)) {
    overall_time = *overall_time_it;
  } else {
    return false;
  }
  if (auto transition_count_it = dict.FindInt(kStateTransitionsCount)) {
    transition_count = *transition_count_it;
  } else {
    return false;
  }
  if (auto get_log_count_it = dict.FindInt(kStateGetLogCount)) {
    get_log_count = *get_log_count_it;
  } else {
    return false;
  }
  if (auto save_log_count_it = dict.FindInt(kStateSaveLogCount)) {
    save_log_count = *save_log_count_it;
  } else {
    return false;
  }

  return true;
}

base::Value ConvertToValue(const StateMetricsData& data) {
  return data.ToValue();
}

bool ConvertFromValue(const base::Value* value, StateMetricsData* data) {
  if (!value || !data) {
    return false;
  }

  return data->FromValue(value);
}

bool MetricsUtils::UpdateStateMetricsOnAbort(
    scoped_refptr<JsonStore> json_store,
    RmadState::StateCase state_case,
    double timestamp) {
  int key = static_cast<int>(state_case);
  if (!UpdateStateMetricsOnStateTransition(
          json_store, state_case, RmadState::STATE_NOT_SET, timestamp)) {
    LOG(ERROR) << "Failed to calculate metrics for state " << key;
    return false;
  }

  std::map<int, StateMetricsData> state_metrics;
  GetMetricsValue(json_store, kStateMetrics, &state_metrics);
  state_metrics[key].is_aborted = true;
  return SetMetricsValue(json_store, kStateMetrics, state_metrics);
}

bool MetricsUtils::UpdateStateMetricsOnStateTransition(
    scoped_refptr<JsonStore> json_store,
    RmadState::StateCase from,
    RmadState::StateCase to,
    double timestamp) {
  std::map<int, StateMetricsData> state_metrics;
  // At the beginning, we may have no data, so ignore the return value.
  GetMetricsValue(json_store, kStateMetrics, &state_metrics);

  if (from != RmadState::STATE_NOT_SET && to != RmadState::STATE_NOT_SET) {
    int key = static_cast<int>(to);
    state_metrics[key].transition_count++;
  }

  if (!CalculateStateOverallTime(&state_metrics, from, timestamp) ||
      !SetStateSetupTimestamp(&state_metrics, to, timestamp)) {
    return false;
  }

  return SetMetricsValue(json_store, kStateMetrics, state_metrics);
}

bool MetricsUtils::UpdateStateMetricsOnGetLog(
    scoped_refptr<JsonStore> json_store, RmadState::StateCase state_case) {
  int key = static_cast<int>(state_case);
  std::map<int, StateMetricsData> state_metrics;
  // At the beginning, we may have no data, so ignore the return value.
  GetMetricsValue(json_store, kStateMetrics, &state_metrics);

  state_metrics[key].get_log_count++;
  return SetMetricsValue(json_store, kStateMetrics, state_metrics);
}

bool MetricsUtils::UpdateStateMetricsOnSaveLog(
    scoped_refptr<JsonStore> json_store, RmadState::StateCase state_case) {
  int key = static_cast<int>(state_case);
  std::map<int, StateMetricsData> state_metrics;
  // At the beginning, we may have no data, so ignore the return value.
  GetMetricsValue(json_store, kStateMetrics, &state_metrics);

  state_metrics[key].save_log_count++;
  return SetMetricsValue(json_store, kStateMetrics, state_metrics);
}

std::string MetricsUtils::GetMetricsSummaryAsString(
    scoped_refptr<JsonStore> json_store) {
  base::Value metrics;
  if (!json_store->GetValue(kMetrics, &metrics)) {
    return "";
  }

  // Since the type might change if we successfully get the value from the json
  // store, we need to check here.
  CHECK(metrics.is_dict());
  // Remove timestamps for the entire process.
  metrics.GetDict().Remove(kMetricsFirstSetupTimestamp);
  metrics.GetDict().Remove(kMetricsSetupTimestamp);

  // Refine readability of state metrics for better understanding.
  const base::Value* original_state_metrics =
      metrics.GetDict().Find(kStateMetrics);
  if (original_state_metrics && original_state_metrics->is_dict()) {
    metrics.GetDict().Set(
        kStateMetrics,
        RefineStateMetricsReadability(original_state_metrics->GetDict()));
  }

  std::string output;
  JSONStringValueSerializer serializer(&output);
  serializer.set_pretty_print(true);
  serializer.Serialize(metrics);

  return output;
}

bool MetricsUtils::SetStateSetupTimestamp(
    std::map<int, StateMetricsData>* state_metrics,
    RmadState::StateCase state_case,
    double setup_timestamp) {
  if (state_case == RmadState::STATE_NOT_SET) {
    return true;
  }

  int key = static_cast<int>(state_case);
  (*state_metrics)[key].setup_timestamp = setup_timestamp;
  (*state_metrics)[key].state_case = state_case;
  return true;
}

bool MetricsUtils::CalculateStateOverallTime(
    std::map<int, StateMetricsData>* state_metrics,
    RmadState::StateCase state_case,
    double leave_timestamp) {
  if (state_case == RmadState::STATE_NOT_SET) {
    return true;
  }

  int key = static_cast<int>(state_case);
  if (state_metrics->find(key) == state_metrics->end()) {
    LOG(ERROR) << key << ": Failed to get state metrics to calculate.";
    return false;
  }

  if ((*state_metrics)[key].setup_timestamp < 0.0) {
    LOG(ERROR) << key << ": Invalid setup timestamp: "
               << (*state_metrics)[key].setup_timestamp << " is less than 0.";
    return false;
  }

  double time_spent_sec =
      leave_timestamp - (*state_metrics)[key].setup_timestamp;
  if (time_spent_sec < 0) {
    LOG(ERROR) << key << ": Failed to calculate time spent.";
    return false;
  }

  (*state_metrics)[key].overall_time += time_spent_sec;
  (*state_metrics)[key].setup_timestamp = leave_timestamp;

  return true;
}

base::Value::Dict MetricsUtils::RefineStateMetricsReadability(
    const base::Value::Dict& original_state_metrics) {
  base::Value::Dict new_state_metrics;
  for (const auto& [state_case_str, metrics_data] : original_state_metrics) {
    // For each state, we should have a dict to store metrics data.
    CHECK(metrics_data.is_dict());
    auto it = kStateNames.end();
    if (int state_case; base::StringToInt(state_case_str, &state_case)) {
      it = kStateNames.find(static_cast<RmadState::StateCase>(state_case));
      if (it != kStateNames.end()) {
        // Remap state_cases to names and remove timestamps for all states.
        auto new_metrics_data = metrics_data.Clone();
        new_metrics_data.GetDict().Remove(kStateSetupTimestamp);
        new_state_metrics.Set(it->second, std::move(new_metrics_data));
      }
    }
  }
  return new_state_metrics;
}

}  // namespace rmad
