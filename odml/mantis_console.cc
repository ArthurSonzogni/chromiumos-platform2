// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*
sample usage:
mantis_console --image=/usr/local/tmp/image.jpg \
      --mask=/usr/local/tmp/mask.jpg \
      --prompt="a red building" \
      --output=/usr/local/tmp/output.jpg \
      --genfill --seed 123
*/

#include <sysexits.h>

#include <cstdint>
#include <iostream>
#include <string>
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
#include <brillo/daemons/dbus_daemon.h>
#include <chromeos/mojo/service_constants.h>
#include <ml_core/dlc/dlc_client.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/core/embedder/scoped_ipc_support.h>
#include <mojo/public/cpp/bindings/pending_receiver.h>
#include <mojo/public/cpp/bindings/pending_remote.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <mojo/public/cpp/bindings/remote.h>
#include <mojo_service_manager/lib/connect.h>
#include <mojo_service_manager/lib/mojom/service_manager.mojom.h>

#include "base/files/file_util.h"
#include "odml/mantis/service.h"
#include "odml/mojom/mantis_processor.mojom.h"
#include "odml/mojom/mantis_service.mojom-shared.h"
#include "odml/mojom/mantis_service.mojom.h"
#include "odml/utils/odml_shim_loader_impl.h"

namespace {

constexpr const char kPrompt[] = "prompt";
constexpr const char kImage[] = "image";
constexpr const char kMask[] = "mask";
constexpr const char kSeed[] = "seed";
constexpr const char kEnableSafety[] = "enable_safety";
constexpr const char kInapinting[] = "inpainting";
constexpr const char kGenfill[] = "genfill";
constexpr const char kOutpainting[] = "outpainting";
constexpr const char kOutputPath[] = "output";

constexpr const int kDefaultSeed = 0;
constexpr const char kDefaultOutputPath[] = "/usr/local/tmp/output.jpg";

}  // namespace

// Command line parsing
std::vector<uint8_t> ParseImage(base::CommandLine* cl, std::string arg_name) {
  CHECK(cl->HasSwitch(arg_name));
  base::FilePath image_path = cl->GetSwitchValuePath(arg_name);
  CHECK(!image_path.empty() && base::PathExists(image_path));
  std::optional<std::vector<uint8_t>> image_bytes =
      base::ReadFileToBytes(image_path);
  CHECK(image_bytes.has_value() && image_bytes->size() > 0);
  return image_bytes.value();
}

std::vector<uint8_t> GetImage(base::CommandLine* cl) {
  return ParseImage(cl, kImage);
}

std::vector<uint8_t> GetMask(base::CommandLine* cl) {
  return ParseImage(cl, kMask);
}

uint32_t GetSeed(base::CommandLine* cl) {
  if (cl && cl->HasSwitch(kSeed)) {
    std::string seed_str = cl->GetSwitchValueASCII(kSeed);
    uint32_t seed_value;
    if (base::StringToUint(seed_str, &seed_value)) {
      return seed_value;
    }
  }
  return kDefaultSeed;
}

std::string GetPrompt(base::CommandLine* cl) {
  CHECK(cl->HasSwitch(kPrompt));
  return cl->GetSwitchValueASCII(kPrompt);
}

std::string GetOutputPath(base::CommandLine* cl) {
  if (cl && cl->HasSwitch(kOutputPath)) {
    return cl->GetSwitchValueASCII(kOutputPath);
  }
  return kDefaultOutputPath;
}

bool ShouldEnableSafety(base::CommandLine* cl) {
  return cl ? cl->HasSwitch(kEnableSafety) : false;
}

bool DoInpainting(base::CommandLine* cl) {
  return cl ? cl->HasSwitch(kInapinting) : false;
}

bool DoGenfill(base::CommandLine* cl) {
  return cl ? cl->HasSwitch(kGenfill) : false;
}

bool DoOutpainting(base::CommandLine* cl) {
  return cl ? cl->HasSwitch(kOutpainting) : false;
}

