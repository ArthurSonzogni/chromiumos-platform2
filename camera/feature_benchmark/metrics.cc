// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "feature_benchmark/metrics.h"

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/json/json_writer.h>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <utility>

#include "cros-camera/common.h"

namespace cros::tests {

namespace {

double Stddev(const std::vector<double>& data, double avg) {
  std::vector<double> diff(data.size());
  std::transform(data.begin(), data.end(), diff.begin(),
                 [avg](double x) { return x - avg; });
  double sq_sum =
      std::inner_product(diff.begin(), diff.end(), diff.begin(), 0.0);
  return std::sqrt(sq_sum / data.size());
}

}  // namespace

void Metrics::AddMetric(const std::string& name,
                        const std::string& unit,
                        bool bigger_is_better) {
  auto it = metric_dict_.find(name);
  CHECK(it == metric_dict_.end());
  metric_dict_[name] = {.unit = unit, .bigger_is_better = bigger_is_better};
}

void Metrics::AddMetricSample(const std::string& name, double val) {
  auto it = metric_dict_.find(name);
  CHECK(it != metric_dict_.end());
  it->second.samples.push_back(val);
}

void Metrics::OutputMetricsToJsonFile(const base::FilePath& output_file_path) {
  CalculateStatistics();

  base::Value::List json_output;
  constexpr char kFunctionNameKey[] = "function_name";
  constexpr char kMetricNameKey[] = "metric_name";
  constexpr char kAvgMetricName[] = "avg";
  constexpr char kStddevMetricName[] = "stddev";
  constexpr char kMinMetricName[] = "min";
  constexpr char kMaxMetricName[] = "max";
  constexpr char kCountMetricName[] = "count";
  constexpr char kUnitKey[] = "unit";
  constexpr char kCountUnit[] = "count";
  constexpr char kValueKey[] = "value";
  constexpr char kBiggerIsBetterKey[] = "bigger_is_better";

  // Following the spec of FunctionMetrics in
  // //camera/tracing/sql/camera_core_metrics.proto
  for (auto const& [name, metric] : metric_dict_) {
    json_output.Append(
        base::Value::Dict()
            .Set(kFunctionNameKey, name)
            .Set(kMetricNameKey, kAvgMetricName)
            .Set(kUnitKey, metric.unit)
            .Set(kValueKey, static_cast<int>(metric.statistics.avg))
            .Set(kBiggerIsBetterKey, metric.bigger_is_better));
    json_output.Append(
        base::Value::Dict()
            .Set(kFunctionNameKey, name)
            .Set(kMetricNameKey, kStddevMetricName)
            .Set(kUnitKey, metric.unit)
            .Set(kValueKey, static_cast<int>(metric.statistics.stddev))
            .Set(kBiggerIsBetterKey, false));
    json_output.Append(
        base::Value::Dict()
            .Set(kFunctionNameKey, name)
            .Set(kMetricNameKey, kMinMetricName)
            .Set(kUnitKey, metric.unit)
            .Set(kValueKey, static_cast<int>(metric.statistics.min))
            .Set(kBiggerIsBetterKey, metric.bigger_is_better));
    json_output.Append(
        base::Value::Dict()
            .Set(kFunctionNameKey, name)
            .Set(kMetricNameKey, kMaxMetricName)
            .Set(kUnitKey, metric.unit)
            .Set(kValueKey, static_cast<int>(metric.statistics.max))
            .Set(kBiggerIsBetterKey, metric.bigger_is_better));
    json_output.Append(
        base::Value::Dict()
            .Set(kFunctionNameKey, name)
            .Set(kMetricNameKey, kCountMetricName)
            .Set(kUnitKey, kCountUnit)
            .Set(kValueKey, static_cast<int>(metric.statistics.count))
            .Set(kBiggerIsBetterKey, true));
  }

  std::string json_string;
  base::JSONWriter::WriteWithOptions(
      json_output, base::JSONWriter::OPTIONS_PRETTY_PRINT, &json_string);
  if (base::WriteFile(output_file_path, json_string.data(),
                      json_string.size()) < 0) {
    LOGF(FATAL) << "Failed to write metrics JSON to path: " << output_file_path;
  }
}

void Metrics::CalculateStatistics() {
  for (auto& [metric_name, metric] : metric_dict_) {
    if (metric.samples.empty()) {
      metric.statistics = {
          .avg = 0,
          .stddev = 0,
          .min = 0,
          .max = 0,
          .count = 0,
      };
      continue;
    }

    metric.statistics.avg =
        std::reduce(metric.samples.begin(), metric.samples.end()) /
        metric.samples.size();
    metric.statistics.stddev = Stddev(metric.samples, metric.statistics.avg);
    metric.statistics.min =
        *std::min_element(metric.samples.begin(), metric.samples.end());
    metric.statistics.max =
        *std::max_element(metric.samples.begin(), metric.samples.end());
    metric.statistics.count = metric.samples.size();
  }
}

}  // namespace cros::tests
