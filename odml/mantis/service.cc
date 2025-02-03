// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/mantis/service.h"

#include <memory>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/functional/bind.h>
#include <base/functional/callback_forward.h>
#include <base/memory/raw_ref.h>
#include <base/task/sequenced_task_runner.h>
#include <base/task/task_traits.h>
#include <base/task/thread_pool.h>
#include <metrics/metrics_library.h>
#include <ml_core/dlc/dlc_client.h>

#include "odml/mantis/lib_api.h"
#include "odml/mantis/processor.h"
#include "odml/mojom/mantis_processor.mojom.h"
#include "odml/mojom/mantis_service.mojom.h"
#include "odml/utils/dlc_client_helper.h"
#include "odml/utils/odml_shim_loader.h"

namespace mantis {

namespace {

using MantisAPIGetter = const MantisAPI* (*)();

constexpr char kDlcName[] = "ml-dlc-302a455f-5453-43fb-a6a1-d856e6fe6435";
constexpr double kFinishedProgress = 1;
constexpr char kReclaimFile[] = "/proc/self/reclaim";
constexpr char kAll[] = "all";

}  // namespace

MantisService::MantisService(
    raw_ref<MetricsLibraryInterface> metrics_lib,
    raw_ref<odml::OdmlShimLoader> shim_loader,
    raw_ref<cros_safety::SafetyServiceManager> safety_service_manager)
    : metrics_lib_(metrics_lib),
      mantis_api_runner_(
          base::ThreadPool::CreateSequencedTaskRunner({base::MayBlock()})),
      shim_loader_(shim_loader),
      safety_service_manager_(safety_service_manager) {}

template <typename FuncType,
          typename CallbackType,
          typename FailureType,
          typename... Args>
bool MantisService::RetryIfShimIsNotReady(FuncType func,
                                          CallbackType& callback,
                                          FailureType failure_result,
                                          Args&... args) {
  if (shim_loader_->IsShimReady()) {
    return false;
  }

  auto split = base::SplitOnceCallback(std::move(callback));
  base::OnceClosure retry_cb =
      base::BindOnce(func, weak_ptr_factory_.GetWeakPtr(), std::move(args)...,
                     std::move(split.first));

  shim_loader_->EnsureShimReady(base::BindOnce(
      [](CallbackType callback, base::OnceClosure retry_cb,
         FailureType failure_result, bool result) {
        if (!result) {
          LOG(ERROR) << "Failed to ensure the shim is ready.";
          std::move(callback).Run(std::move(failure_result));
          return;
        }
        std::move(retry_cb).Run();
      },
      std::move(split.second), std::move(retry_cb), std::move(failure_result)));

  return true;
}

void MantisService::DeleteProcessor() {
  processor_.reset();
  if (!base::WriteFile(base::FilePath(kReclaimFile), kAll)) {
    LOG(WARNING) << "Failed to reclaim memory.";
  }
}

void MantisService::Initialize(
    mojo::PendingRemote<mojom::PlatformModelProgressObserver> progress_observer,
    mojo::PendingReceiver<mojom::MantisProcessor> processor,
    InitializeCallback callback) {
  if (RetryIfShimIsNotReady(&MantisService::Initialize, callback,
                            mojom::InitializeResult::kFailedToLoadLibrary,
                            progress_observer, processor)) {
    return;
  }

  if (processor_) {
    processor_->AddReceiver(std::move(processor));
    mojo::Remote<mojom::PlatformModelProgressObserver> remote(
        std::move(progress_observer));
    if (remote) {
      remote->Progress(kFinishedProgress);
    }
    std::move(callback).Run(mojom::InitializeResult::kSuccess);
    return;
  }

  auto remote =
      std::make_shared<mojo::Remote<mojom::PlatformModelProgressObserver>>(
          std::move(progress_observer));
  std::shared_ptr<odml::DlcClientPtr> dlc_client = odml::CreateDlcClient(
      kDlcName,
      base::BindOnce(&MantisService::OnInstallDlcComplete,
                     weak_ptr_factory_.GetWeakPtr(), std::move(processor),
                     std::move(callback)),
      base::BindRepeating(&MantisService::OnDlcProgress,
                          weak_ptr_factory_.GetWeakPtr(), remote));
  (*dlc_client)->InstallDlc();
}

void MantisService::GetMantisFeatureStatus(
    GetMantisFeatureStatusCallback callback) {
  std::move(callback).Run(
      USE_MANTIS ? mojom::MantisFeatureStatus::kAvailable
                 : mojom::MantisFeatureStatus::kDeviceNotSupported);
}

void MantisService::OnInstallDlcComplete(
    mojo::PendingReceiver<mojom::MantisProcessor> processor,
    InitializeCallback callback,
    base::expected<base::FilePath, std::string> result) {
  if (!result.has_value()) {
    LOG(ERROR) << "Failed to install ML DLC: " << result.error();
    std::move(callback).Run(mojom::InitializeResult::kFailedToLoadLibrary);
    return;
  }

  if (processor_) {
    processor_->AddReceiver(std::move(processor));
    std::move(callback).Run(mojom::InitializeResult::kSuccess);
    return;
  }
  if (is_initializing_processor_) {
    pending_processors_.push_back(PendingProcessor{
        .processor = std::move(processor),
        .callback = std::move(callback),
    });
    return;
  }

  auto get_api = shim_loader_->Get<MantisAPIGetter>("GetMantisAPI");
  if (!get_api) {
    LOG(ERROR) << "Unable to resolve GetMantisAPI() symbol.";
    std::move(callback).Run(mojom::InitializeResult::kFailedToLoadLibrary);
    return;
  }

  const MantisAPI* api = get_api();
  if (!api) {
    LOG(ERROR) << "Unable to get MantisAPI.";
    std::move(callback).Run(mojom::InitializeResult::kFailedToLoadLibrary);
    return;
  }

  // Run in Mantis API runner, and the client should wait for the `callback` to
  // run.
  is_initializing_processor_ = true;
  mantis_api_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](const MantisAPI* api, base::FilePath assets_file_dir) {
            return api->Initialize(assets_file_dir.value());
          },
          api, *std::move(result)),
      base::BindOnce(&MantisService::CreateMantisProcessor,
                     weak_ptr_factory_.GetWeakPtr(), metrics_lib_,
                     mantis_api_runner_, api, std::move(processor),
                     safety_service_manager_,
                     base::BindOnce(&MantisService::DeleteProcessor,
                                    base::Unretained(this)),
                     std::move(callback))
          .Then(base::BindOnce(&MantisService::NotifyPendingProcessors,
                               weak_ptr_factory_.GetWeakPtr())));
}

