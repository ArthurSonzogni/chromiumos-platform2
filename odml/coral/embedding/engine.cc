// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/coral/embedding/engine.h"

#include <memory>
#include <string>
#include <utility>

#include <base/functional/callback.h>
#include <base/functional/callback_helpers.h>
#include <base/hash/hash.h>
#include <base/strings/stringprintf.h>
#include <base/types/expected.h>
#include <mojo/public/cpp/bindings/callback_helpers.h>

#include "odml/coral/common.h"
#include "odml/mojom/coral_service.mojom.h"
#include "odml/mojom/embedding_model.mojom.h"
#include "odml/mojom/on_device_model.mojom.h"

namespace coral {

namespace {
using embedding_model::mojom::OnDeviceEmbeddingModelInferenceError;
using mojom::CoralError;
using on_device_model::mojom::LoadModelResult;

constexpr char kModelUuid[] = "a97333ed-3157-49a3-b503-2d2d3f23c81d";

// Files in /run/daemon-store-cache are prone to be cleaned up on low disk space
// situation.
// The full path of the embedding would be like
//   /run/daemon-store-cache/odmld/<user_hash>/coral/embeddings
// where the directory /run/daemon-store-cache/odmld/<user_hash> is
// automatically set up by the daemon store service on user login.
constexpr char kEmbeddingDatabaseRootDir[] = "/run/daemon-store-cache/odmld";

constexpr char kEmbeddingDatabaseSubDir[] = "coral";

constexpr char kEmbeddingDatabaseFileName[] = "embeddings";

constexpr base::TimeDelta kEmbeddingDatabaseCacheTime = base::Days(2);

// A string representation of Entity.
std::optional<std::string> EntityToString(const mojom::Entity& entity) {
  if (entity.is_app()) {
    const mojom::App& app = *entity.get_app();
    return base::StringPrintf("app<%s,%s>", app.title.c_str(), app.id.c_str());
  } else if (entity.is_tab()) {
    const mojom::Tab& tab = *entity.get_tab();
    return base::StringPrintf("tab<%s,%s>", tab.title.c_str(),
                              tab.url->url.c_str());
  }
  LOG(WARNING) << "Unrecognized entity type";
  return std::nullopt;
}

}  // namespace

namespace internal {

// TODO(b/361429567): Switch to the final prompt for the feature.
std::string EntityToEmbeddingPrompt(const mojom::Entity& entity) {
  if (entity.is_app()) {
    return base::StringPrintf("A page with title: \"%s\" and URL: \"\"\n",
                              entity.get_app()->title.c_str());
  } else if (entity.is_tab()) {
    const mojom::Tab& tab = *entity.get_tab();
    return base::StringPrintf("A page with title: \"%s\" and URL: \"%s\"\n",
                              tab.title.c_str(), tab.url->url.c_str());
  }
  return "";
}

// <entity representation>:<fingerprint of prompt and model version>
// Example:
//   tab<tab_title, tab_url>:2089388806
//   app<app_title, app_id>:4263199713
std::optional<std::string> EntityToCacheKey(const mojom::Entity& entity,
                                            const std::string& prompt,
                                            const std::string& model_version) {
  std::optional<std::string> entity_str = EntityToString(entity);
  if (!entity_str.has_value()) {
    return std::nullopt;
  }
  uint32_t hash = base::PersistentHash(
      base::StringPrintf("%s,%s", prompt.c_str(), model_version.c_str()));
  return base::StringPrintf("%s:%u", entity_str->c_str(), hash);
}

}  // namespace internal

EmbeddingEngine::EmbeddingEngine(
    raw_ref<CoralMetrics> metrics,
    raw_ref<embedding_model::mojom::OnDeviceEmbeddingModelService>
        embedding_service,
    std::unique_ptr<EmbeddingDatabaseFactory> embedding_database_factory,
    odml::SessionStateManagerInterface* session_state_manager)
    : metrics_(metrics),
      embedding_service_(embedding_service),
      embedding_database_factory_(std::move(embedding_database_factory)) {
  if (session_state_manager) {
    session_state_manager->AddObserver(this);
  }
}

void EmbeddingEngine::PrepareResource() {
  if (is_processing_) {
    pending_callbacks_.push(base::BindOnce(&EmbeddingEngine::PrepareResource,
                                           weak_ptr_factory_.GetWeakPtr()));
    return;
  }
  is_processing_ = true;
  // Ensure `is_processing_` will always be reset no matter callback is run or
  // dropped.
  EnsureModelLoaded(mojo::WrapCallbackWithDefaultInvokeIfNotRun(base::BindOnce(
      &EmbeddingEngine::OnProcessCompleted, weak_ptr_factory_.GetWeakPtr())));
}

void EmbeddingEngine::Process(mojom::GroupRequestPtr request,
                              EmbeddingCallback callback) {
  if (is_processing_) {
    pending_callbacks_.push(base::BindOnce(
        &EmbeddingEngine::Process, weak_ptr_factory_.GetWeakPtr(),
        std::move(request), std::move(callback)));
    return;
  }
  is_processing_ = true;

  auto timer = PerformanceTimer::Create();
  EmbeddingCallback wrapped_callback = base::BindOnce(
      &EmbeddingEngine::HandleProcessResult, weak_ptr_factory_.GetWeakPtr(),
      std::move(callback), std::move(timer));
  // Ensure `is_processing_` will always be reset no matter callback is run or
  // dropped.
  base::OnceClosure on_process_completed =
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          base::BindOnce(&EmbeddingEngine::OnProcessCompleted,
                         weak_ptr_factory_.GetWeakPtr()));
  EnsureModelLoaded(base::BindOnce(
      &EmbeddingEngine::DoProcess, weak_ptr_factory_.GetWeakPtr(),
      std::move(request),
      std::move(wrapped_callback).Then(std::move(on_process_completed))));
}