class MantisProcessorWithoutSafetyCheck : public mantis::MantisProcessor {
 public:
  // Use the same constructor as the base class
  using MantisProcessor::MantisProcessor;

  void ClassifyImageSafetyInternal(
      const std::vector<uint8_t>& image,
      const std::string& text,
      base::OnceCallback<void(mantis::mojom::SafetyClassifierVerdict)> callback)
      override {
    if (enable_safety_) {
      MantisProcessor::ClassifyImageSafetyInternal(image, text,
                                                   std::move(callback));
      return;
    }
    LOG(INFO) << "Fake ClassifyImageSafetyInternal was called";
    std::move(callback).Run(mantis::mojom::SafetyClassifierVerdict::kPass);
  }

  void EnableSafety() { enable_safety_ = true; }

  void DisableSafety() { enable_safety_ = false; }

 private:
  bool enable_safety_ = false;
};

class MantisServiceWithoutSafetyCheck : public mantis::MantisService {
 public:
  // Use the same constructor as the base class
  using MantisService::MantisService;
  std::unique_ptr<MantisProcessorWithoutSafetyCheck> mantis_processor;

 private:
  void CreateMantisProcessor(
      mantis::MantisComponent component,
      const mantis::MantisAPI* api,
      mojo::PendingReceiver<mantis::mojom::MantisProcessor> receiver,
      raw_ref<
          mojo::Remote<chromeos::mojo_service_manager::mojom::ServiceManager>>
          service_manager,
      base::OnceCallback<void()> on_disconnected,
      base::OnceCallback<void(mantis::mojom::InitializeResult)> callback)
      override {
    LOG(INFO)
        << "MantisServiceWithoutSafetyCheck::CreateMantisProcessor called";
    mantis_processor = std::make_unique<MantisProcessorWithoutSafetyCheck>(
        component, api, std::move(receiver), service_manager,
        std::move(on_disconnected), std::move(callback));
    // Disable safety by default
    mantis_processor->DisableSafety();
  }
};

class MantisServiceProviderImpl {
 public:
  MantisServiceProviderImpl(
      raw_ref<odml::OdmlShimLoaderImpl> shim_loader,
      mojo::Remote<chromeos::mojo_service_manager::mojom::ServiceManager>&
          service_manager)
      : service_impl_(shim_loader, raw_ref(service_manager)) {}
  raw_ref<MantisServiceWithoutSafetyCheck> service() {
    return raw_ref(service_impl_);
  }

 private:
  MantisServiceWithoutSafetyCheck service_impl_;
};

class MantisConsole : public brillo::DBusDaemon {
 protected:
  int OnInit() override {
    cl_ = base::CommandLine::ForCurrentProcess();
    int exit_code = brillo::DBusDaemon::OnInit();
    if (exit_code != EX_OK) {
      LOG(ERROR) << "DBusDaemon::OnInit() failed";
      return exit_code;
    }

    exit_code = CreateMantisServiceProvider();
    if (exit_code != EX_OK) {
      LOG(ERROR) << "CreateMantisServiceProvider() failed";
      return exit_code;
    }
    exit_code = CreateMantisService();
    if (exit_code != EX_OK) {
      LOG(ERROR) << "CreateMantisService() failed";
      return exit_code;
    }
    if (ShouldEnableSafety(cl_)) {
      mantis_service_provider_impl_->service()
          ->mantis_processor->EnableSafety();
    }

    if (DoInpainting(cl_)) {
      Inpainting();
    }
    if (DoGenfill(cl_)) {
      Genfill();
    }
    if (DoOutpainting(cl_)) {
      Outpainting();
    }

    // Exit daemon,
    // https://crsrc.org/o/src/platform2/libbrillo/brillo/daemons/daemon.h;l=69
    return -1;
  }

