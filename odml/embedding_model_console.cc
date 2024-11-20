// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include <algorithm>
#include <iostream>
#include <memory>
#include <string>

#include <base/command_line.h>
#include <base/files/file_util.h>
#include <base/functional/bind.h>
#include <base/memory/raw_ref.h>
#include <base/run_loop.h>
#include <base/task/single_thread_task_executor.h>
#include <base/task/single_thread_task_runner.h>
#include <base/task/thread_pool/thread_pool_instance.h>
#include <base/time/time.h>
#include <base/uuid.h>
#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>
#include <chromeos/mojo/service_constants.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/core/embedder/scoped_ipc_support.h>
#include <mojo/public/cpp/bindings/remote.h>
#include <mojo_service_manager/lib/connect.h>

#include "odml/mojom/embedding_model.mojom.h"
#include "odml/mojom/on_device_model.mojom.h"
#include "odml/mojom/on_device_model_service.mojom.h"

namespace {

using ::embedding_model::mojom::GenerateEmbeddingRequest;
using ::embedding_model::mojom::OnDeviceEmbeddingModel;
using ::embedding_model::mojom::OnDeviceEmbeddingModelInferenceError;
using ::embedding_model::mojom::OnDeviceEmbeddingModelService;
using ::embedding_model::mojom::TaskType;
using ::on_device_model::mojom::LoadModelResult;

// Switches for command line.
constexpr const char kUuid[] = "uuid";
constexpr const char kGenerateEmbedding[] = "generate_embedding";
constexpr const char kContent[] = "content";
constexpr const char kTaskType[] = "task_type";
constexpr const char kTruncateInput[] = "truncate_input";
constexpr const char kBinaryOutput[] = "binary_output";
// --benchmark specifies benchmark mode.
constexpr const char kBenchmark[] = "benchmark";
// --benchmark_run_count is the number of inference we'll make.
constexpr const char kBenchmarkRunCount[] = "benchmark_run_count";
// --benchmark_max_seconds is the approximate maximum time we'll run. If we
// cannot finish --benchmark_run_count within the specified time, we'll cut it
// short.
constexpr const char kBenchmarkMaxSeconds[] = "benchmark_max_seconds";

const constexpr char* kBenchmarkPrompts[] = {
    "Tokyo City Guide - What to do in Tokyo",
    "numpy.stack() in Python",
    "Mountain View, CA - Google Map",
    "Best Hiking Trails near San Francisco",
    "Understanding Quantum Mechanics",
    "Top 10 Restaurants in Paris",
    "How to Bake a Perfect Chocolate Cake",
    "Learning to Play the Guitar for Beginners",
    "The History of the Roman Empire",
    "The Benefits of Meditation",
    "Effective Time Management Strategies",
    "The Art of Public Speaking",
    "Climate Change and its Impact",
    "The Future of Artificial Intelligence",
    "Space Exploration and the Search for Life",
    "The Wonders of the Natural World",
    "Investing in the Stock Market",
    "The Psychology of Human Behavior",
    "The Power of Positive Thinking",
    "How to Write a Compelling Story"};
constexpr size_t kBenchmarkPromptsSize =
    sizeof(kBenchmarkPrompts) / sizeof(kBenchmarkPrompts[0]);

TaskType GetTaskTypeFromString(std::string s) {
  if (s == "clustering") {
    return TaskType::kClustering;
  }
  LOG(FATAL) << "Unknown TaskType: " << s;
}

constexpr int kMaxPrintEmbeddingCount = 16;

void PrintEmbedding(const std::vector<float>& embedding) {
  printf("Embedding: [\n");
  for (int i = 0; i < std::min(static_cast<int>(embedding.size()),
                               kMaxPrintEmbeddingCount);
       i++) {
    printf(" %.2f,", embedding[i]);
    if ((i + 1) % 4 == 0) {
      printf("\n");
    }
  }
  printf(" ]\n");
}

void WriteEmbedding(const std::vector<float>& embedding,
                    const std::string& path) {
  std::vector<uint8_t> embedding_content(embedding.size() * sizeof(float), 0);
  std::copy(
      reinterpret_cast<const uint8_t*>(embedding.data()),
      reinterpret_cast<const uint8_t*>(embedding.data() + embedding.size()),
      embedding_content.begin());

  bool ret = base::WriteFile(base::FilePath(path), embedding_content);
  CHECK(ret) << "Failed to write file.";
}

class EmbeddingBenchmark {
 public:
  EmbeddingBenchmark(raw_ref<mojo::Remote<OnDeviceEmbeddingModel>> model,
                     TaskType task_type)
      : model_(model), task_type_(task_type) {}

  void Run(int run_count,
           int max_seconds,
           const std::optional<base::FilePath>& output_json_path);

