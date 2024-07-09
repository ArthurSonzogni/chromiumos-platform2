// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/check.h>
#include <base/command_line.h>
#include <base/files/file_path.h>
#include <base/functional/bind.h>
#include <base/run_loop.h>
#include <base/strings/string_util.h>
#include <base/task/single_thread_task_executor.h>
#include <base/task/thread_pool/thread_pool_instance.h>
#include <base/uuid.h>
#include <chromeos/mojo/service_constants.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/core/embedder/scoped_ipc_support.h>
#include <mojo/public/cpp/bindings/pending_receiver.h>
#include <mojo/public/cpp/bindings/pending_remote.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <mojo/public/cpp/bindings/remote.h>
#include <mojo_service_manager/lib/connect.h>
#include <mojo_service_manager/lib/mojom/service_manager.mojom.h>

#include <iostream>
#include <utility>

#include "odml/mojom/on_device_model.mojom.h"
#include "odml/mojom/on_device_model_service.mojom.h"

namespace {
base::FilePath GetModelTestDataDir() {
  return base::FilePath("/tmp");
}

on_device_model::mojom::InputOptionsPtr MakeInput(const std::string& input) {
  auto options = on_device_model::mojom::InputOptions::New();
  options->text = input;
  options->ignore_context = false;
  // options->max_output_tokens = 128;
  return options;
}

class ResponseHolder : public on_device_model::mojom::StreamingResponder {
 public:
  ResponseHolder() = default;
  ~ResponseHolder() override = default;
  mojo::PendingRemote<on_device_model::mojom::StreamingResponder> BindRemote() {
    return receiver_.BindNewPipeAndPassRemote();
  }
  // const std::vector<std::string>& responses() const { return responses_; }
  void WaitForCompletion() { run_loop_.Run(); }
  void OnResponse(on_device_model::mojom::ResponseChunkPtr chunk) override {
    printf("%s", chunk->text.c_str());
    // responses_.push_back(chunk->text);
  }
  void OnComplete(on_device_model::mojom::ResponseSummaryPtr summary) override {
    run_loop_.Quit();
  }

 private:
  base::RunLoop run_loop_;
  mojo::Receiver<on_device_model::mojom::StreamingResponder> receiver_{this};
  // std::vector<std::string> responses_;
};

class ProgressObserver
    : public on_device_model::mojom::PlatformModelProgressObserver {
 public:
  explicit ProgressObserver(
      base::RepeatingCallback<void(double progress)> callback)
      : callback_(std::move(callback)) {}
  ~ProgressObserver() override = default;

  mojo::PendingRemote<on_device_model::mojom::PlatformModelProgressObserver>
  BindRemote() {
    return receiver_.BindNewPipeAndPassRemote();
  }

  void Progress(double progress) override { callback_.Run(progress); }

 private:
  mojo::Receiver<on_device_model::mojom::PlatformModelProgressObserver>
      receiver_{this};
  base::RepeatingCallback<void(double progress)> callback_;
};

}  // namespace

int main(int argc, char** argv) {
  base::CommandLine::Init(argc, argv);
  base::CommandLine* cl = base::CommandLine::ForCurrentProcess();

  std::string uuid = cl->GetSwitchValueASCII("uuid");
  CHECK(!uuid.empty());

  base::ThreadPoolInstance::CreateAndStartWithDefaultParams("thread_pool");

  base::SingleThreadTaskExecutor io_task_executor(base::MessagePumpType::IO);
  mojo::core::Init();

  mojo::core::ScopedIPCSupport ipc_support(
      base::SingleThreadTaskRunner::GetCurrentDefault(),
      mojo::core::ScopedIPCSupport::ShutdownPolicy::CLEAN);

  mojo::Remote<on_device_model::mojom::OnDeviceModelPlatformService> service;

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

  service_manager->Request(
      /*service_name=*/chromeos::mojo_services::kCrosOdmlService,
      /*timeout=*/std::nullopt,
      service.BindNewPipeAndPassReceiver().PassPipe());

  base::FilePath model_path = GetModelTestDataDir();

  mojo::Remote<on_device_model::mojom::OnDeviceModel> model;

  {
    base::RunLoop run_loop;
    ProgressObserver progress_observer(base::BindRepeating(
        [](double progress) { LOG(INFO) << "Progress: " << progress; }));

    service->LoadPlatformModel(
        base::Uuid::ParseLowercase(uuid), model.BindNewPipeAndPassReceiver(),
        progress_observer.BindRemote(),
        base::BindOnce(
            [](base::RunLoop* run_loop,
               on_device_model::mojom::LoadModelResult result) {
              if (result == on_device_model::mojom::LoadModelResult::kSuccess) {
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

  mojo::Remote<on_device_model::mojom::Session> session;
  model->StartSession(session.BindNewPipeAndPassReceiver());

  while (true) {
    printf("> ");
    ResponseHolder response;
    std::string input;
    std::getline(std::cin, input);
    input = base::TrimWhitespaceASCII(input, base::TRIM_ALL);
    session->Execute(MakeInput(input), response.BindRemote());
    response.WaitForCompletion();
    puts("");
    puts("-------------------");
  }
}
