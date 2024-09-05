// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/coral/embedding/engine.h"

#include <utility>

#include <base/functional/callback.h>
#include <base/types/expected.h>

#include "odml/mojom/coral_service.mojom.h"
#include "odml/mojom/embedding_model.mojom.h"

namespace coral {

namespace {
using mojom::CoralError;
}

EmbeddingEngine::EmbeddingEngine(
    raw_ref<embedding_model::mojom::OnDeviceEmbeddingModelService>
        embedding_service)
    : embedding_service_(embedding_service) {}

void EmbeddingEngine::Process(mojom::GroupRequestPtr request,
                              EmbeddingCallback callback) {
  std::move(callback).Run(std::move(request),
                          base::unexpected(CoralError::kUnknownError));
}

}  // namespace coral