 private:
  int CreateMantisServiceProvider() {
    mojo::core::Init();
    mojo::core::ScopedIPCSupport ipc_support(
        base::SingleThreadTaskRunner::GetCurrentDefault(),
        mojo::core::ScopedIPCSupport::ShutdownPolicy::CLEAN);
    auto service_manager_remote =
        chromeos::mojo_service_manager::ConnectToMojoServiceManager();
    if (!service_manager_remote) {
      LOG(ERROR) << "Failed to connect to Mojo Service Manager";
      return -1;
    }
    service_manager_.Bind(std::move(service_manager_remote));
    service_manager_.set_disconnect_with_reason_handler(
        base::BindOnce([](uint32_t error, const std::string& message) {
          LOG(INFO) << "Disconnected from mojo service manager (the mojo "
                       "broker process). Error: "
                    << error << ", message: " << message
                    << ". Shutdown and wait for respawn.";
        }));
    mantis_service_provider_impl_ = std::make_unique<MantisServiceProviderImpl>(
        raw_ref(shim_loader_), service_manager_);
    return EX_OK;
  }

  int CreateMantisService() {
    auto service = mantis_service_provider_impl_->service();
    mojo::Remote<mantis::mojom::MantisProcessor> processor_remote;
    {
      base::RunLoop run_loop;
      service->Initialize(
          mojo::NullRemote(), processor_remote.BindNewPipeAndPassReceiver(),
          base::BindOnce(
              [](base::RunLoop* run_loop,
                 mantis::mojom::InitializeResult result) {
                if (result == mantis::mojom::InitializeResult ::kSuccess) {
                  LOG(INFO) << "Mantis Service initialized";
                } else {
                  LOG(ERROR) << "Mantis service initialization failed";
                  exit(0);
                }
                run_loop->Quit();
              },
              &run_loop));
      run_loop.Run();
    }
    return EX_OK;
  }

  void OnOperationFinish(base::RunLoop* run_loop,
                         mantis::mojom::MantisResultPtr result) {
    LOG(INFO) << "Mantis operation callback";
    LOG(INFO) << "IsError: " << result->is_error();
    if (result->is_error()) {
      LOG(INFO) << "Mantis error " << result->get_error();
    } else {
      auto path = base::FilePath(GetOutputPath(cl_));
      base::WriteFile(path, result->get_result_image());
      LOG(INFO) << "Generated image: " << path;
    }
    run_loop->Quit();
  }

  void Inpainting() {
    auto service = mantis_service_provider_impl_->service();
    LOG(INFO) << "Mantis inpainting call";
    base::RunLoop run_loop;
    service->mantis_processor->Inpainting(
        GetImage(cl_), GetMask(cl_), GetSeed(cl_),
        base::BindOnce(&MantisConsole::OnOperationFinish,
                       base::Unretained(this), &run_loop));
    run_loop.Run();
  }

  void Outpainting() {
    auto service = mantis_service_provider_impl_->service();
    LOG(INFO) << "Mantis outpainting call";
    base::RunLoop run_loop;
    service->mantis_processor->Inpainting(
        GetImage(cl_), GetMask(cl_), GetSeed(cl_),
        base::BindOnce(&MantisConsole::OnOperationFinish,
                       base::Unretained(this), &run_loop));
    run_loop.Run();
  }

  void Genfill() {
    auto service = mantis_service_provider_impl_->service();
    LOG(INFO) << "Mantis genfill call";
    base::RunLoop run_loop;
    service->mantis_processor->GenerativeFill(
        GetImage(cl_), GetMask(cl_), GetSeed(cl_), GetPrompt(cl_),
        base::BindOnce(&MantisConsole::OnOperationFinish,
                       base::Unretained(this), &run_loop));
    run_loop.Run();
  }

  odml::OdmlShimLoaderImpl shim_loader_;
  std::unique_ptr<MantisServiceProviderImpl> mantis_service_provider_impl_;
  mojo::Remote<chromeos::mojo_service_manager::mojom::ServiceManager>
      service_manager_;
  base::CommandLine* cl_;
};

int main(int argc, char** argv) {
  base::CommandLine::Init(argc, argv);

  base::ThreadPoolInstance::CreateAndStartWithDefaultParams("thread_pool");

  MantisConsole mantis_console;
  mantis_console.Run();
  return 0;
}
