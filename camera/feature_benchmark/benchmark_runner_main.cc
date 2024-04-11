// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>

#include <base/command_line.h>
#include <base/files/file_path.h>
#include <base/test/task_environment.h>
#include <base/test/test_timeouts.h>
#include "base/threading/platform_thread.h"
#include <base/timer/elapsed_timer.h>
#include <brillo/flag_helper.h>

#include "cros-camera/common.h"
#include "feature_benchmark/benchmark_runner.h"
#include "features/face_detection/face_detection_benchmark.h"

namespace cros::tests {

void RunBenchmark(BenchmarkConfig& benchmark_config,
                  const base::FilePath& data_dir,
                  int running_time_sec,
                  const base::FilePath& metrics_output_json_path) {
  std::unique_ptr<BenchmarkRunner> benchmark_runner;
  constexpr char kTestCaseFaceDetectionPrefix[] = "face_detection";
  const std::string& test_case_name = benchmark_config.test_case_name();
  if (test_case_name.starts_with(kTestCaseFaceDetectionPrefix)) {
    benchmark_runner =
        std::make_unique<FaceDetectionBenchmark>(benchmark_config, data_dir);
  } else {
    LOGF(FATAL) << "Unknown feature of test case name: " << test_case_name;
  }

  base::ElapsedTimer initialize_timer;
  CHECK(benchmark_runner->InitializeWithLatencyMeasured());
  VLOGF(2) << "Initialization time of the feature is "
           << initialize_timer.Elapsed();

  int fps = benchmark_config.fps();
  base::ElapsedTimer total_timer;
  base::TimeDelta total_running_time = base::Seconds(running_time_sec);
  base::TimeDelta max_latency = base::Seconds(1.0 / fps);
  base::TimeDelta process_time;

  int count = 0;
  while (total_timer.Elapsed() < total_running_time) {
    benchmark_runner->RunWithLatencyMeasured(process_time);
    if (max_latency > process_time) {
      base::PlatformThread::Sleep(max_latency - process_time);
    }
    ++count;
  }

  LOGF(INFO) << "The avg fps of running the benchmark is "
             << count / total_timer.Elapsed().InSecondsF();

  benchmark_runner->OutputMetricsToJsonFile(metrics_output_json_path);
}

}  // namespace cros::tests

int main(int argc, char** argv) {
  base::CommandLine::Init(argc, argv);
  TestTimeouts::Initialize();
  LOG_ASSERT(logging::InitLogging(logging::LoggingSettings()));
  DEFINE_string(test_config_file_path, "",
                "The json config file for the test.");
  DEFINE_string(test_case_name, "", "The test case name of the feature.");
  DEFINE_int32(min_running_time_sec, 0,
               "The minimum time that the feature should be keep running for "
               "in seconds");
  constexpr char kMetricOutputJsonPathDefault[] =
      "/tmp/feature_benchmark_metrics.json";
  DEFINE_string(metrics_output_json_path, kMetricOutputJsonPathDefault,
                "The path of the metrics output JSON file.");
  brillo::FlagHelper::Init(argc, argv, "Cros Camera feature benchmark");
  base::test::SingleThreadTaskEnvironment task_environment;

  auto test_config_file_path = base::FilePath(FLAGS_test_config_file_path);
  cros::tests::BenchmarkConfig benchmark_config(test_config_file_path,
                                                FLAGS_test_case_name);
  cros::tests::RunBenchmark(benchmark_config, test_config_file_path.DirName(),
                            FLAGS_min_running_time_sec,
                            base::FilePath(FLAGS_metrics_output_json_path));
}
