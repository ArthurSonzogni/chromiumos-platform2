// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/coral/title_generation/engine.h"

#include <algorithm>
#include <memory>
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
#include "odml/i18n/translator.h"
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

// Ensures cache hits when user triggers feature in turn from 2 desktops both
// having 2 coral groups.
// If this is adjusted, remember to adjust the kTitleDatabaseDailyWrittenSize
// metrics to prevent overflow.
constexpr size_t kMaxCacheSize = 4;
// The acceptable threshold is set to 1 diff per 4 items.
constexpr double kMaxGroupDifferenceRatioToReuseTitle = 0.2501;

// TODO(b/378632983): We want to reserve 50 tokens for output, while the max
// input token length should be 1024, so this should actually be 1024-50. But
// there is a bug in APU backend currently that makes the maximum supported
// token size 128 less, so this is 1024-128-50 for now.
constexpr size_t kMaxInputSizeInTokens = 846;

constexpr base::TimeDelta kTitleCacheFlushStartingDelay = base::Minutes(10);
constexpr base::TimeDelta kTitleCacheFlushRepeatingDelay = base::Hours(1);

std::string TitlesToPrompt(const std::vector<std::string>& titles) {
  std::string prompt = "Generate a title for this group:\n\n";
  for (const std::string& title : titles) {
    prompt += base::StringPrintf("title: %s\n", title.c_str());
  }
  prompt += "\n";
  return prompt;
}

std::vector<mojom::EntityPtr> CloneEntities(
    const std::vector<EntityWithMetadata>& entities) {
  std::vector<mojom::EntityPtr> ret;
  for (const EntityWithMetadata& entity : entities) {
    ret.push_back(entity.entity.Clone());
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
    const std::vector<EntityWithMetadata>& new_group,
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
  for (const EntityWithMetadata& entity : new_group) {
    auto it = old_group_copy.find(GetTitle(entity.entity));
    if (it == old_group_copy.end()) {
      mismatches++;
      continue;
    }
    old_group_copy.erase(it);
  }
  mismatches += old_group_copy.size();
  return static_cast<double>(mismatches) / new_group.size();
}

// nullopt for doesn't need translation.
std::optional<std::string> GetTranslationSource(
    const LanguageDetectionResult& language_detection_result,
    const std::string& target_locale) {
  constexpr size_t kTopLanguageResultEntriesToCheck = 3;
  // Doesn't need translation if it's English or the target locale already.
  for (int i = 0; i < std::min(language_detection_result.size(),
                               kTopLanguageResultEntriesToCheck);
       i++) {
    const on_device_model::LanguageDetector::TextLanguage& language =
        language_detection_result[i];
    if (language.locale == "en" || language.locale == target_locale) {
      return std::nullopt;
    }
  }

  for (int i = 0; i < std::min(language_detection_result.size(),
                               kTopLanguageResultEntriesToCheck);
       i++) {
    const on_device_model::LanguageDetector::TextLanguage& language =
        language_detection_result[i];
    if (IsLanguageSupported(language.locale)) {
      return language.locale;
    }
  }

  // It shouldn't really reach here because we already verified that a supported
  // language can be found within the language detection result in the embedding
  // engine. Leave it untranslated in this edge case.
  return std::nullopt;
}

}  // namespace

TitleGenerationEngine::TitleGenerationEngine(
    raw_ref<CoralMetrics> metrics,
    raw_ref<on_device_model::mojom::OnDeviceModelPlatformService>
        on_device_model_service,
    odml::SessionStateManagerInterface* session_state_manager,
    raw_ref<i18n::Translator> translator,
    std::unique_ptr<TitleCacheStorageInterface> title_cache_storage)
    : metrics_(metrics),
      on_device_model_service_(on_device_model_service),
      translator_(translator),
      title_cache_(kMaxCacheSize),
      title_cache_storage_(std::move(title_cache_storage)) {
  // cache_flush_timer_ is initialized here because it needs the
  // weak_ptr_factory_.
  cache_flush_timer_ = std::make_unique<DelayedRepeatingTimer>(
      kTitleCacheFlushStartingDelay, kTitleCacheFlushRepeatingDelay,
      base::BindRepeating(&TitleGenerationEngine::OnFlushCacheTimer,
                          weak_ptr_factory_.GetWeakPtr()));
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
  auto on_process_complete = mojo::WrapCallbackWithDefaultInvokeIfNotRun(
      base::BindOnce(&TitleGenerationEngine::OnProcessCompleted,
                     weak_ptr_factory_.GetWeakPtr()));
  EnsureTranslatorInitialized(base::BindOnce(
      &TitleGenerationEngine::GetModelState, weak_ptr_factory_.GetWeakPtr(),
      std::move(on_process_complete)));
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
  bool has_group_without_title = false;
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
      has_group_without_title = true;
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
  auto timer = odml::PerformanceTimer::Create();
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
  // Doesn't need to generate any title, so execute the complete callback
  // directly.
  if (!has_group_without_title) {
    std::move(on_complete)
        .Run(std::move(observer), std::move(groups), base::ok());
    return;
  }
  metrics_->SendTitleGenerationModelLoaded(model_.is_bound());
  base::OnceClosure do_process = base::BindOnce(
      &TitleGenerationEngine::DoProcess, weak_ptr_factory_.GetWeakPtr(),
      std::move(request), std::move(observer), std::move(groups),
      std::move(on_complete));
  EnsureTranslatorInitialized(
      base::BindOnce(&TitleGenerationEngine::EnsureModelLoaded,
                     weak_ptr_factory_.GetWeakPtr(), std::move(do_process)));
}