void EmbeddingEngine::OnUserLoggedIn(
    const odml::SessionStateManagerInterface::User& user) {
  LOG(INFO) << "EmbeddingEngine::OnUserLoggedIn";
  embedding_database_ = embedding_database_factory_->Create(
      base::FilePath(kEmbeddingDatabaseRootDir)
          .Append(user.hash)
          .Append(kEmbeddingDatabaseSubDir)
          .Append(kEmbeddingDatabaseFileName),
      kEmbeddingDatabaseCacheTime);
  sync_db_timer_.Start(FROM_HERE, internal::kEmbeddingDatabaseSyncPeriod,
                       base::BindRepeating(&EmbeddingEngine::SyncDatabase,
                                           weak_ptr_factory_.GetWeakPtr()));
}

void EmbeddingEngine::OnUserLoggedOut() {
  LOG(INFO) << "EmbeddingEngine::OnUserLoggedOut";
  sync_db_timer_.Stop();
  embedding_database_.reset();
}

void EmbeddingEngine::EnsureModelLoaded(base::OnceClosure callback) {
  if (model_) {
    std::move(callback).Run();
    return;
  }
  auto timer = PerformanceTimer::Create();
  embedding_service_->LoadEmbeddingModel(
      base::Uuid::ParseLowercase(kModelUuid),
      model_.BindNewPipeAndPassReceiver(), mojo::NullRemote(),
      base::BindOnce(&EmbeddingEngine::OnModelLoadResult,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback),
                     std::move(timer)));
}

void EmbeddingEngine::OnModelLoadResult(base::OnceClosure callback,
                                        PerformanceTimer::Ptr timer,
                                        LoadModelResult result) {
  if (result != LoadModelResult::kSuccess) {
    // Unbind the model because when load model fails we shouldn't be using the
    // model.
    model_.reset();
    LOG(ERROR) << "Load model failed with result: " << static_cast<int>(result);
    std::move(callback).Run();
    return;
  }
  metrics_->SendLoadEmbeddingModelLatency(timer->GetDuration());
  model_->Version(base::BindOnce(&EmbeddingEngine::OnModelVersionLoaded,
                                 weak_ptr_factory_.GetWeakPtr(),
                                 std::move(callback)));
}

void EmbeddingEngine::OnModelVersionLoaded(base::OnceClosure callback,
                                           const std::string& version) {
  model_version_ = version;
  std::move(callback).Run();
}

void EmbeddingEngine::DoProcess(mojom::GroupRequestPtr request,
                                EmbeddingCallback callback) {
  if (!model_) {
    std::move(callback).Run(std::move(request),
                            base::unexpected(CoralError::kLoadModelFailed));
    return;
  }
  std::vector<std::string> prompts;
  for (const mojom::EntityPtr& entity : request->entities) {
    std::string prompt = internal::EntityToEmbeddingPrompt(*entity);
    // TODO(b/361429567): We can achieve better error tolerance by dropping
    // problematic input entities. For now, fail on any error for simplicity.
    if (prompt.empty()) {
      std::move(callback).Run(std::move(request),
                              base::unexpected(CoralError::kInvalidArgs));
      return;
    }
    prompts.push_back(std::move(prompt));
  }
  ProcessEachPrompt(std::move(request), std::move(prompts), EmbeddingResponse(),
                    std::move(callback));
}

