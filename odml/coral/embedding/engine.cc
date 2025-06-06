// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/coral/embedding/engine.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <base/functional/bind.h>
#include <base/functional/callback.h>
#include <base/functional/callback_helpers.h>
#include <base/hash/hash.h>
#include <base/strings/stringprintf.h>
#include <base/types/expected.h>
#include <mojo/public/cpp/bindings/callback_helpers.h>

#include "odml/coral/common.h"
#include "odml/coral/metrics.h"
#include "odml/i18n/translator.h"
#include "odml/mojom/coral_service.mojom.h"
#include "odml/mojom/embedding_model.mojom.h"
#include "odml/mojom/on_device_model.mojom.h"

namespace coral {

namespace {
using embedding_model::mojom::OnDeviceEmbeddingModelInferenceError;
using mojom::CoralError;
using on_device_model::mojom::LoadModelResult;

// The English locale.
constexpr char kEnglish[] = "en";

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

std::string GetTitleFromEntity(const mojom::Entity& entity) {
  if (entity.is_app()) {
    return entity.get_app()->title;
  } else if (entity.is_tab()) {
    return entity.get_tab()->title;
  }
  return "";
}

// We don't want to send some metrics for Process requests triggered by
// CacheEmbedding. This is because for we want to analyze most of this
// engine's metrics (like cache hits) only for Group requests. CacheEmbedding
// operation sends metrics too in service.cc, and since CacheEmbedding only
// passes through this engine, there is no need to send some metrics for it
// here again.
bool IsFullGroupRequest(const mojom::GroupRequestPtr& request) {
  // The hacky but easiest way to determine whether the request is a
  // CacheEmbeddings request for now is to check whether the clustering_options
  // (or title_generation_options) is null.
  return !request->clustering_options.is_null();
}

bool CheckIfLanguageSupported(
    const LanguageDetectionResult& language_detection_result) {
  // Current logic is to accept the result if any language code in the TOP3
  // classification result is supported.
  constexpr size_t kTopLanguageResultEntriesToCheck = 3;
  size_t entries_to_check = std::min(language_detection_result.size(),
                                     kTopLanguageResultEntriesToCheck);
  for (int i = 0; i < entries_to_check; i++) {
    const on_device_model::LanguageDetector::TextLanguage& language =
        language_detection_result[i];
    if (IsLanguageSupported(language.locale)) {
      return true;
    }
  }
  return false;
}

// nullopt for doesn't need translation.
std::optional<std::string> GetTranslationSource(
    const LanguageDetectionResult& language_detection_result,
    const std::optional<std::string>& target_locale) {
  constexpr size_t kTopLanguageResultEntriesToCheck = 3;
  size_t entries_to_check = std::min(language_detection_result.size(),
                                     kTopLanguageResultEntriesToCheck);
  // Doesn't need translation if it's English or the target locale already.
  for (int i = 0; i < entries_to_check; i++) {
    const on_device_model::LanguageDetector::TextLanguage& language =
        language_detection_result[i];
    if (language.locale == kEnglish || target_locale == language.locale) {
      return std::nullopt;
    }
  }

  for (int i = 0; i < entries_to_check; i++) {
    const on_device_model::LanguageDetector::TextLanguage& language =
        language_detection_result[i];
    if (IsLanguageSupported(language.locale)) {
      return language.locale;
    }
  }
  return std::nullopt;
}

// nullopt for doesn't need translation. Since
// IsLanguageSupportedBySafetyModel(kEnglish) is true, this function could never
// return kEnglish.
std::optional<std::string> GetSafetyTranslationSource(
    const LanguageDetectionResult& language_detection_result) {
  constexpr size_t kTopLanguageResultEntriesToCheck = 3;
  size_t entries_to_check = std::min(language_detection_result.size(),
                                     kTopLanguageResultEntriesToCheck);
  // Doesn't need translation if it's a supported language by safety model.
  for (int i = 0; i < entries_to_check; i++) {
    const on_device_model::LanguageDetector::TextLanguage& language =
        language_detection_result[i];
    if (IsLanguageSupportedBySafetyModel(language.locale)) {
      return std::nullopt;
    }
  }

  // Otherwise, return the first supported language.
  for (int i = 0; i < entries_to_check; i++) {
    const on_device_model::LanguageDetector::TextLanguage& language =
        language_detection_result[i];
    if (IsLanguageSupported(language.locale)) {
      return language.locale;
    }
  }

  // Shouldn't really reach here, but if does, returning nullopt and not
  // translating as fallback.
  return std::nullopt;
}

}  // namespace

namespace internal {

// TODO(b/361429567): Switch to the final prompt for the feature.
std::string EntityToEmbeddingPrompt(const mojom::Entity& entity) {
  if (entity.is_app()) {
    return base::StringPrintf("A page with title: \"%s\"",
                              entity.get_app()->title.c_str());
  } else if (entity.is_tab()) {
    const mojom::Tab& tab = *entity.get_tab();
    return base::StringPrintf("A page with title: \"%s\" and URL: \"%s\"",
                              tab.title.c_str(), tab.url->url.c_str());
  }
  return "";
}

// TODO(b/379964585): Switch to the final prompt for the feature.
std::string EntityToTitle(const mojom::Entity& entity) {
  if (entity.is_app()) {
    return entity.get_app()->title;
  } else if (entity.is_tab()) {
    return entity.get_tab()->title;
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
    raw_ref<cros_safety::SafetyServiceManager> safety_service_manager,
    std::unique_ptr<EmbeddingDatabaseFactory> embedding_database_factory,
    odml::SessionStateManagerInterface* session_state_manager,
    raw_ref<on_device_model::LanguageDetector> language_detector,
    raw_ref<i18n::Translator> translator)
    : metrics_(metrics),
      embedding_service_(embedding_service),
      safety_service_manager_(safety_service_manager),
      embedding_database_factory_(std::move(embedding_database_factory)),
      language_detector_(language_detector),
      translator_(translator) {
  if (session_state_manager) {
    session_state_manager->AddObserver(this);
  }
}

void EmbeddingEngine::PrepareResource(
    std::optional<std::string> language_code) {
  if (is_processing_) {
    pending_callbacks_.push(base::BindOnce(&EmbeddingEngine::PrepareResource,
                                           weak_ptr_factory_.GetWeakPtr(),
                                           std::move(language_code)));
    return;
  }
  is_processing_ = true;
  default_locale_ = std::move(language_code);
  // Ensure `is_processing_` will always be reset no matter callback is run or
  // dropped.
  EnsureModelLoaded(mojo::WrapCallbackWithDefaultInvokeIfNotRun(base::BindOnce(
      &EmbeddingEngine::OnProcessCompleted, weak_ptr_factory_.GetWeakPtr())));
}

void EmbeddingEngine::Process(mojom::GroupRequestPtr request,
                              EmbeddingCallback callback) {
  if (!language_detector_->IsAvailable()) {
    std::move(callback).Run(std::move(request),
                            base::unexpected(CoralError::kLoadModelFailed));
    return;
  }
  if (is_processing_) {
    pending_callbacks_.push(base::BindOnce(
        &EmbeddingEngine::Process, weak_ptr_factory_.GetWeakPtr(),
        std::move(request), std::move(callback)));
    return;
  }
  is_processing_ = true;

  auto timer = odml::PerformanceTimer::Create();
  EmbeddingCallback wrapped_callback = base::BindOnce(
      &EmbeddingEngine::HandleProcessResult, weak_ptr_factory_.GetWeakPtr(),
      std::move(callback), std::move(timer));
  // Ensure `is_processing_` will always be reset no matter callback is run or
  // dropped.
  base::OnceClosure on_process_completed =
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          base::BindOnce(&EmbeddingEngine::OnProcessCompleted,
                         weak_ptr_factory_.GetWeakPtr()));
  if (IsFullGroupRequest(request)) {
    metrics_->SendEmbeddingModelLoaded(model_.is_bound());
  }
  EnsureModelLoaded(base::BindOnce(
      &EmbeddingEngine::DoProcess, weak_ptr_factory_.GetWeakPtr(),
      std::move(request),
      std::move(wrapped_callback).Then(std::move(on_process_completed))));
}

void EmbeddingEngine::OnUserLoggedIn(
    const odml::SessionStateManagerInterface::User& user) {
  LOG(INFO) << "EmbeddingEngine::OnUserLoggedIn";
  embedding_database_ = embedding_database_factory_->Create(
      metrics_,
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
  auto timer = odml::PerformanceTimer::Create();
  embedding_service_->LoadEmbeddingModel(
      base::Uuid::ParseLowercase(kModelUuid),
      model_.BindNewPipeAndPassReceiver(), mojo::NullRemote(),
      base::BindOnce(&EmbeddingEngine::OnModelLoadResult,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback),
                     std::move(timer)));
}

void EmbeddingEngine::OnModelLoadResult(base::OnceClosure callback,
                                        odml::PerformanceTimer::Ptr timer,
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
  ProcessEachPrompt(ProcessingParams{
      .request = std::move(request),
      .prompts = std::move(prompts),
      .response = EmbeddingResponse(),
      .callback = std::move(callback),
  });
}

void EmbeddingEngine::ProcessEachPrompt(ProcessingParams params) {
  size_t index = params.response.embeddings.size();
  // > covers the index out-of-range case although it shouldn't happen.
  if (index >= params.prompts.size()) {
    std::move(params.callback)
        .Run(std::move(params.request), std::move(params.response));
    return;
  }

  EmbeddingEntry entry = GetEmbeddingEntry(*params.request->entities[index],
                                           params.prompts[index]);
  CheckLanguage(std::move(params), std::move(entry));
}

void EmbeddingEngine::CheckLanguage(ProcessingParams params,
                                    EmbeddingEntry entry) {
  size_t index = params.response.embeddings.size();
  if (entry.languages.has_value()) {
    if (IsFullGroupRequest(params.request)) {
      metrics_->SendLanguageDetectionCacheHit(true);
    }
    CheckLanguageResult(std::move(params), std::move(entry));
    return;
  }

  if (IsFullGroupRequest(params.request)) {
    metrics_->SendLanguageDetectionCacheHit(false);
  }

  if (!language_detector_->IsAvailable()) {
    CheckLanguageResult(std::move(params), std::move(entry));
    return;
  }
  language_detector_->Classify(
      GetTitleFromEntity(*params.request->entities[index]),
      base::BindOnce(&EmbeddingEngine::OnLanguageDetectionResult,
                     weak_ptr_factory_.GetWeakPtr(), std::move(params),
                     std::move(entry)));
}

void EmbeddingEngine::OnLanguageDetectionResult(
    ProcessingParams params,
    EmbeddingEntry entry,
    std::optional<std::vector<on_device_model::LanguageDetector::TextLanguage>>
        text_languages) {
  size_t index = params.response.embeddings.size();
  if (text_languages.has_value()) {
    entry.languages = std::move(*text_languages);
    PutEmbeddingEntry(*params.request->entities[index], params.prompts[index],
                      entry);
  }
  CheckLanguageResult(std::move(params), std::move(entry));
}

void EmbeddingEngine::CheckLanguageResult(ProcessingParams params,
                                          EmbeddingEntry entry) {
  // Doesn't have language result.
  if (!entry.languages.has_value()) {
    params.response.embeddings.push_back(EmbeddingWithMetadata());
    ProcessEachPrompt(std::move(params));
    return;
  }

  bool is_supported = CheckIfLanguageSupported(*entry.languages);
  if (IsFullGroupRequest(params.request)) {
    metrics_->SendLanguageIsSupported(is_supported);
  }
  if (!is_supported) {
    params.response.embeddings.push_back(EmbeddingWithMetadata());
    ProcessEachPrompt(std::move(params));
    return;
  }

  // Download DLC for translator takes time, and seeing an entity that would be
  // translated if grouped suggests that we can pre-download the DLC for the
  // user.
  std::optional<std::string> source_locale =
      GetTranslationSource(*entry.languages, default_locale_);
  if (source_locale.has_value()) {
    i18n::LangPair lang_pair{*source_locale, kEnglish};
    translator_->DownloadDlc(lang_pair, base::DoNothing());
  }

  CheckEntrySafety(std::move(params), entry);
}

void EmbeddingEngine::CheckEntrySafety(ProcessingParams params,
                                       EmbeddingEntry entry) {
  size_t index = params.response.embeddings.size();

  if (entry.safety_verdict.has_value()) {
    if (IsFullGroupRequest(params.request)) {
      metrics_->SendSafetyVerdictCacheHit(true);
    }
    CheckEntrySafetyResult(std::move(params), std::move(entry));
    return;
  }

  if (IsFullGroupRequest(params.request)) {
    metrics_->SendSafetyVerdictCacheHit(false);
  }
  std::string entity_string =
      internal::EntityToTitle(*params.request->entities[index]);
  std::optional<std::string> source_locale =
      GetSafetyTranslationSource(*entry.languages);
  if (source_locale.has_value()) {
    // GetSafetyTranslationSource should never return kEnglish.
    i18n::LangPair lang_pair{*source_locale, kEnglish};
    translator_->Translate(lang_pair, entity_string,
                           base::BindOnce(&EmbeddingEngine::ClassifyTextSafety,
                                          weak_ptr_factory_.GetWeakPtr(),
                                          std::move(params), std::move(entry)));
    return;
  }

  ClassifyTextSafety(std::move(params), std::move(entry),
                     std::move(entity_string));
}

void EmbeddingEngine::ClassifyTextSafety(ProcessingParams params,
                                         EmbeddingEntry entry,
                                         std::optional<std::string> text) {
  if (!text.has_value()) {
    params.response.embeddings.push_back(EmbeddingWithMetadata());
    ProcessEachPrompt(std::move(params));
    return;
  }
  auto on_classify_entity_safety_done = base::BindOnce(
      &EmbeddingEngine::OnClassifyEntitySafetyDone,
      weak_ptr_factory_.GetWeakPtr(), std::move(params), std::move(entry));
  safety_service_manager_->ClassifyTextSafety(
      cros_safety::mojom::SafetyRuleset::kCoral, *text,
      std::move(on_classify_entity_safety_done));
}

void EmbeddingEngine::OnClassifyEntitySafetyDone(
    ProcessingParams params,
    EmbeddingEntry entry,
    cros_safety::mojom::SafetyClassifierVerdict verdict) {
  switch (verdict) {
    case cros_safety::mojom::SafetyClassifierVerdict::kPass:
      entry.safety_verdict = true;
      break;
    case cros_safety::mojom::SafetyClassifierVerdict::kFailedText:
      // Only set it false when the entity is explicitly rejected from the
      // filter.
      entry.safety_verdict = false;
      break;
    default:
      // If some other error encountered during safety filtering (e.g.
      // SafetyService isn't ready), don't save the result to the database, so
      // that it can be retried next time.
      break;
  }

  // Valid safety result is fetched, update database.
  if (entry.safety_verdict.has_value()) {
    size_t index = params.response.embeddings.size();
    PutEmbeddingEntry(*params.request->entities[index], params.prompts[index],
                      entry);
  }

  CheckEntrySafetyResult(std::move(params), std::move(entry));
}

void EmbeddingEngine::CheckEntrySafetyResult(ProcessingParams params,
                                             EmbeddingEntry entry) {
  // Doesn't have verdict.
  if (!entry.safety_verdict.has_value()) {
    params.response.embeddings.push_back(EmbeddingWithMetadata());
    ProcessEachPrompt(std::move(params));
    return;
  }
  bool passed = *entry.safety_verdict;
  if (IsFullGroupRequest(params.request)) {
    metrics_->SendSafetyVerdict(passed ? metrics::SafetyVerdict::kPass
                                       : metrics::SafetyVerdict::kFail);
  }
  if (!passed) {
    params.response.embeddings.push_back(EmbeddingWithMetadata());
    ProcessEachPrompt(std::move(params));
    return;
  }
  CheckEntryEmbedding(std::move(params), entry);
}

void EmbeddingEngine::CheckEntryEmbedding(ProcessingParams params,
                                          EmbeddingEntry entry) {
  if (!entry.embedding.empty()) {
    if (IsFullGroupRequest(params.request)) {
      metrics_->SendEmbeddingCacheHit(true);
    }
    params.response.embeddings.push_back(
        EmbeddingWithMetadata{.embedding = std::move(entry.embedding),
                              .language_result = std::move(*entry.languages)});
    ProcessEachPrompt(std::move(params));
    return;
  }

  if (IsFullGroupRequest(params.request)) {
    metrics_->SendEmbeddingCacheHit(false);
  }

  size_t index = params.response.embeddings.size();
  auto timer = odml::PerformanceTimer::Create();
  auto embed_request = embedding_model::mojom::GenerateEmbeddingRequest::New();
  embed_request->content = params.prompts[index];
  embed_request->task_type = embedding_model::mojom::TaskType::kClustering;
  embed_request->truncate_input = true;
  auto on_model_output = base::BindOnce(
      &EmbeddingEngine::OnModelOutput, weak_ptr_factory_.GetWeakPtr(),
      std::move(params), std::move(entry), std::move(timer));
  model_->GenerateEmbedding(std::move(embed_request),
                            std::move(on_model_output));
}

void EmbeddingEngine::OnModelOutput(ProcessingParams params,
                                    EmbeddingEntry entry,
                                    odml::PerformanceTimer::Ptr timer,
                                    OnDeviceEmbeddingModelInferenceError error,
                                    const std::vector<float>& embedding) {
  // TODO(b/361429567): We can achieve better error tolerance by dropping
  // problematic input entities. For now, fail on any error for simplicity.
  if (error != OnDeviceEmbeddingModelInferenceError::kSuccess) {
    LOG(ERROR) << "Model execution failed with result: "
               << static_cast<int>(error);
    std::move(params.callback)
        .Run(std::move(params.request),
             base::unexpected(CoralError::kModelExecutionFailed));
    return;
  }
  metrics_->SendGenerateEmbeddingLatency(timer->GetDuration());

  // Cache the embedding.
  entry.embedding = embedding;
  size_t index = params.response.embeddings.size();
  PutEmbeddingEntry(*params.request->entities[index], params.prompts[index],
                    entry);

  params.response.embeddings.push_back(EmbeddingWithMetadata{
      .embedding = embedding, .language_result = std::move(*entry.languages)});
  ProcessEachPrompt(std::move(params));
}

EmbeddingEntry EmbeddingEngine::GetEmbeddingEntry(const mojom::Entity& entity,
                                                  const std::string& prompt) {
  if (embedding_database_) {
    std::optional<std::string> cache_key =
        internal::EntityToCacheKey(entity, prompt, model_version_);
    if (cache_key.has_value()) {
      return embedding_database_->Get(*cache_key);
    }
  }
  return EmbeddingEntry{.embedding = Embedding(),
                        .safety_verdict = std::nullopt};
}

void EmbeddingEngine::PutEmbeddingEntry(const mojom::Entity& entity,
                                        const std::string& prompt,
                                        EmbeddingEntry entry) {
  if (embedding_database_) {
    std::optional<std::string> cache_key =
        internal::EntityToCacheKey(entity, prompt, model_version_);
    if (cache_key.has_value()) {
      embedding_database_->Put(*cache_key, std::move(entry));
    }
  }
}

void EmbeddingEngine::SyncDatabase() {
  if (embedding_database_) {
    embedding_database_->Sync();
  }
}

void EmbeddingEngine::HandleProcessResult(
    EmbeddingCallback callback,
    odml::PerformanceTimer::Ptr timer,
    mojom::GroupRequestPtr request,
    CoralResult<EmbeddingResponse> result) {
  if (IsFullGroupRequest(request)) {
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