 private:
  // How many times do we want to run during the benchmark?
  int target_run_count_ = -1;
  // If we hit this amount of seconds, we'll cut the benchmark short.
  // This is needed because certain devices (such as CPU) is much slower and we
  // want to benchmark with a maximum time instead of specified number of tries.
  int max_seconds_;
  // How many times has we called GenerateEmbedding()?
  int launched_count_;
  // How many times has GenerateEmbedding() returned a result?
  int finish_count_;
  // How many GenerateEmbedding() calls are invoked but not yet finished?
  int in_flight_count_;
  // Has max_seconds_ been exceeded?
  bool has_been_cut_short_;
  // The start of benchmarking.
  base::TimeTicks start_;
  // End of benchmarking.
  base::TimeTicks end_;
  raw_ref<mojo::Remote<OnDeviceEmbeddingModel>> model_;
  TaskType task_type_;
  // If not empty, will write the result to the specified path.
  std::optional<base::FilePath> output_json_path_;
  std::unique_ptr<base::RunLoop> run_loop_;

  // This is the maximum number of concurrent request send to the embedding
  // service backend at the same time. A value of 2 is picked at the moment
  // because we want to ensure it's larger than 1 so that the roundtrip IPC
  // latency doesn't skew the benchmark result and it's not too large so we can
  // efficiently cut-off the benchmarking due to time limit. Also note that
  // we've a maximum in-flight request count because we're measuring throughput
  // and not latency. If we decide to measure latency in the future then we'll
  // need to remove such cap so as to not distort the percentile latency figures
  // contributed by queueing delay.
  static constexpr int kMaxInflightCount = 2;

  std::string GetContent(int idx);
  void LaunchOne();
  void OnFinish(OnDeviceEmbeddingModelInferenceError error,
                const std::vector<float>& embeddings);
  void PrintStats();
};

void EmbeddingBenchmark::Run(
    int run_count,
    int max_seconds,
    const std::optional<base::FilePath>& output_json_path) {
  CHECK_EQ(target_run_count_, -1);  // run() should only be called once.
  target_run_count_ = run_count;
  max_seconds_ = max_seconds;
  launched_count_ = 0;
  finish_count_ = 0;
  in_flight_count_ = 0;
  has_been_cut_short_ = false;
  output_json_path_ = output_json_path;
  run_loop_ = std::make_unique<base::RunLoop>();
  start_ = base::TimeTicks::Now();
  LaunchOne();
  run_loop_->Run();
  PrintStats();
}

std::string EmbeddingBenchmark::GetContent(int idx) {
  idx = idx % kBenchmarkPromptsSize;
  return std::string(kBenchmarkPrompts[idx]);
}

void EmbeddingBenchmark::LaunchOne() {
  GenerateEmbeddingRequest generate_embedding_request;
  generate_embedding_request.content = GetContent(launched_count_);
  generate_embedding_request.task_type = task_type_;
  generate_embedding_request.truncate_input = true;

  in_flight_count_++;
  launched_count_++;
  (*model_)->GenerateEmbedding(
      generate_embedding_request.Clone(),
      base::BindOnce(&EmbeddingBenchmark::OnFinish, base::Unretained(this)));
}

void EmbeddingBenchmark::OnFinish(OnDeviceEmbeddingModelInferenceError error,
                                  const std::vector<float>& embeddings) {
  base::TimeTicks current_time = base::TimeTicks::Now();
  in_flight_count_--;
  finish_count_++;
  CHECK_EQ(error, OnDeviceEmbeddingModelInferenceError::kSuccess);

  if (!has_been_cut_short_ &&
      ((current_time - start_) > base::Seconds(max_seconds_))) {
    // We've not finished yet, we need to cut it short and let all in-flight
    // requests finish.
    target_run_count_ = launched_count_;
    has_been_cut_short_ = true;
  }

  if (finish_count_ >= target_run_count_) {
    CHECK_EQ(finish_count_, target_run_count_);
    end_ = base::TimeTicks::Now();
    run_loop_->Quit();
    return;
  }

  if (launched_count_ < target_run_count_ &&
      in_flight_count_ < kMaxInflightCount) {
    LaunchOne();
  }
}

void EmbeddingBenchmark::PrintStats() {
  base::TimeDelta run_time = end_ - start_;
  std::cout << "Embedding benchmark result: " << run_time.InMilliseconds()
            << " ms for " << target_run_count_ << " invocations" << std::endl;
  if (output_json_path_.has_value()) {
    std::string json_out =
        "{\"runtime_ms\": " + std::to_string(run_time.InMilliseconds()) +
        ", \"count\": " + std::to_string(target_run_count_) + "}\n";
    bool ret = base::WriteFile(output_json_path_.value(), json_out);
    CHECK(ret) << "Failed to write JSON result file.";
  }
}

void RunBenchmark(raw_ref<mojo::Remote<OnDeviceEmbeddingModel>> model,
                  base::CommandLine* cl) {
  EmbeddingBenchmark bench(
      model, GetTaskTypeFromString(cl->GetSwitchValueASCII(kTaskType)));
  int run_count = 16;
  int max_seconds = 20;
  if (!cl->GetSwitchValueASCII(kBenchmarkRunCount).empty()) {
    run_count = std::stoi(cl->GetSwitchValueASCII(kBenchmarkRunCount));
  }
  if (!cl->GetSwitchValueASCII(kBenchmarkMaxSeconds).empty()) {
    max_seconds = std::stoi(cl->GetSwitchValueASCII(kBenchmarkMaxSeconds));
  }
  std::optional<base::FilePath> output_json_path;
  std::string output_json_option = cl->GetSwitchValueASCII(kBenchmark);
  if (!output_json_option.empty()) {
    output_json_path = base::FilePath(output_json_option);
  }
  bench.Run(run_count, max_seconds, output_json_path);
}

}  // namespace

