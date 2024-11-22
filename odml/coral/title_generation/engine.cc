// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/coral/title_generation/engine.h"

#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <base/functional/bind.h>
#include <base/functional/callback.h>
#include <base/functional/callback_helpers.h>
#include <base/location.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/task/sequenced_task_runner.h>
#include <base/time/time.h>
#include <base/token.h>
#include <base/types/expected.h>
#include <base/uuid.h>
#include <mojo/public/cpp/bindings/callback_helpers.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "mojo/public/cpp/bindings/pending_remote.h"
#include "odml/coral/clustering/engine.h"
#include "odml/coral/common.h"
#include "odml/coral/title_generation/simple_session.h"
#include "odml/mojom/coral_service.mojom-forward.h"
#include "odml/mojom/coral_service.mojom.h"
#include "odml/mojom/on_device_model.mojom.h"
#include "odml/mojom/on_device_model_service.mojom.h"
#include "odml/session_state_manager/session_state_manager.h"

namespace coral {

namespace {
using mojom::CoralError;
using on_device_model::mojom::LoadModelResult;
using on_device_model::mojom::Session;

// Adaptation model for Coral.
constexpr char kModelUuid[] = "fa9a157a-696d-48c5-9e46-efa048743587";
// The duration to keep the model loaded (for upcoming feature triggers).
constexpr base::TimeDelta kUnloadModelInterval = base::Minutes(1);

// Ensures cache hits when user triggers feature in turn from 2 desktops both
// having 2 coral groups.
constexpr size_t kMaxCacheSize = 4;
// The acceptable threshold is set to 1 diff per 4 items.
constexpr double kMaxGroupDifferenceRatioToReuseTitle = 0.2501;

std::string AppToPromptLine(const mojom::App& app) {
  return base::StringPrintf("title: %s\n", app.title.c_str());
}

std::string TabToPromptLine(const mojom::Tab& tab) {
  return base::StringPrintf("title: %s\n", tab.title.c_str());
}

std::string EntitiesToTitlePrompt(
    const std::vector<mojom::EntityPtr>& entities) {
  std::string prompt = "Generate a title for this group:\n\n";
  // TODO(b/361429962): Add mechanism to ensure prompt isn't too large
  // (truncation, omitting some entries, etc.).
  for (const mojom::EntityPtr& entity : entities) {
    if (entity->is_app()) {
      prompt += AppToPromptLine(*entity->get_app());
    } else if (entity->is_tab()) {
      prompt += TabToPromptLine(*entity->get_tab());
    }
  }
  prompt += "\n";
  return prompt;
}

std::vector<mojom::EntityPtr> CloneEntities(
    const std::vector<mojom::EntityPtr>& entities) {
  std::vector<mojom::EntityPtr> ret;
  for (const mojom::EntityPtr& entity : entities) {
    ret.push_back(entity.Clone());
  }
  return ret;
}

std::string GetTitle(const mojom::EntityPtr& entity) {
  if (entity->is_tab()) {
    return entity->get_tab()->title;
  }
  if (entity->is_app()) {
    return entity->get_app()->title;
  }
  return {};
}

double GetDifferenceRatio(
    const std::vector<mojom::EntityPtr>& new_group,
    const std::unordered_multiset<std::string>& old_group) {
  // Shouldn't happen, but fail gracefully by return a value higher than
  // threshold.
  if (new_group.empty()) {
    return 1.0;
  }
  // Remove items from the `old_group`'s multiset for each match, then the total
  // of "items not found in multiset" and "remaining items in multiset" is the
  // difference of two groups. Copy the group because we need to modify it.
  std::unordered_multiset<std::string> old_group_copy(old_group);
  int mismatches = 0;
  for (const mojom::EntityPtr& entity : new_group) {
    auto it = old_group_copy.find(GetTitle(entity));
    if (it == old_group_copy.end()) {
      mismatches++;
      continue;
    }
    old_group_copy.erase(it);
  }
  mismatches += old_group_copy.size();
  return static_cast<double>(mismatches) / new_group.size();
}

}  // namespace

TitleGenerationEngine::TitleGenerationEngine(
    raw_ref<CoralMetrics> metrics,
    raw_ref<on_device_model::mojom::OnDeviceModelPlatformService>
        on_device_model_service,
    odml::SessionStateManagerInterface* session_state_manager)
    : metrics_(metrics),
      on_device_model_service_(on_device_model_service),
      title_cache_(kMaxCacheSize) {
  if (session_state_manager) {
    session_state_manager->AddObserver(this);
  }
}

void TitleGenerationEngine::PrepareResource() {
  if (is_processing_) {
    pending_callbacks_.push(
        base::BindOnce(&TitleGenerationEngine::PrepareResource,
                       weak_ptr_factory_.GetWeakPtr()));
    return;
  }
  is_processing_ = true;
  // Ensure `is_processing_` will always be reset no matter callback is run or
  // dropped.
  EnsureModelLoaded(mojo::WrapCallbackWithDefaultInvokeIfNotRun(
      base::BindOnce(&TitleGenerationEngine::OnProcessCompleted,
                     weak_ptr_factory_.GetWeakPtr())));
}

void TitleGenerationEngine::Process(
    mojom::GroupRequestPtr request,
    ClusteringResponse clustering_response,
    mojo::PendingRemote<mojom::TitleObserver> pending_observer,
    TitleGenerationCallback callback) {
  if (is_processing_) {
    pending_callbacks_.push(base::BindOnce(
        &TitleGenerationEngine::Process, weak_ptr_factory_.GetWeakPtr(),
        std::move(request), std::move(clustering_response),
        std::move(pending_observer), std::move(callback)));
    return;
  }
  is_processing_ = true;
  // Prepare the clusters along with their prompts.
  std::vector<GroupData> groups;
  for (Cluster& cluster : clustering_response.clusters) {
    GroupData group_data{
        .id = base::Token::CreateRandom(),
    };
    std::optional<std::string> title = MaybeGetCachedTitle(cluster.entities);
    // If the title is cached, set the title field directly. Otherwise, generate
    // the prompt and set the entities.
    if (title.has_value()) {
      group_data.title = std::move(title);
    } else {
      // TODO(b/361429962): Validate safety result of the group.
      group_data.prompt = EntitiesToTitlePrompt(cluster.entities);
    }
    group_data.entities = std::move(cluster.entities);
    groups.push_back(std::move(group_data));
  }
  mojo::Remote<mojom::TitleObserver> observer(std::move(pending_observer));
  // Ensure `is_processing_` will always be reset no matter callback is run or
  // dropped.
  base::OnceClosure on_process_completed =
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          base::BindOnce(&TitleGenerationEngine::OnProcessCompleted,
                         weak_ptr_factory_.GetWeakPtr()));
  auto timer = PerformanceTimer::Create();
  ProcessCallback on_complete;
  if (observer) {
    ReplyGroupsWithoutTitles(groups, std::move(callback));
    on_complete =
        base::BindOnce(&TitleGenerationEngine::OnAllTitleGenerationFinished,
                       weak_ptr_factory_.GetWeakPtr(), std::move(timer),
                       std::move(on_process_completed));

  } else {
    on_complete =
        base::BindOnce(&TitleGenerationEngine::ReplyGroupsWithTitles,
                       weak_ptr_factory_.GetWeakPtr(), std::move(timer),
                       std::move(callback), std::move(on_process_completed));
  }
  metrics_->SendTitleGenerationModelLoaded(model_.is_bound());
  EnsureModelLoaded(base::BindOnce(&TitleGenerationEngine::DoProcess,
                                   weak_ptr_factory_.GetWeakPtr(),
                                   std::move(request), std::move(observer),
                                   std::move(groups), std::move(on_complete)));
}

