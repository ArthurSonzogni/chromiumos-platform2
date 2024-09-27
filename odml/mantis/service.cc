// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/mantis/service.h"

#include <memory>

#include "odml/mantis/processor.h"
#include "odml/mojom/mantis_processor.mojom.h"
#include "odml/mojom/mantis_service.mojom.h"
#include "odml/mojom/on_device_model.mojom.h"
#include "odml/mojom/on_device_model_service.mojom.h"
#include "odml/utils/odml_shim_loader.h"

namespace mantis {

namespace {
using mojom::MantisFeatureStatus;
using on_device_model::mojom::LoadModelResult;
using on_device_model::mojom::PlatformModelProgressObserver;
using MantisAPIGetter = const MantisAPI* (*)();
}  // namespace

MantisService::MantisService(raw_ref<odml::OdmlShimLoader> shim_loader)
    : shim_loader_(shim_loader) {}

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
}

void MantisService::Initialize(
    mojo::PendingRemote<PlatformModelProgressObserver> progress_observer,
    mojo::PendingReceiver<mojom::MantisProcessor> processor,
    InitializeCallback callback) {
  if (RetryIfShimIsNotReady(&MantisService::Initialize, callback,
                            LoadModelResult::kFailedToLoadLibrary,
                            progress_observer, processor)) {
    return;
  }

  if (processor_) {
    processor_->AddReceiver(std::move(processor));
    std::move(callback).Run(LoadModelResult::kSuccess);
    return;
  }

  auto get_api = shim_loader_->Get<MantisAPIGetter>("GetMantisAPI");
  if (!get_api) {
    LOG(ERROR) << "Unable to resolve GetMantisAPI() symbol.";
    std::move(callback).Run(LoadModelResult::kFailedToLoadLibrary);
    return;
  }

  const MantisAPI* api = get_api();
  if (!api) {
    LOG(ERROR) << "Unable to get MantisAPI.";
    std::move(callback).Run(LoadModelResult::kFailedToLoadLibrary);
    return;
  }

  // TODO(b/366338439): Load the assets through DLC.
  // TODO(b/365638444): Run on another thread to prevent blocking the main
  // thread.
  MantisComponent component = api->Initialize("/tmp/mantis_assets");

  processor_ = std::make_unique<MantisProcessor>(
      component, api, std::move(processor),
      base::BindOnce(&MantisService::DeleteProcessor, base::Unretained(this)));

  std::move(callback).Run(LoadModelResult::kSuccess);
}

void MantisService::GetMantisFeatureStatus(
    GetMantisFeatureStatusCallback callback) {
  std::move(callback).Run(MantisFeatureStatus::kDeviceNotSupported);
}

}  // namespace mantis
