// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include <algorithm>

#include <base/command_line.h>
#include <base/functional/bind.h>
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

TaskType GetTaskTypeFromString(std::string s) {
  if (s == "clustering")
    return TaskType::kClustering;
  LOG(FATAL) << "Unknown TaskType: " << s;
}

constexpr int kMaxPrintEmbeddingCount = 16;

void PrintEmbedding(const std::vector<float>& embedding) {
  printf("Embedding: [\n");
  for (int i = 0; i < std::min(static_cast<int>(embedding.size()),
                               kMaxPrintEmbeddingCount);
       i++) {
    printf(" %.2f,", embedding[i]);
    if ((i + 1) % 4 == 0)
      printf("\n");
  }
  printf(" ]\n");
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
              [](base::RunLoop* run_loop,
                 OnDeviceEmbeddingModelInferenceError error,
                 const std::vector<float>& embeddings) {
                if (error == OnDeviceEmbeddingModelInferenceError::kSuccess) {
                  PrintEmbedding(embeddings);
                } else {
                  LOG(ERROR)
                      << "Failed to generate embedding, error: " << error;
                }
                run_loop->Quit();
              },
              &run_loop));

      run_loop.Run();
    }
  }

  return 0;
}
