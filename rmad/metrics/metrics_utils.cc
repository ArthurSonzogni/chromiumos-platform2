// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/metrics/metrics_utils.h"

#include <map>

#include <base/json/json_string_value_serializer.h>
#include <base/logging.h>
#include <base/memory/scoped_refptr.h>

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
  base::Value dict(base::Value::Type::DICTIONARY);
  dict.SetKey(kStateCase, ConvertToValue(static_cast<int>(state_case)));
  dict.SetKey(kStateIsAborted, ConvertToValue(is_aborted));
  dict.SetKey(kStateSetupTimestamp, ConvertToValue(setup_timestamp));
  dict.SetKey(kStateOverallTime, ConvertToValue(overall_time));
  dict.SetKey(kStateTransitionsCount, ConvertToValue(transition_count));
  dict.SetKey(kStateGetLogCount, ConvertToValue(get_log_count));
  dict.SetKey(kStateSaveLogCount, ConvertToValue(save_log_count));
  return dict;
}

bool StateMetricsData::FromValue(const base::Value* data) {
  if (!data || !data->is_dict()) {
    return false;
  }

  if (auto state_case_it = data->FindKey(kStateCase);
      state_case_it && state_case_it->GetIfInt().has_value()) {
    state_case = static_cast<RmadState::StateCase>(state_case_it->GetInt());
  } else {
    return false;
  }
  if (auto is_aborted_it = data->FindKey(kStateIsAborted);
      is_aborted_it && is_aborted_it->GetIfBool().has_value()) {
    is_aborted = is_aborted_it->GetBool();
  } else {
    return false;
  }
  if (auto setup_timestamp_it = data->FindKey(kStateSetupTimestamp);
      setup_timestamp_it && setup_timestamp_it->GetIfDouble().has_value()) {
    setup_timestamp = setup_timestamp_it->GetDouble();
  } else {
    return false;
  }
  if (auto overall_time_it = data->FindKey(kStateOverallTime);
      overall_time_it && overall_time_it->GetIfDouble().has_value()) {
    overall_time = overall_time_it->GetDouble();
  } else {
    return false;
  }
  if (auto transition_count_it = data->FindKey(kStateTransitionsCount);
      transition_count_it && transition_count_it->GetIfInt().has_value()) {
    transition_count = transition_count_it->GetInt();
  } else {
    return false;
  }
  if (auto get_log_count_it = data->FindKey(kStateGetLogCount);
      get_log_count_it && get_log_count_it->GetIfInt().has_value()) {
    get_log_count = get_log_count_it->GetInt();
  } else {
    return false;
  }
  if (auto save_log_count_it = data->FindKey(kStateSaveLogCount);
      save_log_count_it && save_log_count_it->GetIfInt().has_value()) {
    save_log_count = save_log_count_it->GetInt();
  } else {
    return false;
  }

  return true;
}

base::Value ConvertToValue(const StateMetricsData& value) {
  return value.ToValue();
}

bool ConvertFromValue(const base::Value* data, StateMetricsData* result) {
  if (!data || !result) {
    return false;
  }

  return result->FromValue(data);
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

  if (from != RmadState::STATE_NOT_SET) {
    int key = static_cast<int>(from);
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
  base::Value metrics = base::Value(base::Value::Type::DICTIONARY);
  if (json_store->GetValue(kMetrics, &metrics)) {
    CHECK(metrics.is_dict());
  }
  metrics.RemoveKey(kFirstSetupTimestamp);
  metrics.RemoveKey(kSetupTimestamp);

  base::Value* state_metrics = metrics.FindKey(kStateMetrics);
  if (state_metrics && state_metrics->is_dict()) {
    for (auto [state_case, metrics_data] : state_metrics->DictItems()) {
      metrics_data.RemoveKey(kStateSetupTimestamp);
    }
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
    LOG(ERROR) << "Failed to get state metrics to calculate.";
    return false;
  }

  if ((*state_metrics)[key].setup_timestamp == 0.0) {
    LOG(ERROR) << "Failed to get timestamp when state is setup.";
    return false;
  }

  double time_spent_sec =
      leave_timestamp - (*state_metrics)[key].setup_timestamp;
  if (time_spent_sec < 0) {
    LOG(ERROR) << "Failed to calculate time spent.";
    return false;
  }

  (*state_metrics)[key].overall_time += time_spent_sec;
  (*state_metrics)[key].setup_timestamp = leave_timestamp;

  return true;
}

}  // namespace rmad