void TitleGenerationEngine::OnUserLoggedIn(
    const odml::SessionStateManagerInterface::User& user) {
  current_user_ = user;
}

void TitleGenerationEngine::OnUserLoggedOut() {
  current_user_.reset();
}

void TitleGenerationEngine::EnsureModelLoaded(base::OnceClosure callback) {
  if (model_) {
    // When the loaded model is used, reset the unload timer.
    SetUnloadModelTimer();
    std::move(callback).Run();
    return;
  }
  auto timer = PerformanceTimer::Create();
  on_device_model_service_->LoadPlatformModel(
      base::Uuid::ParseLowercase(kModelUuid),
      model_.BindNewPipeAndPassReceiver(), mojo::NullRemote(),
      base::BindOnce(&TitleGenerationEngine::OnModelLoadResult,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback),
                     std::move(timer)));
}

void TitleGenerationEngine::OnModelLoadResult(base::OnceClosure callback,
                                              PerformanceTimer::Ptr timer,
                                              LoadModelResult result) {
  if (result != LoadModelResult::kSuccess) {
    // Unbind the model because when load model fails we shouldn't be using the
    // model.
    model_.reset();
    LOG(ERROR) << "Load model failed with result: " << static_cast<int>(result);
  } else {
    // Only report model load latency on success.
    metrics_->SendLoadTitleGenerationModelLatency(timer->GetDuration());
    SetUnloadModelTimer();
  }
  std::move(callback).Run();
}

