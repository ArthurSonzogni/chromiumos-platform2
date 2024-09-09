// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/coral/embedding/engine.h"

#include <string>
#include <utility>

#include <base/functional/callback.h>
#include <base/strings/stringprintf.h>
#include <base/types/expected.h>

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
        embedding_service)
    : embedding_service_(embedding_service) {}

void EmbeddingEngine::Process(mojom::GroupRequestPtr request,
                              EmbeddingCallback callback) {
  EnsureModelLoaded(base::BindOnce(&EmbeddingEngine::DoProcess,
                                   weak_ptr_factory_.GetWeakPtr(),
                                   std::move(request), std::move(callback)));
}

void EmbeddingEngine::EnsureModelLoaded(base::OnceClosure callback) {
  if (model_) {
    std::move(callback).Run();
    return;
  }
  embedding_service_->LoadEmbeddingModel(
      base::Uuid::ParseLowercase(kModelUuid),
      model_.BindNewPipeAndPassReceiver(), mojo::NullRemote(),
      base::BindOnce(&EmbeddingEngine::OnModelLoadResult,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void EmbeddingEngine::OnModelLoadResult(base::OnceClosure callback,
                                        LoadModelResult result) {
  if (result != LoadModelResult::kSuccess) {
    // Unbind the model so we can use `model_.is_bound()` to check whether the
    // model is loaded.
    model_.reset();
    LOG(ERROR) << "Load model failed with result: " << static_cast<int>(result);
  }
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
  auto options =
      embedding_model::mojom::OnDeviceEmbeddingModelInferenceOptions::New();
  options->truncate_input = true;
  // TODO(b/361429567): The input should be formatted according to the embedding
  // model.
  model_->GenerateEmbedding(std::move(prompt), std::move(options),
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
