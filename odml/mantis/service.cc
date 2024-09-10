// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/mantis/service.h"

#include "odml/mojom/mantis_processor.mojom.h"
#include "odml/mojom/mantis_service.mojom.h"
#include "odml/mojom/on_device_model.mojom.h"
#include "odml/mojom/on_device_model_service.mojom.h"

namespace mantis {

namespace {
using mojom::LoadModelResult;
using mojom::PlatformModelProgressObserver;
}  // namespace

void MantisService::Initialize(
    mojo::PendingRemote<mojom::PlatformModelProgressObserver> progress_observer,
    mojo::PendingReceiver<mojom::MantisProcessor> processor,
    InitializeCallback callback) {
  auto result = LoadModelResult::kFailedToLoadLibrary;
  std::move(callback).Run(std::move(result));
}

}  // namespace mantis
