// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*
sample usage:
mantis_console --image=/usr/local/tmp/image.jpg \
      --mask=/usr/local/tmp/mask.jpg \
      --prompt="a red building" \
      --image_output_path=/usr/local/tmp/output.jpg \
      --generated_region_output_path=/usr/local/tmp/generated_region.jpg \
      --genfill --seed 123
*/

#include <sysexits.h>

#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <base/check.h>
#include <base/command_line.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/functional/bind.h>
#include <base/memory/raw_ref.h>
#include <base/run_loop.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/task/single_thread_task_executor.h>
#include <base/task/thread_pool/thread_pool_instance.h>
#include <base/uuid.h>
#include <brillo/daemons/dbus_daemon.h>
#include <chromeos/mojo/service_constants.h>
#include <metrics/metrics_library.h>
#include <ml_core/dlc/dlc_client.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/core/embedder/scoped_ipc_support.h>
#include <mojo/public/cpp/bindings/pending_receiver.h>
#include <mojo/public/cpp/bindings/pending_remote.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <mojo/public/cpp/bindings/remote.h>
#include <mojo_service_manager/lib/connect.h>
#include <mojo_service_manager/lib/mojom/service_manager.mojom.h>

#include "odml/cros_safety/safety_service_manager.h"
#include "odml/cros_safety/safety_service_manager_bypass.h"
#include "odml/cros_safety/safety_service_manager_impl.h"
#include "odml/i18n/translator.h"
#include "odml/i18n/translator_impl.h"
#include "odml/mantis/service.h"
#include "odml/mojom/mantis_processor.mojom.h"
#include "odml/mojom/mantis_service.mojom-shared.h"
#include "odml/mojom/mantis_service.mojom.h"
#include "odml/utils/odml_shim_loader_impl.h"
#include "odml/utils/performance_timer.h"