void TitleGenerationEngine::OnUserLoggedIn(
    const odml::SessionStateManagerInterface::User& user) {
  current_user_ = user;
  title_cache_storage_->Load(user, title_cache_);
  cache_flush_timer_->Start();
  title_cache_dirty_ = false;
}

void TitleGenerationEngine::OnUserLoggedOut() {
  cache_flush_timer_->Stop();
  if (current_user_.has_value()) {
    title_cache_dirty_ |=
        title_cache_storage_->FilterForExpiration(title_cache_);
  }
  if (current_user_.has_value() && title_cache_dirty_) {
    title_cache_storage_->FilterForExpiration(title_cache_);
    title_cache_storage_->Save(current_user_.value(), title_cache_);
  }
  current_user_.reset();
  title_cache_.Clear();
  title_cache_dirty_ = false;
}

void TitleGenerationEngine::OnGetModelStateResult(
    base::OnceClosure callback,
    on_device_model::mojom::PlatformModelState state) {
  // If it's not already installed on disk, we load the model to ensure it's
  // installed. This is a workaround due to that currently there's no API to
  // only install the model.
  if (state != on_device_model::mojom::PlatformModelState::kInstalledOnDisk) {
    LOG(ERROR) << "Model state: " << static_cast<int>(state);
    EnsureModelLoaded(std::move(callback));
    return;
  }
  // Else, the model should be installed on disk.
  std::move(callback).Run();
}

void TitleGenerationEngine::EnsureTranslatorInitialized(
    base::OnceClosure callback) {
  if (translator_->IsAvailable()) {
    std::move(callback).Run();
    return;
  }
  translator_->Initialize(base::BindOnce([](bool success) {
                            if (!success) {
                              LOG(ERROR) << "Load translator failed";
                            }
                          }).Then(std::move(callback)));
}