void EmbeddingEngine::ProcessEachPrompt(mojom::GroupRequestPtr request,
                                        std::vector<std::string> prompts,
                                        EmbeddingResponse response,
                                        EmbeddingCallback callback) {
  size_t index = response.embeddings.size();
  // > covers the index out-of-range case although it shouldn't happen.
  if (index >= prompts.size()) {
    std::move(callback).Run(std::move(request), std::move(response));
    return;
  }
  // |prmopts| could be used in OnModelOutput() when computing the cache key.
  // So do not consume it.
  const std::string& prompt = prompts[index];

  // Try getting the embedding from the cache database.
  if (embedding_database_) {
    std::optional<std::string> cache_key = internal::EntityToCacheKey(
        *request->entities[index], prompt, model_version_);
    if (cache_key.has_value()) {
      std::optional<Embedding> embedding = embedding_database_->Get(*cache_key);
      if (embedding.has_value()) {
        response.embeddings.push_back(*embedding);
        ProcessEachPrompt(std::move(request), std::move(prompts),
                          std::move(response), std::move(callback));
        return;
      }
    }
  }

  auto timer = PerformanceTimer::Create();
  auto on_model_output = base::BindOnce(
      &EmbeddingEngine::OnModelOutput, weak_ptr_factory_.GetWeakPtr(),
      std::move(request), std::move(prompts), std::move(response),
      std::move(callback), std::move(timer));
  auto embed_request = embedding_model::mojom::GenerateEmbeddingRequest::New();
  embed_request->content = prompt;
  embed_request->task_type = embedding_model::mojom::TaskType::kClustering;
  embed_request->truncate_input = true;
  model_->GenerateEmbedding(std::move(embed_request),
                            std::move(on_model_output));
}

void EmbeddingEngine::OnModelOutput(mojom::GroupRequestPtr request,
                                    std::vector<std::string> prompts,
                                    EmbeddingResponse response,
                                    EmbeddingCallback callback,
                                    PerformanceTimer::Ptr timer,
                                    OnDeviceEmbeddingModelInferenceError error,
                                    const std::vector<float>& embedding) {
  // TODO(b/361429567): We can achieve better error tolerance by dropping
  // problematic input entities. For now, fail on any error for simplicity.
  if (error != OnDeviceEmbeddingModelInferenceError::kSuccess) {
    LOG(ERROR) << "Model execution failed with result: "
               << static_cast<int>(error);
    std::move(callback).Run(
        std::move(request),
        base::unexpected(CoralError::kModelExecutionFailed));
    return;
  }
  metrics_->SendGenerateEmbeddingLatency(timer->GetDuration());

  // Cache the embedding.
  if (embedding_database_) {
    size_t index = response.embeddings.size();
    std::optional<std::string> cache_key = internal::EntityToCacheKey(
        *request->entities[index], prompts[index], model_version_);
    if (cache_key.has_value()) {
      embedding_database_->Put(*cache_key, embedding);
    }
  }

  response.embeddings.push_back(embedding);
  ProcessEachPrompt(std::move(request), std::move(prompts), std::move(response),
                    std::move(callback));
}

void EmbeddingEngine::SyncDatabase() {
  if (embedding_database_) {
    embedding_database_->Sync();
  }
}

void EmbeddingEngine::HandleProcessResult(
    EmbeddingCallback callback,
    PerformanceTimer::Ptr timer,
    mojom::GroupRequestPtr request,
    CoralResult<EmbeddingResponse> result) {
  // We don't want to send some metrics for Process requests triggered by
  // CacheEmbedding. This is because for we want to analyze most of this
  // engine's metrics (like cache hits) only for Group requests. CacheEmbedding
  // operation sends metrics too in service.cc, and since CacheEmbedding only
  // passes through this engine, there is no need to send some metrics for it
  // here again.
  // The hacky but easiest way to determine whether the request is a
  // CacheEmbeddings request for now is to check whether the clustering_options
  // (or title_generation_options) is null.
  if (request->clustering_options) {
    metrics_->SendEmbeddingEngineStatus(result.transform([](auto&&) {}));
    if (result.has_value()) {
      metrics_->SendEmbeddingEngineLatency(timer->GetDuration());
    }
  }
  std::move(callback).Run(std::move(request), std::move(result));
}

void EmbeddingEngine::OnProcessCompleted() {
  is_processing_ = false;
  if (pending_callbacks_.empty()) {
    return;
  }
  base::OnceClosure callback = std::move(pending_callbacks_.front());
  pending_callbacks_.pop();
  std::move(callback).Run();
}

}  // namespace coral
