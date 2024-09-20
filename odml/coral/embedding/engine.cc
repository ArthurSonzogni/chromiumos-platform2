// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/coral/embedding/engine.h"

#include <string>
#include <utility>

#include <base/functional/callback.h>
#include <base/strings/stringprintf.h>
#include <base/types/expected.h>

#include "odml/coral/common.h"
#include "odml/mojom/coral_service.mojom.h"
#include "odml/mojom/embedding_model.mojom.h"
#include "odml/mojom/on_device_model.mojom.h"

namespace coral {

namespace {
using embedding_model::mojom::OnDeviceEmbeddingModelInferenceError;
using mojom::CoralError;
using on_device_model::mojom::LoadModelResult;

// TODO(b/361429567): Decide and use the final embedding model. This is just a
// random UUID for now.
constexpr char kModelUuid[] = "a4ad9399-76c5-4b37-8982-41cb5996ca69";

// TODO(b/361429567): Switch to the final prompt for the feature..
std::string EntityToEmbeddingPrompt(const mojom::Entity& entity) {
  if (entity.is_app()) {
    return base::StringPrintf("A page with title: %s\n",
                              entity.get_app()->title.c_str());
  } else if (entity.is_tab()) {
    const mojom::Tab& tab = *entity.get_tab();
    return base::StringPrintf("A page with title: %s and URL: %s\n",
                              tab.title.c_str(), tab.url->url.c_str());
  }
  return "";
}

}  // namespace

EmbeddingEngine::EmbeddingEngine(
    raw_ref<embedding_model::mojom::OnDeviceEmbeddingModelService>
        embedding_service,
    odml::SessionStateManagerInterface* session_state_manager)
    : embedding_service_(embedding_service) {
  if (session_state_manager) {
    session_state_manager->AddObserver(this);
  }
}

void EmbeddingEngine::Process(mojom::GroupRequestPtr request,
                              EmbeddingCallback callback) {
  EnsureModelLoaded(base::BindOnce(&EmbeddingEngine::DoProcess,
                                   weak_ptr_factory_.GetWeakPtr(),
                                   std::move(request), std::move(callback)));
}

void EmbeddingEngine::OnUserLoggedIn(
    const odml::SessionStateManagerInterface::User& user) {
  LOG(INFO) << "EmbeddingEngine::OnUserLoggedIn";
}

void EmbeddingEngine::OnUserLoggedOut() {
  LOG(INFO) << "EmbeddingEngine::OnUserLoggedOut";
}

void EmbeddingEngine::EnsureModelLoaded(base::OnceClosure callback) {
  switch (state_) {
    case ModelLoadState::kLoaded:
      std::move(callback).Run();
      return;
    case ModelLoadState::kPending:
      pending_callbacks_.push_back(std::move(callback));
      return;
    case ModelLoadState::kNew:
      state_ = ModelLoadState::kPending;
      pending_callbacks_.push_back(std::move(callback));
      embedding_service_->LoadEmbeddingModel(
          base::Uuid::ParseLowercase(kModelUuid),
          model_.BindNewPipeAndPassReceiver(), mojo::NullRemote(),
          base::BindOnce(&EmbeddingEngine::OnModelLoadResult,
                         weak_ptr_factory_.GetWeakPtr()));
      return;
  }
}

void EmbeddingEngine::OnModelLoadResult(LoadModelResult result) {
  if (result != LoadModelResult::kSuccess) {
    // Unbind the model because when load model fails we shouldn't be using the
    // model. And set state to New so that the next request can attempt to load
    // the model again.
    model_.reset();
    state_ = ModelLoadState::kNew;
    LOG(ERROR) << "Load model failed with result: " << static_cast<int>(result);
  } else {
    state_ = ModelLoadState::kLoaded;
  }
  std::vector<base::OnceClosure> pending_callbacks =
      std::move(pending_callbacks_);
  pending_callbacks_.clear();
  for (base::OnceClosure& callback : pending_callbacks) {
    std::move(callback).Run();
  }
}

void EmbeddingEngine::DoProcess(mojom::GroupRequestPtr request,
                                EmbeddingCallback callback) {
  if (!model_) {
    std::move(callback).Run(std::move(request),
                            base::unexpected(CoralError::kLoadModelFailed));
    return;
  }
  CHECK_EQ(state_, ModelLoadState::kLoaded);

  std::vector<std::string> prompts;
  for (const mojom::EntityPtr& entity : request->entities) {
    std::string prompt = EntityToEmbeddingPrompt(*entity);
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
  // We'll never need prompts[index] anymore.
  std::string prompt = std::move(prompts[index]);
  auto on_model_output = base::BindOnce(
      &EmbeddingEngine::OnModelOutput, weak_ptr_factory_.GetWeakPtr(),
      std::move(request), std::move(prompts), std::move(response),
      std::move(callback));
  auto embed_request = embedding_model::mojom::GenerateEmbeddingRequest::New();
  embed_request->content = std::move(prompt);
  embed_request->task_type = embedding_model::mojom::TaskType::kClustering;
  embed_request->truncate_input = true;
  model_->GenerateEmbedding(std::move(embed_request),
                            std::move(on_model_output));
}

void EmbeddingEngine::OnModelOutput(mojom::GroupRequestPtr request,
                                    std::vector<std::string> prompts,
                                    EmbeddingResponse response,
                                    EmbeddingCallback callback,
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
  response.embeddings.push_back(embedding);
  ProcessEachPrompt(std::move(request), std::move(prompts), std::move(response),
                    std::move(callback));
}

}  // namespace coral
