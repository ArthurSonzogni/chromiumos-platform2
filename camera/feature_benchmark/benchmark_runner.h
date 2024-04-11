// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CAMERA_FEATURE_BENCHMARK_BENCHMARK_RUNNER_H_
#define CAMERA_FEATURE_BENCHMARK_BENCHMARK_RUNNER_H_

#include <string>

#include <base/files/file_path.h>
#include <base/test/task_environment.h>
#include <base/values.h>

#include "feature_benchmark/metrics.h"

namespace cros::tests {

class BenchmarkConfig {
 public:
  // config file should be json of the following format.
  // {
  //  "fps": (int),
  //  ...
  // },
  BenchmarkConfig(const base::FilePath& config_file_path,
                  const std::string& test_case_name);
  BenchmarkConfig(const BenchmarkConfig&) = delete;
  BenchmarkConfig& operator=(const BenchmarkConfig&) = delete;
  BenchmarkConfig(BenchmarkConfig&&) = delete;
  BenchmarkConfig& operator=(const BenchmarkConfig&&) = delete;

  const std::string& test_case_name() const { return test_case_name_; }
  float fps() const { return fps_; }
  const base::Value::Dict& test_case_config() const {
    return test_case_config_;
  }

 private:
  std::string test_case_name_;
  float fps_;
  base::Value::Dict test_case_config_;
};

class BenchmarkRunner {
 public:
  explicit BenchmarkRunner(const base::FilePath& data_dir);
  BenchmarkRunner(const BenchmarkRunner&) = delete;
  BenchmarkRunner& operator=(const BenchmarkRunner&) = delete;
  BenchmarkRunner(BenchmarkRunner&&) = delete;
  BenchmarkRunner& operator=(const BenchmarkRunner&&) = delete;
  virtual ~BenchmarkRunner() = default;

  bool InitializeWithLatencyMeasured();
  void RunWithLatencyMeasured(base::TimeDelta& process_time);
  void OutputMetricsToJsonFile(const base::FilePath& output_file_path) {
    metrics_.OutputMetricsToJsonFile(output_file_path);
  }

 protected:
  virtual bool Initialize() = 0;
  virtual void Run() = 0;
  const base::FilePath& data_dir() const { return data_dir_; }

 private:
  base::FilePath data_dir_;
  Metrics metrics_;
};

}  // namespace cros::tests

#endif  // CAMERA_FEATURE_BENCHMARK_BENCHMARK_RUNNER_H_
