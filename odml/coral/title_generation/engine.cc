// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/coral/title_generation/engine.h"

#include <string>
#include <utility>
#include <vector>

#include <base/functional/bind.h>
#include <base/functional/callback.h>
#include <base/location.h>
#include <base/strings/stringprintf.h>
#include <base/task/sequenced_task_runner.h>
#include <base/time/time.h>
#include <base/types/expected.h>
#include <base/uuid.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "mojo/public/cpp/bindings/pending_remote.h"
#include "odml/coral/clustering/engine.h"
#include "odml/coral/title_generation/simple_session.h"
#include "odml/mojom/coral_service.mojom-forward.h"
#include "odml/mojom/coral_service.mojom.h"
#include "odml/mojom/on_device_model.mojom.h"
#include "odml/mojom/on_device_model_service.mojom.h"

namespace coral {

namespace {
using mojom::CoralError;
using on_device_model::mojom::LoadModelResult;
using on_device_model::mojom::Session;

constexpr char kModelUuid[] = "ee7c31c2-18e5-405a-b54e-f2607130a15d";
// The duration to keep the model loaded (for upcoming feature triggers).
constexpr base::TimeDelta kUnloadModelInterval = base::Minutes(1);

std::string AppToPromptLine(const mojom::App& app) {
  return base::StringPrintf("app title: %s\n", app.title.c_str());
}

std::string TabToPromptLine(const mojom::Tab& tab) {
  return base::StringPrintf("tab title: %s, URL: %s\n", tab.title.c_str(),
                            tab.url->url.c_str());
}

std::string EntitiesToTitlePrompt(
    const std::vector<mojom::EntityPtr>& entities) {
  // TODO(b/361429962): Switch to the final prompt for the feature..
  std::string prompt =
      "Given the below tabs and apps, generate a suitable group title with "
      "less than 3 words. Output exactly the title and nothing else.\n";
  // TODO(b/361429962): Add mechanism to ensure prompt isn't too large
  // (truncation, omitting some entries, etc.).
  for (const mojom::EntityPtr& entity : entities) {
    if (entity->is_app()) {
      prompt += AppToPromptLine(*entity->get_app());
    } else if (entity->is_tab()) {
      prompt += TabToPromptLine(*entity->get_tab());
    }
  }
  return prompt;
}

mojom::EntityKeyPtr EntityToKey(const mojom::Entity& entity) {
  switch (entity.which()) {
    case mojom::Entity::Tag::kUnknown:
      return mojom::EntityKey::NewUnknown(false);
    case mojom::Entity::Tag::kTab:
      return mojom::EntityKey::NewTabUrl(entity.get_tab()->url->Clone());
    case mojom::Entity::Tag::kApp:
      return mojom::EntityKey::NewAppId(entity.get_app()->id);
  }
}

}  // namespace

TitleGenerationEngine::TitleGenerationEngine(
    raw_ref<on_device_model::mojom::OnDeviceModelPlatformService>
        on_device_model_service)
    : on_device_model_service_(on_device_model_service) {}

void TitleGenerationEngine::Process(mojom::GroupRequestPtr request,
                                    ClusteringResponse clustering_response,
                                    TitleGenerationCallback callback) {
  EnsureModelLoaded(base::BindOnce(
      &TitleGenerationEngine::DoProcess, weak_ptr_factory_.GetWeakPtr(),
      std::move(request), std::move(clustering_response), std::move(callback)));
}

void TitleGenerationEngine::EnsureModelLoaded(base::OnceClosure callback) {
  if (model_) {
    OnModelLoadResult(std::move(callback), LoadModelResult::kSuccess);
    return;
  }
  on_device_model_service_->LoadPlatformModel(
      base::Uuid::ParseLowercase(kModelUuid),
      model_.BindNewPipeAndPassReceiver(), mojo::NullRemote(),
      base::BindOnce(&TitleGenerationEngine::OnModelLoadResult,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void TitleGenerationEngine::OnModelLoadResult(base::OnceClosure callback,
                                              LoadModelResult result) {
  if (result != LoadModelResult::kSuccess) {
    // Unbind the model so we can use `model_.is_bound()` to check whether the
    // model is loaded.
    model_.reset();
    LOG(ERROR) << "Load model failed with result: " << static_cast<int>(result);
  }
  std::move(callback).Run();
  if (result == LoadModelResult::kSuccess) {
    base::Time unload_time = base::Time::Now() + kUnloadModelInterval;
    // This resets any existing scheduled `UnloadModel` which is expected (as
    // the model is used again and we should extend the unload timestamp).
    unload_model_timer_.Start(
        FROM_HERE, unload_time,
        base::BindOnce(&TitleGenerationEngine::UnloadModel,
                       weak_ptr_factory_.GetWeakPtr()));
  }
}

void TitleGenerationEngine::UnloadModel() {
  model_.reset();
}

void TitleGenerationEngine::DoProcess(mojom::GroupRequestPtr request,
                                      ClusteringResponse clustering_response,
                                      TitleGenerationCallback callback) {
  if (!model_) {
    std::move(callback).Run(std::move(request),
                            base::unexpected(CoralError::kLoadModelFailed));
    return;
  }
  auto session = SimpleSession::New();
  model_->StartSession(session->BindReceiver());
  if (!session->is_bound()) {
    std::move(callback).Run(std::move(request),
                            base::unexpected(CoralError::kLoadModelFailed));
    return;
  }
  std::vector<std::string> prompts;
  for (const Cluster& cluster : clustering_response.clusters) {
    prompts.push_back(EntitiesToTitlePrompt(cluster.entities));
  }
  ProcessEachPrompt(std::move(request), std::move(session),
                    std::move(clustering_response.clusters), std::move(prompts),
                    TitleGenerationResponse(), std::move(callback));
}

void TitleGenerationEngine::ProcessEachPrompt(
    mojom::GroupRequestPtr request,
    SimpleSession::Ptr session,
    std::vector<Cluster> clusters,
    std::vector<std::string> prompts,
    TitleGenerationResponse response,
    TitleGenerationCallback callback) {
  CHECK(session->is_bound());

  size_t index = response.groups.size();
  // > covers the index out-of-range case although it shouldn't happen.
  if (index >= prompts.size()) {
    std::move(callback).Run(std::move(request), std::move(response));
    return;
  }
  base::flat_map<std::string, std::string> fields{{"prompt", prompts[index]}};
  SimpleSession* session_ptr = session.get();
  auto on_model_output = base::BindOnce(
      &TitleGenerationEngine::OnModelOutput, weak_ptr_factory_.GetWeakPtr(),
      std::move(request), std::move(session), std::move(clusters),
      std::move(prompts), std::move(response), std::move(callback));
  auto execute_session = base::BindOnce(
      [](SimpleSession* session, base::OnceCallback<void(std::string)> callback,
         const std::optional<std::string>& formatted) {
        if (!formatted.has_value()) {
          std::move(callback).Run("");
          return;
        }
        auto input_options = on_device_model::mojom::InputOptions::New();
        input_options->text = *formatted;
        session->Execute(std::move(input_options), std::move(callback));
        return;
      },
      session_ptr, std::move(on_model_output));
  on_device_model_service_->FormatInput(
      base::Uuid::ParseLowercase(kModelUuid),
      on_device_model::mojom::FormatFeature::kPrompt, fields,
      std::move(execute_session));
}

void TitleGenerationEngine::OnModelOutput(mojom::GroupRequestPtr request,
                                          SimpleSession::Ptr session,
                                          std::vector<Cluster> clusters,
                                          std::vector<std::string> prompts,
                                          TitleGenerationResponse response,
                                          TitleGenerationCallback callback,
                                          std::string title) {
  size_t index = response.groups.size();
  CHECK(index < clusters.size());

  auto group = mojom::Group::New();
  // TODO(b/361429962): Figure out whether truncating should happen in here or
  // in UI.
  // TODO(b/361429962): Validate safety result of the title.
  group->title = std::move(title);
  for (const mojom::EntityPtr& entity : clusters[index].entities) {
    group->entities.push_back(EntityToKey(*entity));
  }
  response.groups.push_back(std::move(group));
  ProcessEachPrompt(std::move(request), std::move(session), std::move(clusters),
                    std::move(prompts), std::move(response),
                    std::move(callback));
}

}  // namespace coral
