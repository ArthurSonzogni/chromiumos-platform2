// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/coral/service.h"

#include "odml/mojom/coral_service.mojom.h"

namespace coral {

namespace {
using mojom::CacheEmbeddingsResult;
using mojom::CoralError;
using mojom::GroupResult;
}  // namespace

void CoralService::Group(mojom::GroupRequestPtr request,
                         GroupCallback callback) {
  auto result = GroupResult::NewError(CoralError::kUnknownError);
  std::move(callback).Run(std::move(result));
}

void CoralService::CacheEmbeddings(mojom::CacheEmbeddingsRequestPtr request,
                                   CacheEmbeddingsCallback callback) {
  auto result = CacheEmbeddingsResult::NewError(CoralError::kUnknownError);
  std::move(callback).Run(std::move(result));
}

}  // namespace coral
