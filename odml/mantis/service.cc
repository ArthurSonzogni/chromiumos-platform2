// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/mantis/service.h"

#include <cmath>
#include <memory>
#include <optional>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/functional/bind.h>
#include <base/functional/callback_forward.h>
#include <base/functional/callback_helpers.h>
#include <base/memory/raw_ref.h>
#include <base/task/sequenced_task_runner.h>
#include <base/task/task_traits.h>
#include <base/task/thread_pool.h>
#include <base/uuid.h>
#include <metrics/metrics_library.h>
#include <ml_core/dlc/dlc_client.h>

#include "odml/i18n/translator.h"
#include "odml/mantis/lib_api.h"
#include "odml/mantis/metrics.h"
#include "odml/mantis/processor.h"
#include "odml/mojom/mantis_processor.mojom.h"
#include "odml/mojom/mantis_service.mojom.h"
#include "odml/utils/dlc_client_helper.h"
#include "odml/utils/odml_shim_loader.h"
#include "odml/utils/performance_timer.h"

namespace mantis {

namespace {

using MantisAPIGetter = const MantisAPI* (*)();

constexpr char kDlcPrefix[] = "ml-dlc-";
constexpr char kDefaultDlcUUID[] = "9807ba80-5bee-4b94-a901-e6972d136051";
constexpr double kFinishedProgress = 1;
constexpr std::array<const char*, 3> kI18nLanguage = {"fr", "de", "ja"};
// We split the overall progress into 1 Mantis model DLC and n i18n language
// translate models. For example, the Mantis model DLC can take 92.5% of the
// progress, continued with 2.5% each for 3 translation models.
constexpr double kI18nDlcProgressAllocation = 0.025;
constexpr double kMantisDlcProgressAllocation =
    kFinishedProgress - kI18nLanguage.size() * kI18nDlcProgressAllocation;
constexpr char kReclaimFile[] = "/proc/self/reclaim";
constexpr char kAll[] = "all";

// Rounds `value` to ensure we don't have precision problem. This is to prevent
// bugs in the UI side.
double RoundToNearest0_0001(double value) {
  return std::round(10000.0 * value) / 10000.0;
}

// Reports overall progress based on the start, allocation, and individual
// progress of each part.
void OnDlcProgress(
    std::shared_ptr<mojo::Remote<mojom::PlatformModelProgressObserver>>
        progress_observer,
    double start,
    double allocation,
    double progress) {
  if (progress_observer && *progress_observer) {
    (*progress_observer)
        ->Progress(RoundToNearest0_0001(start + allocation * progress));
  }
}

// A helper function to call `Translator::DownloadDlc` sequentially for each
// items in `kI18nLanguage`. This is achieved by providing the index for the
// recursive function to allow it being passed as a callback for `DownloadDlc`.
// In the end, it will call `callback` with `true` if successful, or `false`
// otherwise.
void InstallI18nDlcForIndex(
    raw_ref<i18n::Translator> translator,
    std::shared_ptr<mojo::Remote<mojom::PlatformModelProgressObserver>>
        progress_observer,
    int index,
    base::OnceCallback<void(bool)> callback) {
  if (index == kI18nLanguage.size()) {
    std::move(callback).Run(true);
    return;
  }
  translator->DownloadDlc(
      {.source = kI18nLanguage[index], .target = "en"},
      base::BindOnce(
          [](raw_ref<i18n::Translator> translator,
             std::shared_ptr<mojo::Remote<mojom::PlatformModelProgressObserver>>
                 progress_observer,
             int index, base::OnceCallback<void(bool)> callback, bool success) {
            LOG_IF(WARNING, !success)
                << "Failed to install translate for language "
                << kI18nLanguage[index];
            if (success) {
              InstallI18nDlcForIndex(translator, progress_observer, index + 1,
                                     std::move(callback));
            } else {
              std::move(callback).Run(false);
            }
          },
          translator, progress_observer, index, std::move(callback)),
      /*progress=*/
      base::BindRepeating(&OnDlcProgress, progress_observer,
                          /*start=*/kMantisDlcProgressAllocation +
                              index * kI18nDlcProgressAllocation,
                          kI18nDlcProgressAllocation));
}

}  // namespace

MantisService::MantisService(
    raw_ref<MetricsLibraryInterface> metrics_lib,
    raw_ref<odml::OdmlShimLoader> shim_loader,
    raw_ref<cros_safety::SafetyServiceManager> safety_service_manager,
    raw_ref<i18n::Translator> translator)
    : metrics_lib_(metrics_lib),
      mantis_api_runner_(
          base::ThreadPool::CreateSequencedTaskRunner({base::MayBlock()})),
      shim_loader_(shim_loader),
      safety_service_manager_(safety_service_manager),
      translator_(translator) {}

void MantisService::DeleteProcessor() {
  processor_.reset();
  if (!base::WriteFile(base::FilePath(kReclaimFile), kAll)) {
    LOG(WARNING) << "Failed to reclaim memory.";
  }
}

void MantisService::Initialize(
    mojo::PendingRemote<mojom::PlatformModelProgressObserver> progress_observer,
    mojo::PendingReceiver<mojom::MantisProcessor> processor,
    const std::optional<base::Uuid>& dlc_uuid,
    InitializeCallback callback) {
  std::shared_ptr<mojo::Remote<mojom::PlatformModelProgressObserver>> remote =
      std::make_shared<mojo::Remote<mojom::PlatformModelProgressObserver>>(
          std::move(progress_observer));

  if (shim_loader_->IsShimReady()) {
    InitializeInternal(remote, std::move(processor), dlc_uuid,
                       std::move(callback));
    return;
  }

  shim_loader_->InstallVerifiedShim(
      base::BindOnce(&MantisService::OnInstallVerifiedShimComplete,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback),
                     remote, std::move(processor), dlc_uuid));
}