void TitleGenerationEngine::SetUnloadModelTimer() {
  base::Time unload_time = base::Time::Now() + kUnloadModelInterval;
  // This resets any existing scheduled `UnloadModel`.
  unload_model_timer_.Start(FROM_HERE, unload_time,
                            base::BindOnce(&TitleGenerationEngine::UnloadModel,
                                           weak_ptr_factory_.GetWeakPtr()));
}

void TitleGenerationEngine::UnloadModel() {
  model_.reset();
}

void TitleGenerationEngine::ReplyGroupsWithoutTitles(
    const std::vector<GroupData>& groups,
    TitleGenerationEngine::TitleGenerationCallback callback) {
  TitleGenerationResponse response;
  for (const GroupData& group_data : groups) {
    auto group = mojom::Group::New();
    group->id = group_data.id;
    group->title = group_data.title;
    group->entities = CloneEntities(group_data.entities);
    response.groups.push_back(std::move(group));
  }
  std::move(callback).Run(std::move(response));
}

void TitleGenerationEngine::ReplyGroupsWithTitles(
    PerformanceTimer::Ptr timer,
    TitleGenerationEngine::TitleGenerationCallback callback,
    base::OnceClosure on_complete,
    mojo::Remote<mojom::TitleObserver> observer,
    SimpleSession::Ptr session,
    std::vector<GroupData> groups,
    CoralResult<void> result) {
  ReportTitleGenerationMetrics(std::move(timer), result);
  TitleGenerationResponse response;
  if (!result.has_value()) {
    std::move(callback).Run(base::unexpected(result.error()));
    return;
  }
  for (const GroupData& group_data : groups) {
    auto group = mojom::Group::New();
    group->id = group_data.id;
    group->title = group_data.title;
    group->entities = CloneEntities(group_data.entities);
    response.groups.push_back(std::move(group));
  }
  std::move(callback).Run(std::move(response));
  CacheGroupTitles(groups);
  ReportPromptSizes(0, std::move(session), std::move(groups),
                    std::move(on_complete));
}

void TitleGenerationEngine::OnAllTitleGenerationFinished(
    PerformanceTimer::Ptr timer,
    base::OnceClosure on_complete,
    mojo::Remote<mojom::TitleObserver> observer,
    SimpleSession::Ptr session,
    std::vector<GroupData> groups,
    CoralResult<void> result) {
  ReportTitleGenerationMetrics(std::move(timer), result);
  if (result.has_value()) {
    // All titles should have been updated to the observer.
    CacheGroupTitles(groups);
    ReportPromptSizes(0, std::move(session), std::move(groups),
                      std::move(on_complete));
    return;
  }
  LOG(ERROR) << "Failed to generate titles with code: "
             << static_cast<int>(result.error());
  // Update the remaining groups to with empty title to the observer.
  for (const GroupData& group : groups) {
    if (group.title.has_value()) {
      continue;
    }
    observer->TitleUpdated(group.id, "");
  }
}