void TitleGenerationEngine::GetModelState(base::OnceClosure callback) {
  on_device_model_service_->GetPlatformModelState(
      base::Uuid::ParseLowercase(kModelUuid),
      base::BindOnce(&TitleGenerationEngine::OnGetModelStateResult,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void TitleGenerationEngine::EnsureModelLoaded(base::OnceClosure callback) {
  if (model_) {
    std::move(callback).Run();
    return;
  }
  auto timer = odml::PerformanceTimer::Create();
  on_device_model_service_->LoadPlatformModel(
      base::Uuid::ParseLowercase(kModelUuid),
      model_.BindNewPipeAndPassReceiver(), mojo::NullRemote(),
      base::BindOnce(&TitleGenerationEngine::OnModelLoadResult,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback),
                     std::move(timer)));
}

void TitleGenerationEngine::OnModelLoadResult(base::OnceClosure callback,
                                              odml::PerformanceTimer::Ptr timer,
                                              LoadModelResult result) {
  if (result != LoadModelResult::kSuccess) {
    // Unbind the model because when load model fails we shouldn't be using
    // the model.
    model_.reset();
    LOG(ERROR) << "Load model failed with result: " << static_cast<int>(result);
  } else {
    // Only report model load latency on success.
    metrics_->SendLoadTitleGenerationModelLatency(timer->GetDuration());
  }
  std::move(callback).Run();
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
    // Title could still be present already due to cached results.
    group->title = group_data.title;
    group->entities = CloneEntities(group_data.entities);
    response.groups.push_back(std::move(group));
  }
  std::move(callback).Run(std::move(response));
}

void TitleGenerationEngine::ReplyGroupsWithTitles(
    odml::PerformanceTimer::Ptr timer,
    TitleGenerationEngine::TitleGenerationCallback callback,
    base::OnceClosure on_complete,
    mojo::Remote<mojom::TitleObserver> observer,
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
}

void TitleGenerationEngine::OnAllTitleGenerationFinished(
    odml::PerformanceTimer::Ptr timer,
    base::OnceClosure on_complete,
    mojo::Remote<mojom::TitleObserver> observer,
    std::vector<GroupData> groups,
    CoralResult<void> result) {
  ReportTitleGenerationMetrics(std::move(timer), result);
  if (result.has_value()) {
    // All titles should have been updated to the observer.
    CacheGroupTitles(groups);
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
    std::move(callback).Run(std::move(observer), std::move(groups),
                            base::unexpected(CoralError::kLoadModelFailed));
    return;
  }

  auto session = SimpleSession::New();
  model_->StartSession(session->BindReceiver());
  if (!session->is_bound()) {
    std::move(callback).Run(std::move(observer), std::move(groups),
                            base::unexpected(CoralError::kLoadModelFailed));
    return;
  }

  ProcessEachPrompt(ProcessParams{.index = 0,
                                  .request = std::move(request),
                                  .session = std::move(session),
                                  .observer = std::move(observer),
                                  .groups = std::move(groups),
                                  .callback = std::move(callback)});
}

void TitleGenerationEngine::ProcessEachPrompt(ProcessParams params) {
  size_t index = params.index;

  // > covers the index out-of-range case although it shouldn't happen.
  if (index >= params.groups.size()) {
    std::move(params.callback)
        .Run(std::move(params.observer), std::move(params.groups), base::ok());
    return;
  }
  // Cached title is reused for this group, skip and process the next one.
  if (params.groups[index].title.has_value()) {
    params.index++;
    ProcessEachPrompt(std::move(params));
    return;
  }
  EntitiesToMaybeTranslatedTitles(std::move(params),
                                  odml::PerformanceTimer::Create(), {});
}

void TitleGenerationEngine::EntitiesToMaybeTranslatedTitles(
    ProcessParams params,
    odml::PerformanceTimer::Ptr timer,
    std::vector<std::string> titles) {
  size_t index = params.index;
  CHECK(index < params.groups.size());
  size_t entity_index = titles.size();

  if (entity_index >= params.groups[index].entities.size()) {
    base::flat_map<std::string, std::string> fields{
        {"prompt", TitlesToPrompt(titles)}};
    on_device_model_service_->FormatInput(
        base::Uuid::ParseLowercase(kModelUuid),
        on_device_model::mojom::FormatFeature::kPrompt, fields,
        base::BindOnce(&TitleGenerationEngine::OnFormatInputResponse,
                       weak_ptr_factory_.GetWeakPtr(), std::move(params),
                       std::move(timer)));
    return;
  }

  const EntityWithMetadata& entity =
      params.groups[index].entities[entity_index];
  // TODO(b/399282851): Support other locales.
  std::string target_locale("en");

  std::optional<std::string> translation_source =
      GetTranslationSource(entity.language_result, target_locale);
  if (!translation_source.has_value()) {
    titles.push_back(GetTitle(entity.entity));
    EntitiesToMaybeTranslatedTitles(std::move(params), std::move(timer),
                                    std::move(titles));
    return;
  }
  i18n::LangPair lang_pair{*translation_source, "en"};
  translator_->Translate(
      lang_pair, GetTitle(entity.entity),
      base::BindOnce(&TitleGenerationEngine::OnTranslateResult,
                     weak_ptr_factory_.GetWeakPtr(), std::move(params),
                     std::move(timer), std::move(titles)));
}

void TitleGenerationEngine::OnTranslateResult(
    ProcessParams params,
    odml::PerformanceTimer::Ptr timer,
    std::vector<std::string> titles,
    std::optional<std::string> translated) {
  // Can't form the prompt if any title is missing. Output empty title for this
  // group.
  if (!translated.has_value()) {
    OnModelOutput(std::move(params), std::move(timer), "");
    return;
  }
  titles.push_back(std::move(*translated));
  EntitiesToMaybeTranslatedTitles(std::move(params), std::move(timer),
                                  std::move(titles));
}

void TitleGenerationEngine::OnFormatInputResponse(
    ProcessParams params,
    odml::PerformanceTimer::Ptr timer,
    const std::optional<std::string>& formatted) {
  CHECK(params.session->is_bound());
  if (!formatted.has_value()) {
    OnModelOutput(std::move(params), std::move(timer), "");
    return;
  }
  SimpleSession* session_ptr = params.session.get();
  session_ptr->SizeInTokens(
      *formatted,
      base::BindOnce(&TitleGenerationEngine::OnSizeInTokensResponse,
                     weak_ptr_factory_.GetWeakPtr(), std::move(params),
                     std::move(timer), *formatted));
  return;
}

void TitleGenerationEngine::OnSizeInTokensResponse(
    ProcessParams params,
    odml::PerformanceTimer::Ptr timer,
    std::string prompt,
    uint32_t size_in_tokens) {
  CHECK(params.session->is_bound());
  metrics_->SendTitleInputTokenSize(size_in_tokens);
  if (size_in_tokens > kMaxInputSizeInTokens) {
    LOG(WARNING) << "Input prompt is too long.";
    OnModelOutput(std::move(params), std::move(timer), "");
    return;
  }
  auto input_options = on_device_model::mojom::AppendOptions::New();
  auto input = on_device_model::mojom::Input::New();
  input->pieces.push_back(std::move(prompt));
  input_options->input = std::move(input);
  SimpleSession* session_ptr = params.session.get();
  session_ptr->Execute(std::move(input_options),
                       base::BindOnce(&TitleGenerationEngine::OnModelOutput,
                                      weak_ptr_factory_.GetWeakPtr(),
                                      std::move(params), std::move(timer)));
}

void TitleGenerationEngine::OnModelOutput(ProcessParams params,
                                          odml::PerformanceTimer::Ptr timer,
                                          std::string title) {
  size_t index = params.index;
  CHECK(index < params.groups.size());
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
  CHECK(!params.groups[index].title.has_value());
  params.groups[index].title = std::move(title);
  if (params.observer) {
    params.observer->TitleUpdated(params.groups[index].id,
                                  *params.groups[index].title);
  }
  params.index++;
  ProcessEachPrompt(std::move(params));
}

void TitleGenerationEngine::ReportTitleGenerationMetrics(
    odml::PerformanceTimer::Ptr timer, CoralStatus status) {
  metrics_->SendTitleGenerationEngineStatus(status);
  if (status.has_value()) {
    metrics_->SendTitleGenerationEngineLatency(timer->GetDuration());
  }
}

void TitleGenerationEngine::OnProcessCompleted() {
  is_processing_ = false;
  if (pending_callbacks_.empty()) {
    UnloadModel();
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
    for (const EntityWithMetadata& entity : group_data.entities) {
      entity_titles.insert(GetTitle(entity.entity));
    }
    title_cache_.Put(
        *group_data.title,
        TitleCacheEntry{
            .entity_titles = std::move(entity_titles),
            .last_updated =
                base::Time::Now().InMillisecondsFSinceUnixEpochIgnoringNull()});
    title_cache_dirty_ = true;
  }
}

std::optional<std::string> TitleGenerationEngine::MaybeGetCachedTitle(
    const std::vector<EntityWithMetadata>& entities) {
  std::optional<std::string> ret;
  float min_difference = 1.0;
  for (const auto& [title, title_cache_entry] : title_cache_) {
    float difference =
        GetDifferenceRatio(entities, title_cache_entry.entity_titles);
    if (difference < min_difference) {
      min_difference = difference;
      if (difference < kMaxGroupDifferenceRatioToReuseTitle) {
        ret = title;
      }
    }
  }
  // If there's a cache hit, the entry is moved to the front later on through
  // CacheGroupTitles(), which is called regardless of whether there's a cache
  // hit.
  metrics_->SendTitleCacheDifferenceRatio(min_difference);
  metrics_->SendTitleCacheHit(ret.has_value());
  return ret;
}

std::optional<std::string> TitleGenerationEngine::GetNthTitleCacheKeyForTesting(
    int n) {
  auto itr = title_cache_.begin();
  if (n >= title_cache_.size()) {
    return std::nullopt;
  }
  // HashingLRUCache has bidirectional iterator not random access iterator, so
  // we'll need to advance it to nth position like that.
  for (int i = 0; i < n; i++) {
    CHECK(itr != title_cache_.end());
    itr++;
  }
  return itr->first;
}

void TitleGenerationEngine::OnFlushCacheTimer() {
  if (current_user_.has_value()) {
    title_cache_dirty_ |=
        title_cache_storage_->FilterForExpiration(title_cache_);
  }

  if (current_user_.has_value() && title_cache_dirty_) {
    title_cache_storage_->Save(current_user_.value(), title_cache_);
    title_cache_dirty_ = false;
  }
}

}  // namespace coral
