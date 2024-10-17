// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file..

#include <unistd.h>

#include <fstream>
#include <iostream>
#include <optional>
#include <utility>

#include <base/check.h>
#include <base/command_line.h>
#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/functional/bind.h>
#include <base/run_loop.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/task/single_thread_task_executor.h>
#include <base/task/thread_pool/thread_pool_instance.h>
#include <base/uuid.h>
#include <brillo/syslog_logging.h>
#include <chromeos/mojo/service_constants.h>
#include <dlcservice/proto_bindings/dlcservice.pb.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/core/embedder/scoped_ipc_support.h>
#include <mojo/public/cpp/bindings/pending_receiver.h>
#include <mojo/public/cpp/bindings/pending_remote.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <mojo/public/cpp/bindings/remote.h>
#include <mojo_service_manager/lib/connect.h>
#include <mojo_service_manager/lib/mojom/service_manager.mojom.h>

#include "base/files/file_util.h"
#include "base/logging.h"
#include "odml/mojom/big_buffer.mojom.h"
#include "odml/mojom/cros_safety.mojom.h"
#include "odml/mojom/cros_safety_service.mojom.h"

namespace {

constexpr const char kText[] = "text";
constexpr const char kImage[] = "image";

class FilePath;

}  // namespace

int main(int argc, char** argv) {
  base::CommandLine::Init(argc, argv);
  base::CommandLine* cl = base::CommandLine::ForCurrentProcess();

  base::ThreadPoolInstance::CreateAndStartWithDefaultParams("thread_pool");

  base::SingleThreadTaskExecutor io_task_executor(base::MessagePumpType::IO);
  mojo::core::Init();

  mojo::core::ScopedIPCSupport ipc_support(
      base::SingleThreadTaskRunner::GetCurrentDefault(),
      mojo::core::ScopedIPCSupport::ShutdownPolicy::CLEAN);

  mojo::Remote<cros_safety::mojom::CrosSafetyService> safety_service;

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
      /*service_name=*/chromeos::mojo_services::kCrosSafetyService,
      /*timeout=*/std::nullopt,
      safety_service.BindNewPipeAndPassReceiver().PassPipe());

  mojo::Remote<cros_safety::mojom::CloudSafetySession> session;
  LOG(INFO) << "call CreateCloudSafetySession";
  {
    base::RunLoop run_loop;
    safety_service->CreateCloudSafetySession(
        session.BindNewPipeAndPassReceiver(),
        base::BindOnce(
            [](base::RunLoop* run_loop,
               cros_safety::mojom::GetCloudSafetySessionResult result) {
              LOG(INFO) << result;
              run_loop->Quit();
            },
            &run_loop));
    run_loop.Run();
  }

  if (!cl->HasSwitch(kImage)) {
    std::string text = cl->GetSwitchValueASCII(kText);
    CHECK(!text.empty());

    LOG(INFO) << "run ClassifyTextSafety";
    {
      base::RunLoop run_loop;
      session->ClassifyTextSafety(
          cros_safety::mojom::SafetyRuleset::kGeneric, text,
          base::BindOnce(
              [](base::RunLoop* run_loop,
                 cros_safety::mojom::SafetyClassifierVerdict verdict) {
                LOG(INFO) << verdict;
                run_loop->Quit();
              },
              &run_loop));
      run_loop.Run();
    }
  } else {
    std::optional<std::string> text;
    if (cl->HasSwitch(kText)) {
      text = cl->GetSwitchValueASCII(kText);
    }

    base::FilePath image_path = cl->GetSwitchValuePath(kImage);
    CHECK(!image_path.empty() && base::PathExists(image_path));

    std::optional<std::vector<uint8_t>> image_bytes =
        base::ReadFileToBytes(image_path);
    CHECK(image_bytes.has_value() && image_bytes->size() > 0);

    LOG(INFO) << "run ClassifyImageSafety";
    {
      base::RunLoop run_loop;
      session->ClassifyImageSafety(
          cros_safety::mojom::SafetyRuleset::kGeneric, text,
          mojo_base::mojom::BigBuffer::NewBytes(image_bytes.value()),
          base::BindOnce(
              [](base::RunLoop* run_loop,
                 cros_safety::mojom::SafetyClassifierVerdict verdict) {
                LOG(INFO) << verdict;
                run_loop->Quit();
              },
              &run_loop));
      run_loop.Run();
    }
  }
}