void TitleGenerationEngine::DoProcess(
    mojom::GroupRequestPtr request,
    mojo::Remote<mojom::TitleObserver> observer,
    std::vector<GroupData> groups,
    ProcessCallback callback) {
  if (!model_) {
    std::move(callback).Run(std::move(observer), nullptr, std::move(groups),
                            base::unexpected(CoralError::kLoadModelFailed));
    return;
  }

  auto session = SimpleSession::New();
  model_->StartSession(session->BindReceiver());
  if (!session->is_bound()) {
    std::move(callback).Run(std::move(observer), nullptr, std::move(groups),
                            base::unexpected(CoralError::kLoadModelFailed));
    return;
  }

  ProcessEachPrompt(0, std::move(request), std::move(session),
                    std::move(observer), std::move(groups),
                    std::move(callback));
}

void TitleGenerationEngine::ProcessEachPrompt(
    size_t index,
    mojom::GroupRequestPtr request,
    SimpleSession::Ptr session,
    mojo::Remote<mojom::TitleObserver> observer,
    std::vector<GroupData> groups,
    ProcessCallback callback) {
  CHECK(session->is_bound());

  // > covers the index out-of-range case although it shouldn't happen.
  if (index >= groups.size()) {
    std::move(callback).Run(std::move(observer), std::move(session),
                            std::move(groups), base::ok());
    return;
  }
  // Cached title is reused for this group, skip and process the next one.
  if (groups[index].title.has_value()) {
    ProcessEachPrompt(index + 1, std::move(request), std::move(session),
                      std::move(observer), std::move(groups),
                      std::move(callback));
    return;
  }
  base::flat_map<std::string, std::string> fields{
      {"prompt", groups[index].prompt}};
  SimpleSession* session_ptr = session.get();
  auto timer = PerformanceTimer::Create();
  auto on_model_output = base::BindOnce(
      &TitleGenerationEngine::OnModelOutput, weak_ptr_factory_.GetWeakPtr(),
      index, std::move(request), std::move(session), std::move(observer),
      std::move(groups), std::move(callback), std::move(timer));
  auto execute_session = base::BindOnce(
      [](SimpleSession* session, base::OnceCallback<void(std::string)> callback,
         const std::optional<std::string>& formatted) {
        if (!formatted.has_value()) {
          std::move(callback).Run("");
          return;
        }
        auto input_options = on_device_model::mojom::InputOptions::New();
        auto input = on_device_model::mojom::Input::New();
        input->pieces.push_back(*formatted);
        input_options->input = std::move(input);
        session->Execute(std::move(input_options), std::move(callback));
        return;
      },
      session_ptr, std::move(on_model_output));
  on_device_model_service_->FormatInput(
      base::Uuid::ParseLowercase(kModelUuid),
      on_device_model::mojom::FormatFeature::kPrompt, fields,
      std::move(execute_session));
}

void TitleGenerationEngine::OnModelOutput(
    size_t index,
    mojom::GroupRequestPtr request,
    SimpleSession::Ptr session,
    mojo::Remote<mojom::TitleObserver> observer,
    std::vector<GroupData> groups,
    ProcessCallback callback,
    PerformanceTimer::Ptr timer,
    std::string title) {
  CHECK(index < groups.size());
  // The model outputs a leading blank space by default. In any case, trimming
  // blank space from both ends makes the title format on UI more consistent.
  title = base::TrimWhitespaceASCII(title, base::TRIM_ALL);

  // Send metrics for this title generation result.
  metrics_->SendTitleGenerationResult(
      title.empty() ? metrics::TitleGenerationResult::kEmptyModelOutput
                    : metrics::TitleGenerationResult::kSuccess);
  if (!title.empty()) {
    metrics_->SendGenerateTitleLatency(timer->GetDuration());
    metrics_->SendTitleLengthInCharacters(title.size());
    // "length in words" in this metric is defined by number of white spaces
    // + 1. This quite accurately represents number of words in English titles.
    metrics_->SendTitleLengthInWords(std::ranges::count(title, ' ') + 1);
  }

  // TODO(b/361429962): Figure out whether truncating should happen in here or
  // in UI.
  // TODO(b/361429962): Validate safety result of the title.
  CHECK(!groups[index].title.has_value());
  groups[index].title = std::move(title);
  if (observer) {
    observer->TitleUpdated(groups[index].id, *groups[index].title);
  }
  ProcessEachPrompt(index + 1, std::move(request), std::move(session),
                    std::move(observer), std::move(groups),
                    std::move(callback));
}

