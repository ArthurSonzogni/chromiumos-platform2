// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/mantis/service.h"

#include "odml/mojom/mantis_processor.mojom.h"
#include "odml/mojom/mantis_service.mojom.h"
#include "odml/mojom/on_device_model.mojom.h"
#include "odml/mojom/on_device_model_service.mojom.h"
#include "odml/utils/odml_shim_loader.h"

namespace mantis {

namespace {
using on_device_model::mojom::LoadModelResult;
using on_device_model::mojom::PlatformModelProgressObserver;
}  // namespace

MantisService::MantisService(raw_ref<odml::OdmlShimLoader> shim_loader)
    : shim_loader_(shim_loader) {}

void MantisService::Initialize(
    mojo::PendingRemote<PlatformModelProgressObserver> progress_observer,
    mojo::PendingReceiver<mojom::MantisProcessor> processor,
    InitializeCallback callback) {
  auto result = LoadModelResult::kFailedToLoadLibrary;
  std::move(callback).Run(std::move(result));
}

}  // namespace mantis