void MantisService::GetMantisFeatureStatus(
    GetMantisFeatureStatusCallback callback) {
  std::move(callback).Run(
      USE_MANTIS ? mojom::MantisFeatureStatus::kAvailable
                 : mojom::MantisFeatureStatus::kDeviceNotSupported);
}

void MantisService::OnInstallVerifiedShimComplete(
    InitializeCallback callback,
    std::shared_ptr<mojo::Remote<mojom::PlatformModelProgressObserver>>
        progress_observer,
    mojo::PendingReceiver<mojom::MantisProcessor> processor,
    const std::optional<base::Uuid>& dlc_uuid,
    bool result) {
  if (!result) {
    // Because the shim has not been downloaded, a 0% progress update will be
    // sent to signal the UI to display a download message.
    if (progress_observer && *progress_observer) {
      (*progress_observer)->Progress(0);
    }
    shim_loader_->EnsureShimReady(
        base::BindOnce(&MantisService::OnInstallShimComplete,
                       weak_ptr_factory_.GetWeakPtr(), std::move(callback),
                       progress_observer, std::move(processor), dlc_uuid));
    return;
  }

  InitializeInternal(progress_observer, std::move(processor), dlc_uuid,
                     std::move(callback));
}

void MantisService::OnInstallShimComplete(
    InitializeCallback callback,
    std::shared_ptr<mojo::Remote<mojom::PlatformModelProgressObserver>>
        progress_observer,
    mojo::PendingReceiver<mojom::MantisProcessor> processor,
    const std::optional<base::Uuid>& dlc_uuid,
    bool result) {
  if (!result) {
    LOG(ERROR) << "Failed to ensure the shim is ready.";
    std::move(callback).Run(mojom::InitializeResult::kFailedToLoadLibrary);
    return;
  }

  InitializeInternal(progress_observer, std::move(processor), dlc_uuid,
                     std::move(callback));
}

void MantisService::OnInstallMantisDlcComplete(
    mojo::PendingReceiver<mojom::MantisProcessor> processor,
    InitializeCallback callback,
    odml::PerformanceTimer::Ptr timer,
    std::shared_ptr<mojo::Remote<mojom::PlatformModelProgressObserver>>
        progress_observer,
    base::expected<base::FilePath, std::string> result) {
  if (!result.has_value()) {
    LOG(ERROR) << "Failed to install ML DLC: " << result.error();
    std::move(callback).Run(mojom::InitializeResult::kFailedToLoadLibrary);
    return;
  }

  InstallI18nDlcForIndex(
      translator_, progress_observer, 0,
      base::BindOnce(&MantisService::PrepareMantisProcessor,
                     weak_ptr_factory_.GetWeakPtr(), std::move(processor),
                     std::move(callback), std::move(timer),
                     *std ::move(result)));
}

