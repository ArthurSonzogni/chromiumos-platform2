// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>
#include <utility>

#include <base/check.h>
#include <base/command_line.h>
#include <base/files/file_path.h>
#include <base/functional/bind.h>
#include <base/run_loop.h>
#include <base/strings/string_number_conversions.h>
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

#include "odml/mojom/on_device_model.mojom.h"
#include "odml/mojom/on_device_model_service.mojom.h"

namespace {

using ::on_device_model::mojom::FormatFeature;
using ::on_device_model::mojom::InputOptions;
using ::on_device_model::mojom::InputOptionsPtr;
using ::on_device_model::mojom::LoadModelResult;
using ::on_device_model::mojom::OnDeviceModel;
using ::on_device_model::mojom::OnDeviceModelPlatformService;
using ::on_device_model::mojom::PerformanceClass;
using ::on_device_model::mojom::PlatformModelProgressObserver;
using ::on_device_model::mojom::ResponseChunkPtr;
using ::on_device_model::mojom::ResponseSummaryPtr;
using ::on_device_model::mojom::SafetyFeature;
using ::on_device_model::mojom::SafetyInfoPtr;
using ::on_device_model::mojom::Session;
using ::on_device_model::mojom::StreamingResponder;

constexpr const char kUuid[] = "uuid";
constexpr const char kFormat[] = "format";
constexpr const char kFormatField[] = "format_field";
constexpr const char kRequestSafety[] = "request_safety";
constexpr const char kResponseSafety[] = "response_safety";

base::FilePath GetModelTestDataDir() {
  return base::FilePath("/tmp");
}

InputOptionsPtr MakeInput(const std::string& input) {
  auto options = InputOptions::New();
  options->text = input;
  options->ignore_context = false;
  return options;
}

bool ValidateSafetyResult(OnDeviceModelPlatformService& service,
                          OnDeviceModel& model,
                          uint32_t safety_feature,
                          const std::string& input) {
  base::RunLoop run_loop0;
  SafetyInfoPtr safety_info;
  model.ClassifyTextSafety(
      input, base::BindOnce(
                 [](base::RunLoop* run_loop, SafetyInfoPtr* target,
                    SafetyInfoPtr result) {
                   *target = std::move(result);
                   run_loop->Quit();
                 },
                 &run_loop0, &safety_info));
  run_loop0.Run();

  bool result;
  base::RunLoop run_loop1;
  service.ValidateSafetyResult(
      static_cast<SafetyFeature>(safety_feature), input, std::move(safety_info),
      base::BindOnce(
          [](base::RunLoop* run_loop, bool* target, bool result) {
            *target = result;
            run_loop->Quit();
          },
          &run_loop1, &result));
  run_loop1.Run();

  return result;
}

class ResponseHolder : public StreamingResponder {
 public:
  ResponseHolder() = default;
  ~ResponseHolder() override = default;
  mojo::PendingRemote<StreamingResponder> BindRemote() {
    return receiver_.BindNewPipeAndPassRemote();
  }
  const std::string& WaitForCompletion() {
    run_loop_.Run();
    return response_;
  }
  void OnResponse(ResponseChunkPtr chunk) override {
    printf("%s", chunk->text.c_str());
    response_ += chunk->text;
  }
  void OnComplete(ResponseSummaryPtr summary) override { run_loop_.Quit(); }

 private:
  base::RunLoop run_loop_;
  mojo::Receiver<StreamingResponder> receiver_{this};
  std::string response_;
};

class ProgressObserver : public PlatformModelProgressObserver {
 public:
  explicit ProgressObserver(
      base::RepeatingCallback<void(double progress)> callback)
      : callback_(std::move(callback)) {}
  ~ProgressObserver() override = default;

  mojo::PendingRemote<PlatformModelProgressObserver> BindRemote() {
    return receiver_.BindNewPipeAndPassRemote();
  }

  void Progress(double progress) override { callback_.Run(progress); }

