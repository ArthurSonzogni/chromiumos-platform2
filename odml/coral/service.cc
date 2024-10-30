// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/coral/service.h"

#include <metrics/metrics_library.h>

#include "odml/coral/clustering/engine.h"
#include "odml/coral/common.h"
#include "odml/coral/embedding/embedding_database.h"
#include "odml/coral/embedding/engine.h"
#include "odml/coral/title_generation/engine.h"
#include "odml/mojom/coral_service.mojom.h"
#include "odml/mojom/on_device_model_service.mojom.h"
#include "odml/session_state_manager/session_state_manager.h"

namespace coral {

namespace {
using mojom::CacheEmbeddingsResult;
using mojom::CoralError;
using mojom::GroupResponse;
using mojom::GroupResult;
}  // namespace

CoralService::CoralService(
    raw_ref<MetricsLibraryInterface> metrics,
    raw_ref<on_device_model::mojom::OnDeviceModelPlatformService>
        on_device_model_service,
    raw_ref<embedding_model::mojom::OnDeviceEmbeddingModelService>
        embedding_model_service,
    odml::SessionStateManagerInterface* session_state_manager)
    : metrics_(metrics),
      embedding_engine_(std::make_unique<EmbeddingEngine>(
          raw_ref(metrics_),
          embedding_model_service,
          std::make_unique<EmbeddingDatabaseFactory>(),
          session_state_manager)),
      clustering_engine_(std::make_unique<ClusteringEngine>(
          raw_ref(metrics_),
          std::make_unique<clustering::ClusteringFactory>())),
      title_generation_engine_(std::make_unique<TitleGenerationEngine>(
          raw_ref(metrics_), on_device_model_service, session_state_manager)) {}

CoralService::CoralService(
    raw_ref<MetricsLibraryInterface> metrics,
    std::unique_ptr<EmbeddingEngineInterface> embedding_engine,
    std::unique_ptr<ClusteringEngineInterface> clustering_engine,
    std::unique_ptr<TitleGenerationEngineInterface> title_generation_engine)
    : metrics_(metrics),
      embedding_engine_(std::move(embedding_engine)),
      clustering_engine_(std::move(clustering_engine)),
      title_generation_engine_(std::move(title_generation_engine)) {}

void CoralService::PrepareResource() {
  embedding_engine_->PrepareResource();
  title_generation_engine_->PrepareResource();
}

void CoralService::Group(mojom::GroupRequestPtr request,
                         mojo::PendingRemote<mojom::TitleObserver> observer,
                         GroupCallback callback) {
  auto timer = PerformanceTimer::Create();
  GroupCallback wrapped_callback = base::BindOnce(
      &CoralService::HandleGroupResult, weak_ptr_factory_.GetWeakPtr(),
      std::move(timer), std::move(callback));
  embedding_engine_->Process(
      std::move(request),
      base::BindOnce(&CoralService::OnEmbeddingResult,
                     weak_ptr_factory_.GetWeakPtr(),
                     std::move(wrapped_callback), std::move(observer)));
}

void CoralService::CacheEmbeddings(mojom::CacheEmbeddingsRequestPtr request,
                                   CacheEmbeddingsCallback callback) {
  auto timer = PerformanceTimer::Create();
  // Turn the request into a full group request to reuse the same helper
  // functions.
  auto group_request = mojom::GroupRequest::New(
      std::move(request->entities), std::move(request->embedding_options),
      nullptr, nullptr);
  embedding_engine_->Process(
      std::move(group_request),
      base::BindOnce(&CoralService::HandleCacheEmbeddingsResult,
                     weak_ptr_factory_.GetWeakPtr(), std::move(timer),
                     std::move(callback)));
}

void CoralService::OnEmbeddingResult(
    GroupCallback callback,
    mojo::PendingRemote<mojom::TitleObserver> observer,
    mojom::GroupRequestPtr request,
    CoralResult<EmbeddingResponse> result) {
  if (!result.has_value()) {
    std::move(callback).Run(GroupResult::NewError(result.error()));
    return;
  }
  clustering_engine_->Process(
      std::move(request), std::move(*result),
      base::BindOnce(&CoralService::OnClusteringResult,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback),
                     std::move(observer)));
}

void CoralService::OnClusteringResult(
    GroupCallback callback,
    mojo::PendingRemote<mojom::TitleObserver> observer,
    mojom::GroupRequestPtr request,
    CoralResult<ClusteringResponse> result) {
  if (!result.has_value()) {
    std::move(callback).Run(GroupResult::NewError(result.error()));
    return;
  }
  title_generation_engine_->Process(
      std::move(request), std::move(*result), std::move(observer),
      base::BindOnce(&CoralService::OnTitleGenerationResult,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void CoralService::OnTitleGenerationResult(
    GroupCallback callback, CoralResult<TitleGenerationResponse> result) {
  if (!result.has_value()) {
    std::move(callback).Run(GroupResult::NewError(result.error()));
    return;
  }
  std::move(callback).Run(
      GroupResult::NewResponse(GroupResponse::New(std::move(result->groups))));
}

void CoralService::HandleGroupResult(PerformanceTimer::Ptr timer,
                                     GroupCallback callback,
                                     mojom::GroupResultPtr result) {
  CoralStatus status;
  if (result->is_error()) {
    status = base::unexpected(result->get_error());
  } else {
    status = base::ok();
    metrics_.SendGroupLatency(timer->GetDuration());
  }
  metrics_.SendGroupStatus(status);
  std::move(callback).Run(std::move(result));
}

void CoralService::HandleCacheEmbeddingsResult(
    PerformanceTimer::Ptr timer,
    CacheEmbeddingsCallback callback,
    mojom::GroupRequestPtr request,
    CoralResult<EmbeddingResponse> embed_result) {
  mojom::CacheEmbeddingsResultPtr result;
  CoralStatus status;
  if (!embed_result.has_value()) {
    status = base::unexpected(embed_result.error());
    result = CacheEmbeddingsResult::NewError(embed_result.error());
  } else {
    status = base::ok();
    result = CacheEmbeddingsResult::NewResponse(
        mojom::CacheEmbeddingsResponse::New());
    metrics_.SendCacheEmbeddingsLatency(timer->GetDuration());
  }
  metrics_.SendCacheEmbeddingsStatus(status);
  std::move(callback).Run(std::move(result));
}

}  // namespace coral