void MantisService::PrepareMantisProcessor(
    mojo::PendingReceiver<mojom::MantisProcessor> processor,
    InitializeCallback callback,
    odml::PerformanceTimer::Ptr timer,
    base::FilePath assets_file_dir,
    bool i18n_success) {
  if (!i18n_success) {
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
          api, assets_file_dir),
      base::BindOnce(&MantisService::CreateMantisProcessor,
                     weak_ptr_factory_.GetWeakPtr(), metrics_lib_,
                     mantis_api_runner_, api, std::move(processor),
                     safety_service_manager_, translator_,
                     base::BindOnce(&MantisService::DeleteProcessor,
                                    base::Unretained(this)),
                     std::move(callback), std::move(timer))
          .Then(base::BindOnce(&MantisService::NotifyPendingProcessors,
                               weak_ptr_factory_.GetWeakPtr())));
}

// TODO(crbug.com/396779215): Send notification to the UI.
void MantisService::OnInstallVerifiedMantisDlcComplete(
    mojo::PendingReceiver<mojom::MantisProcessor> processor,
    InitializeCallback callback,
    odml::PerformanceTimer::Ptr timer,
    const std::string& target_dlc_uuid,
    std::shared_ptr<mojo::Remote<mojom::PlatformModelProgressObserver>>
        progress_observer,
    base::expected<base::FilePath, std::string> result) {
  if (result.has_value()) {
    OnInstallMantisDlcComplete(std::move(processor), std::move(callback),
                               std::move(timer), progress_observer, result);
    return;
  }

  std::shared_ptr<odml::DlcClientPtr> dlc_client = odml::CreateDlcClient(
      kDlcPrefix + target_dlc_uuid,
      base::BindOnce(&MantisService::OnInstallMantisDlcComplete,
                     weak_ptr_factory_.GetWeakPtr(), std::move(processor),
                     std::move(callback), std::move(timer), progress_observer),
      base::BindRepeating(&OnDlcProgress, progress_observer, /*start=*/0.0,
                          kMantisDlcProgressAllocation));
  (*dlc_client)->InstallDlc();
}

void MantisService::CreateMantisProcessor(
    raw_ref<MetricsLibraryInterface> metrics_lib,
    scoped_refptr<base::SequencedTaskRunner> mantis_api_runner,
    const MantisAPI* api,
    mojo::PendingReceiver<mojom::MantisProcessor> processor,
    raw_ref<cros_safety::SafetyServiceManager> safety_service_manager,
    const raw_ref<i18n::Translator> translator,
    base::OnceCallback<void()> on_disconnected,
    base::OnceCallback<void(mantis::mojom::InitializeResult)> callback,
    odml::PerformanceTimer::Ptr timer,
    MantisComponent component) {
  processor_ = std::make_unique<MantisProcessor>(
      metrics_lib, std::move(mantis_api_runner), component, api,
      std::move(processor), safety_service_manager, translator,
      std::move(on_disconnected), std::move(callback));
  SendTimeMetric(*metrics_lib_, TimeMetric::kLoadModelLatency, *timer);
}

void MantisService::NotifyPendingProcessors() {
  is_initializing_processor_ = false;
  for (PendingProcessor& pending : pending_processors_) {
    processor_->AddReceiver(std::move(pending.processor));
    std::move(pending.callback).Run(mojom::InitializeResult::kSuccess);
  }
  pending_processors_.clear();
}

void MantisService::InitializeInternal(
    std::shared_ptr<mojo::Remote<mojom::PlatformModelProgressObserver>>
        progress_observer,
    mojo::PendingReceiver<mojom::MantisProcessor> processor,
    const std::optional<base::Uuid>& dlc_uuid,
    InitializeCallback callback) {
  // Determine if the model is already loaded here. The model might be ready
  // later, e.g. after DLC is installed. However, we consider that case
  // unloaded since we already do some processing.
  SendBoolMetric(*metrics_lib_, BoolMetric::kModelLoaded,
                 processor_ != nullptr);
  if (processor_) {
    processor_->AddReceiver(std::move(processor));
    std::move(callback).Run(mojom::InitializeResult::kSuccess);
    return;
  }
  std::string target_dlc_uuid = kDefaultDlcUUID;
  if (dlc_uuid.has_value() && dlc_uuid.value().is_valid()) {
    target_dlc_uuid = dlc_uuid.value().AsLowercaseString();
  }

  std::shared_ptr<odml::DlcClientPtr> dlc_client = odml::CreateDlcClient(
      kDlcPrefix + target_dlc_uuid,
      base::BindOnce(&MantisService::OnInstallVerifiedMantisDlcComplete,
                     weak_ptr_factory_.GetWeakPtr(), std::move(processor),
                     std::move(callback), odml::PerformanceTimer::Create(),
                     target_dlc_uuid, progress_observer),
      base::DoNothing());
  (*dlc_client)->InstallVerifiedDlcOnly();
}
}  // namespace mantis