 private:
  mojo::Receiver<PlatformModelProgressObserver> receiver_{this};
  base::RepeatingCallback<void(double progress)> callback_;
};

std::string FormatInput(OnDeviceModelPlatformService& service,
                        const base::Uuid& uuid,
                        uint32_t format_feature,
                        const std::string& input_field,
                        const std::string& input) {
  std::optional<std::string> result;
  base::RunLoop run_loop;
  service.FormatInput(
      uuid, static_cast<FormatFeature>(format_feature), {{input_field, input}},
      base::BindOnce(
          [](base::RunLoop* run_loop, std::optional<std::string>* target,
             const std::optional<std::string>& result) {
            *target = result;
            run_loop->Quit();
          },
          &run_loop, &result));
  run_loop.Run();

  CHECK(result.has_value()) << "Failed to format the input";
  return *result;
}

}  // namespace

int main(int argc, char** argv) {
  base::CommandLine::Init(argc, argv);
  base::CommandLine* cl = base::CommandLine::ForCurrentProcess();

  std::string uuid = cl->GetSwitchValueASCII(kUuid);
  CHECK(!uuid.empty());

  std::optional<uint32_t> format_feature;
  std::string format_field;
  if (cl->HasSwitch(kFormat)) {
    format_feature = 0;
    CHECK(
        base::StringToUint(cl->GetSwitchValueASCII(kFormat), &*format_feature));
    format_field = cl->GetSwitchValueASCII(kFormatField);
    CHECK(!format_field.empty());
  }

  std::optional<uint32_t> request_safety;
  if (cl->HasSwitch(kRequestSafety)) {
    request_safety = 0;
    CHECK(base::StringToUint(cl->GetSwitchValueASCII(kRequestSafety),
                             &*request_safety));
  }

  std::optional<uint32_t> response_safety;
  if (cl->HasSwitch(kResponseSafety)) {
    response_safety = 0;
    CHECK(base::StringToUint(cl->GetSwitchValueASCII(kResponseSafety),
                             &*response_safety));
  }

  base::ThreadPoolInstance::CreateAndStartWithDefaultParams("thread_pool");

  base::SingleThreadTaskExecutor io_task_executor(base::MessagePumpType::IO);
  mojo::core::Init();

  mojo::core::ScopedIPCSupport ipc_support(
      base::SingleThreadTaskRunner::GetCurrentDefault(),
      mojo::core::ScopedIPCSupport::ShutdownPolicy::CLEAN);

  mojo::Remote<OnDeviceModelPlatformService> service;

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

  {
    base::RunLoop run_loop;
    service->GetEstimatedPerformanceClass(base::BindOnce(
        [](base::RunLoop* run_loop, PerformanceClass result) {
          LOG(INFO) << result;
          run_loop->Quit();
        },
        &run_loop));
    run_loop.Run();
  }

  base::FilePath model_path = GetModelTestDataDir();

  mojo::Remote<OnDeviceModel> model;

  {
    base::RunLoop run_loop;
    ProgressObserver progress_observer(base::BindRepeating(
        [](double progress) { LOG(INFO) << "Progress: " << progress; }));

    service->LoadPlatformModel(
        base::Uuid::ParseLowercase(uuid), model.BindNewPipeAndPassReceiver(),
        progress_observer.BindRemote(),
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

  mojo::Remote<Session> session;
  model->StartSession(session.BindNewPipeAndPassReceiver());

  while (true) {
    printf("> ");
    ResponseHolder response;
    std::string input;
    std::getline(std::cin, input);
    input = base::TrimWhitespaceASCII(input, base::TRIM_ALL);
    if (request_safety.has_value() &&
        !ValidateSafetyResult(*service, *model, *request_safety, input)) {
      LOG(WARNING) << "Request safety violation detected!";
    }
    if (format_feature.has_value()) {
      input = FormatInput(*service, base::Uuid::ParseLowercase(uuid),
                          *format_feature, format_field, input);
    }
    session->Execute(MakeInput(input), response.BindRemote());
    const std::string& output = response.WaitForCompletion();
    if (response_safety.has_value() &&
        !ValidateSafetyResult(*service, *model, *response_safety, output)) {
      LOG(WARNING) << "Response safety violation detected!";
    }
    puts("");
    puts("-------------------");
  }
}