int main(int argc, char** argv) {
  // Setup command line and logging.
  base::CommandLine::Init(argc, argv);
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);
  base::CommandLine* cl = base::CommandLine::ForCurrentProcess();

  const std::string uuid = cl->GetSwitchValueASCII(kUuid);
  CHECK(!uuid.empty());

  // Setup mojo
  base::ThreadPoolInstance::CreateAndStartWithDefaultParams("thread_pool");
  base::SingleThreadTaskExecutor io_task_executor(base::MessagePumpType::IO);
  mojo::core::Init();
  mojo::core::ScopedIPCSupport ipc_support(
      base::SingleThreadTaskRunner::GetCurrentDefault(),
      mojo::core::ScopedIPCSupport::ShutdownPolicy::CLEAN);

  // Obtain a remote to the service.
  mojo::Remote<OnDeviceEmbeddingModelService> service;
  mojo::Remote<chromeos::mojo_service_manager::mojom::ServiceManager>
      service_manager;
  auto service_manager_remote =
      chromeos::mojo_service_manager::ConnectToMojoServiceManager();

  if (!service_manager_remote) {
    LOG(ERROR) << "Failed to connect to Mojo Service Manager";
    return -1;
  }

  service_manager.Bind(std::move(service_manager_remote));
  service_manager.set_disconnect_with_reason_handler(
      base::BindOnce([](uint32_t error, const std::string& message) {
        LOG(INFO) << "Disconnected from mojo service manager (the mojo "
                     "broker process). Error: "
                  << error << ", message: " << message
                  << ". Shutdown and wait for respawn.";
      }));

  const base::TimeDelta kRemoteRequestTimeout = base::Milliseconds(10 * 1000);
  service_manager->Request(
      /*service_name=*/chromeos::mojo_services::kCrosEmbeddingModelService,
      /*timeout=*/kRemoteRequestTimeout,
      service.BindNewPipeAndPassReceiver().PassPipe());

  mojo::Remote<OnDeviceEmbeddingModel> model;

  {
    base::RunLoop run_loop;

    service->LoadEmbeddingModel(
        base::Uuid::ParseLowercase(uuid), model.BindNewPipeAndPassReceiver(),
        mojo::NullRemote(),
        base::BindOnce(
            [](base::RunLoop* run_loop, LoadModelResult result) {
              if (result == LoadModelResult::kSuccess) {
                LOG(INFO) << "LOADED";
              } else {
                LOG(ERROR) << "Fail";
                exit(0);
              }
              run_loop->Quit();
            },
            &run_loop));
    run_loop.Run();
  }

  if (cl->HasSwitch(kGenerateEmbedding) || cl->HasSwitch(kContent)) {
    std::string content = cl->GetSwitchValueASCII(kContent);
    GenerateEmbeddingRequest generate_embedding_request;
    generate_embedding_request.content = content;
    generate_embedding_request.task_type =
        GetTaskTypeFromString(cl->GetSwitchValueASCII(kTaskType));
    generate_embedding_request.truncate_input = false;
    if (cl->HasSwitch(kTruncateInput)) {
      generate_embedding_request.truncate_input = true;
    }

    {
      base::RunLoop run_loop;
      model->GenerateEmbedding(
          generate_embedding_request.Clone(),
          base::BindOnce(
              [](base::RunLoop* run_loop, base::CommandLine* cl,
                 OnDeviceEmbeddingModelInferenceError error,
                 const std::vector<float>& embeddings) {
                if (error == OnDeviceEmbeddingModelInferenceError::kSuccess) {
                  PrintEmbedding(embeddings);
                  if (cl->HasSwitch(kBinaryOutput)) {
                    WriteEmbedding(embeddings,
                                   cl->GetSwitchValueASCII(kBinaryOutput));
                  }
                } else {
                  LOG(ERROR)
                      << "Failed to generate embedding, error: " << error;
                }
                run_loop->Quit();
              },
              &run_loop, base::Unretained(cl)));

      run_loop.Run();
    }
  } else if (cl->HasSwitch(kBenchmark)) {
    RunBenchmark(raw_ref(model), cl);
  }

  return 0;
}
