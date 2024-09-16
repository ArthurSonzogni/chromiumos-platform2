// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/embedding_model/embedding_model_service.h"

namespace embedding_model {

EmbeddingModelService::EmbeddingModelService(
    raw_ref<MetricsLibraryInterface> metrics, raw_ref<ModelFactory> factory)
    : metrics_(metrics), factory_(factory) {}

EmbeddingModelService::~EmbeddingModelService() = default;

void EmbeddingModelService::LoadEmbeddingModel(
    const base::Uuid& uuid,
    mojo::PendingReceiver<mojom::OnDeviceEmbeddingModel> model,
    mojo::PendingRemote<on_device_model::mojom::PlatformModelProgressObserver>
        progress_observer,
    LoadEmbeddingModelCallback callback) {
  // Not yet implemented.
}

}  // namespace embedding_model
