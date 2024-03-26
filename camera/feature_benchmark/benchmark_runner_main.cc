// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <numeric>
#include <string>
#include <vector>

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
                  int running_time_sec) {
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
  CHECK(benchmark_runner->Initialize());
  VLOGF(2) << "Initialization time of the feature is "
           << initialize_timer.Elapsed();

  int fps = benchmark_config.fps();
  base::ElapsedTimer total_timer;
  base::TimeDelta total_running_time = base::Seconds(running_time_sec);
  base::TimeDelta max_latency = base::Seconds(1.0 / fps);
  base::TimeDelta process_time;
  base::TimeDelta avg_process_time = base::Seconds(0);

  int count = 0;
  while (total_timer.Elapsed() < total_running_time) {
    base::ElapsedTimer run_once_timer;
    benchmark_runner->Run();
    process_time = run_once_timer.Elapsed();
    avg_process_time = (avg_process_time * count + process_time) / (count + 1);
    if (max_latency > process_time) {
      base::PlatformThread::Sleep(max_latency - process_time);
    }
    ++count;
  }

  LOGF(INFO) << "The avg time of the feature running in milliseconds is: "
             << avg_process_time.InMillisecondsF() << " with counts: " << count
             << " and fps: " << count / total_timer.Elapsed().InSecondsF();
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
  brillo::FlagHelper::Init(argc, argv, "Cros Camera feature benchmark");
  base::test::SingleThreadTaskEnvironment task_environment;

  auto test_config_file_path = base::FilePath(FLAGS_test_config_file_path);
  cros::tests::BenchmarkConfig benchmark_config(test_config_file_path,
                                                FLAGS_test_case_name);
  cros::tests::RunBenchmark(benchmark_config, test_config_file_path.DirName(),
                            FLAGS_min_running_time_sec);
}