void MantisService::CreateMantisProcessor(
    raw_ref<MetricsLibraryInterface> metrics_lib,
    scoped_refptr<base::SequencedTaskRunner> mantis_api_runner,
    const MantisAPI* api,
    mojo::PendingReceiver<mojom::MantisProcessor> processor,
    raw_ref<cros_safety::SafetyServiceManager> safety_service_manager,
    base::OnceCallback<void()> on_disconnected,
    base::OnceCallback<void(mantis::mojom::InitializeResult)> callback,
    MantisComponent component) {
  processor_ = std::make_unique<MantisProcessor>(
      metrics_lib, std::move(mantis_api_runner), component, api,
      std::move(processor), safety_service_manager, std::move(on_disconnected),
      std::move(callback));
}

void MantisService::NotifyPendingProcessors() {
  is_initializing_processor_ = false;
  for (PendingProcessor& pending : pending_processors_) {
    processor_->AddReceiver(std::move(pending.processor));
    std::move(pending.callback).Run(mojom::InitializeResult::kSuccess);
  }
  pending_processors_.clear();
}

void MantisService::OnDlcProgress(
    std::shared_ptr<mojo::Remote<mojom::PlatformModelProgressObserver>>
        progress_observer,
    double progress) {
  if (progress_observer && *progress_observer) {
    (*progress_observer)->Progress(progress);
  }
}

}  // namespace mantis
