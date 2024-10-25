// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unistd.h>

#include <iostream>
#include <optional>
#include <utility>

#include <base/check.h>
#include <base/check_op.h>
#include <base/command_line.h>
#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/functional/bind.h>
#include <base/logging.h>
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

#include "odml/mojom/big_buffer.mojom.h"
#include "odml/mojom/cros_safety.mojom.h"
#include "odml/mojom/cros_safety_service.mojom.h"

namespace {

constexpr const char kText[] = "text";
constexpr const char kImage[] = "image";
constexpr const char kCloud[] = "cloud";

constexpr base::TimeDelta kRemoteRequestTimeout = base::Seconds(3);

class FilePath;

}  // namespace

void OnCreateCloudSafetySessionComplete(
    base::RunLoop* run_loop,
    cros_safety::mojom::GetCloudSafetySessionResult result) {
  CHECK_EQ(result, cros_safety::mojom::GetCloudSafetySessionResult::kOk);
  run_loop->Quit();
}

void OnCreateOnDeviceSafetySessionComplete(
    base::RunLoop* run_loop,
    cros_safety::mojom::GetOnDeviceSafetySessionResult result) {
  CHECK_EQ(result, cros_safety::mojom::GetOnDeviceSafetySessionResult::kOk);
  run_loop->Quit();
}

void OnClassifyComplete(base::RunLoop* run_loop,
                        cros_safety::mojom::SafetyClassifierVerdict result) {
  LOG(INFO) << result;
  run_loop->Quit();
}

mojo::Remote<cros_safety::mojom::CloudSafetySession> CreateCloudSafetySession(
    mojo::Remote<cros_safety::mojom::CrosSafetyService> safety_service) {
  LOG(INFO) << "Call CreateCloudSafetySession";
  mojo::Remote<cros_safety::mojom::CloudSafetySession> cloud_safety_session;
  base::RunLoop run_loop;

  CHECK(safety_service && safety_service.is_bound() &&
        safety_service.is_connected());
  safety_service->CreateCloudSafetySession(
      cloud_safety_session.BindNewPipeAndPassReceiver(),
      base::BindOnce(&OnCreateCloudSafetySessionComplete, &run_loop));
  run_loop.Run();

  CHECK(cloud_safety_session && cloud_safety_session.is_bound() &&
        cloud_safety_session.is_connected())
      << "CreateCloudSafetySession returns ok but session isn't connected";
  return cloud_safety_session;
}

mojo::Remote<cros_safety::mojom::OnDeviceSafetySession>
CreateOnDeviceSafetySession(
    mojo::Remote<cros_safety::mojom::CrosSafetyService> safety_service) {
  LOG(INFO) << "Call CreateOnDeviceSafetySession";
  mojo::Remote<cros_safety::mojom::OnDeviceSafetySession>
      on_device_safety_session;
  base::RunLoop run_loop;

  CHECK(safety_service && safety_service.is_bound() &&
        safety_service.is_connected());
  safety_service->CreateOnDeviceSafetySession(
      on_device_safety_session.BindNewPipeAndPassReceiver(),
      base::BindOnce(&OnCreateOnDeviceSafetySessionComplete, &run_loop));
  run_loop.Run();

  CHECK(on_device_safety_session && on_device_safety_session.is_bound() &&
        on_device_safety_session.is_connected())
      << "CreateOnDeviceSafetySession returns ok but session isn't connected";
  return on_device_safety_session;
}

void FilterImageWithCloudClassifier(
    base::CommandLine* cl,
    mojo::Remote<cros_safety::mojom::CloudSafetySession> cloud_safety_session) {
  std::optional<std::string> text;
  if (cl->HasSwitch(kText)) {
    text = cl->GetSwitchValueASCII(kText);
  }

  base::FilePath image_path = cl->GetSwitchValuePath(kImage);
  CHECK(!image_path.empty() && base::PathExists(image_path));

  std::optional<std::vector<uint8_t>> image_bytes =
      base::ReadFileToBytes(image_path);
  CHECK(image_bytes.has_value() && image_bytes->size() > 0);

  LOG(INFO) << "Run cloud session ClassifyImageSafety";
  base::RunLoop run_loop;

  CHECK(cloud_safety_session && cloud_safety_session.is_bound() &&
        cloud_safety_session.is_connected());
  cloud_safety_session->ClassifyImageSafety(
      cros_safety::mojom::SafetyRuleset::kGeneric, text,
      mojo_base::mojom::BigBuffer::NewBytes(image_bytes.value()),
      base::BindOnce(&OnClassifyComplete, &run_loop));
  run_loop.Run();
}