namespace {

constexpr const char kPrompt[] = "prompt";
constexpr const char kImage[] = "image";
constexpr const char kMask[] = "mask";
constexpr const char kSeed[] = "seed";
constexpr const char kEnableSafety[] = "enable_safety";
constexpr const char kInapinting[] = "inpainting";
constexpr const char kGenfill[] = "genfill";
constexpr const char kOutpainting[] = "outpainting";
constexpr const char kImageOutputPath[] = "image_output_path";
constexpr const char kGeneratedRegionOutputPath[] =
    "generated_region_output_path";
constexpr const char kDlcUUID[] = "dlc_uuid";

constexpr const int kDefaultSeed = 0;
constexpr const char kDefaultImageOutputPath[] = "/usr/local/tmp/output.jpg";
constexpr const char kDefaultGeneratedRegionOutputPath[] =
    "/usr/local/tmp/generated_region.jpg";

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

std::string GetOutputImagePath(base::CommandLine* cl) {
  if (cl && cl->HasSwitch(kImageOutputPath)) {
    return cl->GetSwitchValueASCII(kImageOutputPath);
  }
  return kDefaultImageOutputPath;
}

std::string GetGeneratedRegionOutputPath(base::CommandLine* cl) {
  if (cl && cl->HasSwitch(kGeneratedRegionOutputPath)) {
    return cl->GetSwitchValueASCII(kGeneratedRegionOutputPath);
  }
  return kDefaultGeneratedRegionOutputPath;
}

std::optional<std::string> GetDlcUUID(base::CommandLine* cl) {
  if (cl && cl->HasSwitch(kDlcUUID)) {
    return cl->GetSwitchValueASCII(kDlcUUID);
  }
  return std::nullopt;
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

class MantisProcessorForInterception : public mantis::MantisProcessor {
 public:
  // Use the same constructor as the base class
  using MantisProcessor::MantisProcessor;

  base::CommandLine* cl_;

 private:
  void OnClassifyImageOutputDone(
      std::unique_ptr<mantis::MantisProcess> process,
      std::vector<mantis::mojom::SafetyClassifierVerdict> results) override {
    base::FilePath output_image_path = base::FilePath(GetOutputImagePath(cl_));
    base::WriteFile(output_image_path, process->image_result);
    LOG(INFO) << "Generated image: " << output_image_path;

    base::FilePath generated_region_path =
        base::FilePath(GetGeneratedRegionOutputPath(cl_));
    base::WriteFile(generated_region_path, process->generated_region);
    LOG(INFO) << "Generated region: " << generated_region_path;

    MantisProcessor::MantisProcessor::OnClassifyImageOutputDone(
        std::move(process), std::move(results));
  }
};

class MantisServiceForInterception : public mantis::MantisService {
 public:
  // Use the same constructor as the base class
  using MantisService::MantisService;
  std::unique_ptr<MantisProcessorForInterception> mantis_processor;

 private:
  void CreateMantisProcessor(
      raw_ref<MetricsLibraryInterface> metrics_lib,
      scoped_refptr<base::SequencedTaskRunner> mantis_api_runner,
      const mantis::MantisAPI* api,
      mojo::PendingReceiver<mantis::mojom::MantisProcessor> receiver,
      raw_ref<cros_safety::SafetyServiceManager> safety_service_manager,
      raw_ref<i18n::Translator> translator,
      base::OnceCallback<void()> on_disconnected,
      base::OnceCallback<void(mantis::mojom::InitializeResult)> callback,
      odml::PerformanceTimer::Ptr timer,
      mantis::MantisComponent component) override {
    LOG(INFO) << "MantisServiceForInterception::CreateMantisProcessor called";
    mantis_processor = std::make_unique<MantisProcessorForInterception>(
        metrics_lib, mantis_api_runner, component, api, std::move(receiver),
        safety_service_manager, translator, std::move(on_disconnected),
        std::move(callback));
  }
};

class MantisServiceProviderImpl {
 public:
  MantisServiceProviderImpl(
      raw_ref<MetricsLibrary> metrics,
      raw_ref<odml::OdmlShimLoaderImpl> shim_loader,
      mojo::Remote<chromeos::mojo_service_manager::mojom::ServiceManager>&
          service_manager,
      raw_ref<cros_safety::SafetyServiceManager> safety_service_manager,
      raw_ref<i18n::TranslatorImpl> translator)
      : service_impl_(
            metrics, shim_loader, safety_service_manager, translator) {}
  raw_ref<MantisServiceForInterception> service() {
    return raw_ref(service_impl_);
  }

 private:
  MantisServiceForInterception service_impl_;
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

    exit_code = CreateMantisServiceProvider(ShouldEnableSafety(cl_));
    if (exit_code != EX_OK) {
      LOG(ERROR) << "CreateMantisServiceProvider() failed";
      return exit_code;
    }
    exit_code = CreateMantisService();
    if (exit_code != EX_OK) {
      LOG(ERROR) << "CreateMantisService() failed";
      return exit_code;
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
  int CreateMantisServiceProvider(bool enable_safety) {
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
    if (enable_safety) {
      safety_service_manager_ =
          std::make_unique<cros_safety::SafetyServiceManagerImpl>(
              service_manager_, raw_ref(metrics_));
    } else {
      safety_service_manager_ =
          std::make_unique<cros_safety::SafetyServiceManagerBypass>();
    }
    mantis_service_provider_impl_ = std::make_unique<MantisServiceProviderImpl>(
        raw_ref(metrics_), raw_ref(shim_loader_), service_manager_,
        raw_ref(*safety_service_manager_.get()), raw_ref(translator_));
    return EX_OK;
  }

  int CreateMantisService() {
    auto service = mantis_service_provider_impl_->service();
    mojo::Remote<mantis::mojom::MantisProcessor> processor_remote;
    {
      base::RunLoop run_loop;
      std::optional<std::string> dlc_uuid_string = GetDlcUUID(cl_);
      std::optional<base::Uuid> dlc_uuid = std::nullopt;
      if (dlc_uuid_string.has_value()) {
        dlc_uuid = base::Uuid::ParseLowercase(dlc_uuid_string.value());
      }

      service->Initialize(
          mojo::NullRemote(), processor_remote.BindNewPipeAndPassReceiver(),
          dlc_uuid,
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
    service->mantis_processor->cl_ = cl_;
    return EX_OK;
  }

  void OnOperationFinish(base::RunLoop* run_loop,
                         mantis::mojom::MantisResultPtr result) {
    LOG(INFO) << "Mantis operation callback";
    LOG(INFO) << "IsError: " << result->is_error();
    if (result->is_error()) {
      LOG(INFO) << "Mantis error " << result->get_error();
    } else {
      LOG(INFO) << "Mantis process finished successfully.";
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
    service->mantis_processor->Outpainting(
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
  // The metrics lib. Should be destructed after service providers.
  MetricsLibrary metrics_;
  std::unique_ptr<MantisServiceProviderImpl> mantis_service_provider_impl_;
  mojo::Remote<chromeos::mojo_service_manager::mojom::ServiceManager>
      service_manager_;
  std::unique_ptr<cros_safety::SafetyServiceManager> safety_service_manager_;
  i18n::TranslatorImpl translator_{raw_ref(shim_loader_)};
  base::CommandLine* cl_;
};

int main(int argc, char** argv) {
  base::CommandLine::Init(argc, argv);

  base::ThreadPoolInstance::CreateAndStartWithDefaultParams("thread_pool");

  MantisConsole mantis_console;
  mantis_console.Run();
  return 0;
}
