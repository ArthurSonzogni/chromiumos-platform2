// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/coral/service.h"

#include <string>
#include <vector>

#include <metrics/metrics_library.h>

#include "odml/coral/clustering/engine.h"
#include "odml/coral/common.h"
#include "odml/coral/embedding/embedding_database.h"
#include "odml/coral/embedding/engine.h"
#include "odml/coral/title_generation/cache_storage.h"
#include "odml/coral/title_generation/engine.h"
#include "odml/i18n/ml_service_language_detector.h"
#include "odml/mojom/coral_service.mojom.h"
#include "odml/mojom/on_device_model_service.mojom.h"
#include "odml/session_state_manager/session_state_manager.h"
#include "odml/utils/performance_timer.h"

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
    odml::SessionStateManagerInterface* session_state_manager,
    raw_ref<cros_safety::SafetyServiceManager> safety_service_manager,
    raw_ref<i18n::Translator> translator)
    : metrics_(metrics),
      language_detector_(
          std::make_unique<on_device_model::MlServiceLanguageDetector>()),
      embedding_engine_(std::make_unique<EmbeddingEngine>(
          raw_ref(metrics_),
          embedding_model_service,
          safety_service_manager,
          std::make_unique<EmbeddingDatabaseFactory>(),
          session_state_manager,
          raw_ref(*language_detector_.get()))),
      clustering_engine_(std::make_unique<ClusteringEngine>(
          raw_ref(metrics_),
          std::make_unique<clustering::ClusteringFactory>())),
      title_generation_engine_(std::make_unique<TitleGenerationEngine>(
          raw_ref(metrics_),
          on_device_model_service,
          session_state_manager,
          translator,
          std::make_unique<TitleCacheStorage>(std::nullopt,
                                              raw_ref(metrics_)))) {}

CoralService::CoralService(
    raw_ref<MetricsLibraryInterface> metrics,
    std::unique_ptr<EmbeddingEngineInterface> embedding_engine,
    std::unique_ptr<ClusteringEngineInterface> clustering_engine,
    std::unique_ptr<TitleGenerationEngineInterface> title_generation_engine)
    : metrics_(metrics),
      language_detector_(
          std::make_unique<on_device_model::MlServiceLanguageDetector>()),
      embedding_engine_(std::move(embedding_engine)),
      clustering_engine_(std::move(clustering_engine)),
      title_generation_engine_(std::move(title_generation_engine)) {}

void CoralService::PrepareResource() {
  // Deprecated.
}

void CoralService::Initialize(
    mojo::PendingRemote<
        chromeos::machine_learning::mojom::MachineLearningService> ml_service,
    mojo::PendingReceiver<mojom::CoralProcessor> receiver,
    const std::optional<std::string>& language_code) {
  if (!ml_service_) {
    if (!ml_service.is_valid()) {
      LOG(ERROR) << "Initializing CoralService failed due to invalid "
                    "ml_service remote.";
      return;
    } else {
      ml_service_.Bind(std::move(ml_service));
      language_detector_->Initialize(*ml_service_.get());
      ml_service_.reset_on_disconnect();
    }
  }
  embedding_engine_->PrepareResource();
  title_generation_engine_->PrepareResource(language_code);
  processor_receiver_set_.Add(this, std::move(receiver),
                              base::SequencedTaskRunner::GetCurrentDefault());
}

void CoralService::GroupDeprecated(
    mojom::GroupRequestPtr request,
    mojo::PendingRemote<mojom::TitleObserver> observer,
    GroupDeprecatedCallback callback) {
  // TODO(b/390555211): This will soon be deprecated and remove.
  Group(std::move(request), std::move(observer), std::move(callback));
}

void CoralService::CacheEmbeddingsDeprecated(
    mojom::CacheEmbeddingsRequestPtr request,
    CacheEmbeddingsDeprecatedCallback callback) {
  // TODO(b/390555211): This will soon be deprecated and remove.
  CacheEmbeddings(std::move(request), std::move(callback));
}

void CoralService::Group(mojom::GroupRequestPtr request,
                         mojo::PendingRemote<mojom::TitleObserver> observer,
                         GroupCallback callback) {
  metrics_.SendGroupInputCount(request->entities.size());
  auto timer = odml::PerformanceTimer::Create();
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
  auto timer = odml::PerformanceTimer::Create();
  // Turn the request into a full group request to reuse the same helper
  // functions.
  auto group_request = mojom::GroupRequest::New(
      std::move(request->entities), std::move(request->embedding_options),
      nullptr, nullptr, std::vector<mojom::EntityPtr>());
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

  std::vector<mojom::EntityPtr> suppression_context;
  if (request->suppression_context.has_value()) {
    for (auto& entity : *request->suppression_context) {
      suppression_context.push_back(entity->Clone());
    }
  }
  auto suppression_context_group_request = mojom::GroupRequest::New(
      /*entities=*/std::move(suppression_context),
      request->embedding_options->Clone(),
      /*clustering_options=*/nullptr, /*title_generation_options=*/nullptr,
      /*suppression_context=*/std::vector<mojom::EntityPtr>());
  embedding_engine_->Process(
      std::move(suppression_context_group_request),
      base::BindOnce(&CoralService::OnExistingEmbeddingResult,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback),
                     std::move(observer), std::move(result),
                     std::move(request)));
}

void CoralService::OnExistingEmbeddingResult(
    GroupCallback callback,
    mojo::PendingRemote<mojom::TitleObserver> observer,
    CoralResult<EmbeddingResponse> original_result,
    mojom::GroupRequestPtr original_request,
    mojom::GroupRequestPtr suppression_context_request,
    CoralResult<EmbeddingResponse> suppression_context_result) {
  if (!suppression_context_result.has_value()) {
    std::move(callback).Run(
        GroupResult::NewError(suppression_context_result.error()));
    return;
  }

  clustering_engine_->Process(
      std::move(original_request), std::move(*original_result),
      std::move(*suppression_context_result),
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

void CoralService::HandleGroupResult(odml::PerformanceTimer::Ptr timer,
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
    odml::PerformanceTimer::Ptr timer,
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