void TitleGenerationEngine::ReportTitleGenerationMetrics(
    PerformanceTimer::Ptr timer, CoralStatus status) {
  metrics_->SendTitleGenerationEngineStatus(status);
  if (status.has_value()) {
    metrics_->SendTitleGenerationEngineLatency(timer->GetDuration());
  }
}

void TitleGenerationEngine::OnProcessCompleted() {
  is_processing_ = false;
  if (pending_callbacks_.empty()) {
    return;
  }
  base::OnceClosure callback = std::move(pending_callbacks_.front());
  pending_callbacks_.pop();
  std::move(callback).Run();
}

void TitleGenerationEngine::CacheGroupTitles(
    const std::vector<GroupData>& groups) {
  // Title cache needs to be bound to a specific user.
  if (!current_user_.has_value()) {
    return;
  }
  for (const GroupData& group_data : groups) {
    if (!group_data.title.has_value()) {
      continue;
    }
    std::unordered_multiset<std::string> entity_titles;
    for (const mojom::EntityPtr& entity : group_data.entities) {
      entity_titles.insert(GetTitle(entity));
    }
    title_cache_.Put(*group_data.title,
                     TitleCacheEntry{
                         .entity_titles = std::move(entity_titles),
                         .user = *current_user_,
                     });
  }
}

std::optional<std::string> TitleGenerationEngine::MaybeGetCachedTitle(
    const std::vector<mojom::EntityPtr>& entities) {
  std::optional<std::string> ret;
  float min_difference = 1.0;
  for (const auto& [title, title_cache_entry] : title_cache_) {
    if (current_user_ != title_cache_entry.user) {
      continue;
    }
    float difference =
        GetDifferenceRatio(entities, title_cache_entry.entity_titles);
    if (difference < min_difference) {
      min_difference = difference;
      if (difference < kMaxGroupDifferenceRatioToReuseTitle) {
        ret = title;
      }
    }
  }
  metrics_->SendTitleCacheDifferenceRatio(min_difference);
  metrics_->SendTitleCacheHit(ret.has_value());
  return ret;
}

// TODO(b/353900545): We can use the token sizes from ResponseSummary result
// after we sync with Chrome and after APU backend supports calculating token
// during Execute as well.
void TitleGenerationEngine::ReportPromptSizes(size_t index,
                                              SimpleSession::Ptr session,
                                              std::vector<GroupData> groups,
                                              base::OnceClosure on_complete) {
  CHECK(session->is_bound());

  // > covers the index out-of-range case although it shouldn't happen.
  if (index >= groups.size()) {
    std::move(on_complete).Run();
    return;
  }

  // Cached title is reused for this group.
  if (groups[index].prompt.empty()) {
    ReportPromptSizes(index + 1, std::move(session), std::move(groups),
                      std::move(on_complete));
    return;
  }

  session->SizeInTokens(
      groups[index].prompt,
      base::BindOnce(&TitleGenerationEngine::OnSizeInTokensResponse,
                     weak_ptr_factory_.GetWeakPtr(), index, std::move(session),
                     std::move(groups), std::move(on_complete)));
}

void TitleGenerationEngine::OnSizeInTokensResponse(
    size_t index,
    SimpleSession::Ptr session,
    std::vector<GroupData> groups,
    base::OnceClosure on_complete,
    uint32_t size_in_tokens) {
  CHECK(index < groups.size());
  metrics_->SendTitleInputTokenSize(size_in_tokens);
  ReportPromptSizes(index + 1, std::move(session), std::move(groups),
                    std::move(on_complete));
}

}  // namespace coral
