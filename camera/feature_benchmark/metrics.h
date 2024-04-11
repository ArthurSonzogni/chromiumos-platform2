// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CAMERA_FEATURE_BENCHMARK_METRICS_H_
#define CAMERA_FEATURE_BENCHMARK_METRICS_H_

#include <map>
#include <string>
#include <vector>

#include <base/values.h>
#include <base/files/file_path.h>

namespace cros::tests {

class Metrics {
 public:
  Metrics() = default;
  Metrics(const Metrics&) = delete;
  Metrics& operator=(const Metrics&) = delete;
  Metrics(Metrics&&) = delete;
  Metrics& operator=(const Metrics&&) = delete;

  void AddMetric(const std::string& name,
                 const std::string& unit,
                 bool bigger_is_better);

  // A metric name must be registered via AddMetric before calling
  // AddMetricSample with the name.
  void AddMetricSample(const std::string& name, double val);
  void OutputMetricsToJsonFile(const base::FilePath& output_file_path);

 private:
  void CalculateStatistics();

  struct Statistics {
    double avg;
    double stddev;
    double min;
    double max;
    int count;
  };
  struct Metric {
    std::string unit;
    bool bigger_is_better;
    std::vector<double> samples;
    Statistics statistics;
  };
  using MetricName = std::string;

  std::map<MetricName, Metric> metric_dict_;
};

}  // namespace cros::tests

#endif  // CAMERA_FEATURE_BENCHMARK_METRICS_H_
