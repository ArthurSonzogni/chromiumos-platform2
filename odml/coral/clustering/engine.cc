// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/coral/clustering/engine.h"

#include <utility>

#include <base/functional/callback.h>
#include <base/types/expected.h>

#include "odml/coral/embedding/engine.h"
#include "odml/mojom/coral_service.mojom.h"

namespace coral {

namespace {
using mojom::CoralError;
}

ClusteringEngine::ClusteringEngine() = default;

void ClusteringEngine::Process(const mojom::GroupRequest& request,
                               const EmbeddingResponse& embedding_response,
                               ClusteringCallback callback) {
  std::move(callback).Run(base::unexpected(CoralError::kUnknownError));
}

}  // namespace coral
