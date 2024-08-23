// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/coral/embedding/engine.h"

#include <utility>

#include <base/functional/callback.h>
#include <base/types/expected.h>

#include "odml/mojom/coral_service.mojom.h"

namespace coral {

namespace {
using mojom::CoralError;
}

EmbeddingEngine::EmbeddingEngine() = default;

void EmbeddingEngine::Process(const mojom::GroupRequest& request,
                              EmbeddingCallback callback) {
  std::move(callback).Run(base::unexpected(CoralError::kUnknownError));
}

}  // namespace coral
