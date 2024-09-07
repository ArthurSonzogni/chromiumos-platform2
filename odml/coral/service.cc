// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/coral/service.h"

#include "odml/coral/clustering/agglomerative_clustering.h"
#include "odml/coral/clustering/engine.h"
#include "odml/coral/common.h"
#include "odml/coral/embedding/engine.h"
#include "odml/coral/title_generation/engine.h"
#include "odml/mojom/coral_service.mojom.h"
#include "odml/mojom/on_device_model_service.mojom.h"

namespace coral {

namespace {
using mojom::CacheEmbeddingsResult;
using mojom::CoralError;
using mojom::GroupResponse;
using mojom::GroupResult;
}  // namespace

CoralService::CoralService(
    raw_ref<on_device_model::mojom::OnDeviceModelPlatformService>
        on_device_model_service,
    raw_ref<embedding_model::mojom::OnDeviceEmbeddingModelService>
        embedding_model_service)
    : CoralService(
          std::make_unique<EmbeddingEngine>(embedding_model_service),
          std::make_unique<ClusteringEngine>(
              std::make_unique<clustering::ClusteringFactory>()),
          std::make_unique<TitleGenerationEngine>(on_device_model_service)) {}

CoralService::CoralService(
    std::unique_ptr<EmbeddingEngineInterface> embedding_engine,
    std::unique_ptr<ClusteringEngineInterface> clustering_engine,
    std::unique_ptr<TitleGenerationEngineInterface> title_generation_engine)
    : embedding_engine_(std::move(embedding_engine)),
      clustering_engine_(std::move(clustering_engine)),
      title_generation_engine_(std::move(title_generation_engine)) {}

void CoralService::Group(mojom::GroupRequestPtr request,
                         GroupCallback callback) {
  embedding_engine_->Process(
      std::move(request),
      base::BindOnce(&CoralService::OnEmbeddingResult,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void CoralService::CacheEmbeddings(mojom::CacheEmbeddingsRequestPtr request,
                                   CacheEmbeddingsCallback callback) {
  // Turn the request into a full group request to reuse the same helper
  // functions.
  auto group_request = mojom::GroupRequest::New(
      std::move(request->entities), std::move(request->embedding_options),
      nullptr, nullptr);
  embedding_engine_->Process(
      std::move(group_request),
      base::BindOnce(
          [](CacheEmbeddingsCallback callback, mojom::GroupRequestPtr request,
             CoralResult<EmbeddingResponse> embed_result) {
            mojom::CacheEmbeddingsResultPtr result;
            if (!embed_result.has_value()) {
              result = CacheEmbeddingsResult::NewError(embed_result.error());
            } else {
              result = CacheEmbeddingsResult::NewResponse(
                  mojom::CacheEmbeddingsResponse::New());
            }
            std::move(callback).Run(std::move(result));
          },
          std::move(callback)));
}

void CoralService::OnEmbeddingResult(GroupCallback callback,
                                     mojom::GroupRequestPtr request,
                                     CoralResult<EmbeddingResponse> result) {
  if (!result.has_value()) {
    std::move(callback).Run(GroupResult::NewError(result.error()));
    return;
  }
  clustering_engine_->Process(
      std::move(request), std::move(*result),
      base::BindOnce(&CoralService::OnClusteringResult,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void CoralService::OnClusteringResult(GroupCallback callback,
                                      mojom::GroupRequestPtr request,
                                      CoralResult<ClusteringResponse> result) {
  if (!result.has_value()) {
    std::move(callback).Run(GroupResult::NewError(result.error()));
    return;
  }
  title_generation_engine_->Process(
      std::move(request), std::move(*result),
      base::BindOnce(&CoralService::OnTitleGenerationResult,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void CoralService::OnTitleGenerationResult(
    GroupCallback callback,
    mojom::GroupRequestPtr request,
    CoralResult<TitleGenerationResponse> result) {
  if (!result.has_value()) {
    std::move(callback).Run(GroupResult::NewError(result.error()));
    return;
  }
  std::move(callback).Run(
      GroupResult::NewResponse(GroupResponse::New(std::move(result->groups))));
}

}  // namespace coral