void FilterTextWithCloudClassifier(
    base::CommandLine* cl,
    mojo::Remote<cros_safety::mojom::CloudSafetySession> cloud_safety_session) {
  std::string text = cl->GetSwitchValueASCII(kText);
  CHECK(!text.empty());

  LOG(INFO) << "Run cloud session ClassifyTextSafety";
  base::RunLoop run_loop;

  CHECK(cloud_safety_session && cloud_safety_session.is_bound() &&
        cloud_safety_session.is_connected());
  cloud_safety_session->ClassifyTextSafety(
      cros_safety::mojom::SafetyRuleset::kGeneric, text,
      base::BindOnce(&OnClassifyComplete, &run_loop));
  run_loop.Run();
}

void FilterImageWithOnDeviceClassifier(
    base::CommandLine* cl,
    mojo::Remote<cros_safety::mojom::OnDeviceSafetySession>
        on_device_safety_session) {
  LOG(FATAL) << "On-device image filtering not supported currently";
}

void FilterTextWithOnDeviceClassifier(
    base::CommandLine* cl,
    mojo::Remote<cros_safety::mojom::OnDeviceSafetySession>
        on_device_safety_session) {
  std::string text = cl->GetSwitchValueASCII(kText);
  CHECK(!text.empty());

  LOG(INFO) << "Run on-device session ClassifyTextSafety";
  base::RunLoop run_loop;

  CHECK(on_device_safety_session && on_device_safety_session.is_bound() &&
        on_device_safety_session.is_connected());
  on_device_safety_session->ClassifyTextSafety(
      cros_safety::mojom::SafetyRuleset::kGeneric, text,
      base::BindOnce(&OnClassifyComplete, &run_loop));
  run_loop.Run();
}

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
        LOG(FATAL) << "Disconnected from mojo service manager (the mojo "
                      "broker process). Error: "
                   << error << ", message: " << message
                   << ". Shutdown and wait for respawn.";
      }));

  service_manager->Request(
      /*service_name=*/chromeos::mojo_services::kCrosSafetyService,
      /*timeout=*/kRemoteRequestTimeout,
      safety_service.BindNewPipeAndPassReceiver().PassPipe());
  safety_service.set_disconnect_with_reason_handler(
      base::BindOnce([](uint32_t error, const std::string& reason) {
        LOG(FATAL) << "Safety service disconnected, error: " << error
                   << ", reason: " << reason;
      }));
  CHECK(safety_service && safety_service.is_bound() &&
        safety_service.is_connected())
      << "Cannot receive CrosSafetyService from mojo service manager";

  if (cl->HasSwitch(kCloud)) {
    mojo::Remote<cros_safety::mojom::CloudSafetySession> cloud_safety_session =
        CreateCloudSafetySession(std::move(safety_service));
    if (cl->HasSwitch(kImage)) {
      // Filter image with cloud classifier
      FilterImageWithCloudClassifier(cl, std::move(cloud_safety_session));
    } else {
      // Filter text using cloud classifier
      FilterTextWithCloudClassifier(cl, std::move(cloud_safety_session));
    }
  } else {
    mojo::Remote<cros_safety::mojom::OnDeviceSafetySession>
        on_device_safety_session =
            CreateOnDeviceSafetySession(std::move(safety_service));
    if (cl->HasSwitch(kImage)) {
      // Filter image using on-device classifier
      FilterImageWithOnDeviceClassifier(cl,
                                        std::move(on_device_safety_session));
    } else {
      // Filter text using on-device classifier
      FilterTextWithOnDeviceClassifier(cl, std::move(on_device_safety_session));
    }
  }
}
