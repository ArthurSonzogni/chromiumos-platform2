// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_CORAL_TITLE_GENERATION_ENGINE_H_
#define ODML_CORAL_TITLE_GENERATION_ENGINE_H_

#include <memory>
#include <queue>
#include <string>
#include <vector>

#include <base/containers/lru_cache.h>
#include <base/functional/callback.h>
#include <base/memory/raw_ref.h>
#include <base/timer/wall_clock_timer.h>
#include <base/token.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "odml/coral/clustering/engine.h"
#include "odml/coral/common.h"
#include "odml/coral/delayed_repeating_timer.h"
#include "odml/coral/metrics.h"
#include "odml/coral/title_generation/cache_storage.h"
#include "odml/coral/title_generation/simple_session.h"
#include "odml/i18n/translator.h"
#include "odml/mojom/coral_service.mojom.h"
#include "odml/mojom/on_device_model.mojom.h"
#include "odml/mojom/on_device_model_service.mojom.h"
#include "odml/session_state_manager/session_state_manager.h"
#include "odml/utils/performance_timer.h"

namespace coral {

struct TitleGenerationResponse : public MoveOnly {
  bool operator==(const TitleGenerationResponse&) const = default;

  std::vector<mojom::GroupPtr> groups;
};

class TitleGenerationEngineInterface {
 public:
  virtual ~TitleGenerationEngineInterface() = default;

  // Claim resources necessary for `Process`, like downloading from dlc, loading
  // model etc. It is not necessary to call this before `Process`, but the first
  // `Process` will take longer without calling `PrepareResource` first.
  virtual void PrepareResource(std::optional<std::string> language_code) {}

  using TitleGenerationCallback =
      base::OnceCallback<void(CoralResult<TitleGenerationResponse>)>;
  virtual void Process(mojom::GroupRequestPtr request,
                       ClusteringResponse clustering_response,
                       mojo::PendingRemote<mojom::TitleObserver> observer,
                       TitleGenerationCallback callback) = 0;
};

class TitleGenerationEngine
    : public TitleGenerationEngineInterface,
      public odml::SessionStateManagerInterface::Observer {
 public:
  TitleGenerationEngine(
      raw_ref<CoralMetrics> metrics,
      raw_ref<on_device_model::mojom::OnDeviceModelPlatformService>
          on_device_model_service,
      odml::SessionStateManagerInterface* session_state_manager,
      raw_ref<i18n::Translator> translator,
      std::unique_ptr<TitleCacheStorageInterface> title_cache_storage);
  ~TitleGenerationEngine() override = default;

  // TitleGenerationEngineInterface overrides.
  // TitleGenerationEngine only processes 1 PrepareResource/Process request,
  // until it finishes. This is to simplify state management of the loaded
  // models.
  void PrepareResource(std::optional<std::string> language_code) override;
  void Process(mojom::GroupRequestPtr request,
               ClusteringResponse clustering_response,
               mojo::PendingRemote<mojom::TitleObserver> observer,
               TitleGenerationCallback callback) override;

  // SessionStateManagerInterface overrides.
  void OnUserLoggedIn(
      const odml::SessionStateManagerInterface::User& user) override;
  void OnUserLoggedOut() override;

  std::optional<std::string> GetNthTitleCacheKeyForTesting(int n);

 private:
  void EnsureTranslatorInitialized(base::OnceClosure callback);
  void GetModelState(const std::string& locale, base::OnceClosure callback);
  void OnGetModelStateResult(const std::string& locale,
                             base::OnceClosure callback,
                             on_device_model::mojom::PlatformModelState state);
  void EnsureModelLoaded(const std::string& locale, base::OnceClosure callback);
  void OnModelLoadResult(
      base::OnceClosure callback,
      odml::PerformanceTimer::Ptr timer,
      mojo::Remote<on_device_model::mojom::OnDeviceModel> original_model,
      on_device_model::mojom::LoadModelResult result);
  void UnloadModel();

  struct GroupData {
    base::Token id;
    std::optional<std::string> title;
    std::vector<EntityWithMetadata> entities;
  };
  void ReplyGroupsWithoutTitles(
      const std::vector<GroupData>& groups,
      TitleGenerationEngine::TitleGenerationCallback callback);
  // Used as the DoProcess callback in the case that no observer provided, so
  // titles have to be returned in the TitleGenerationResponse.
  void ReplyGroupsWithTitles(
      odml::PerformanceTimer::Ptr timer,
      TitleGenerationEngine::TitleGenerationCallback callback,
      base::OnceClosure on_complete,
      mojo::Remote<mojom::TitleObserver> unused_observer,
      std::vector<GroupData> groups,
      CoralResult<void> result);
  // Used as the DoProcess callback in the case that observer is provided, so
  // the title generation response is already returned and here we just have to
  // handle title generation failure.
  void OnAllTitleGenerationFinished(odml::PerformanceTimer::Ptr timer,
                                    base::OnceClosure on_complete,
                                    mojo::Remote<mojom::TitleObserver> observer,
                                    std::vector<GroupData> groups,
                                    CoralResult<void> result);

  void ReportTitleGenerationMetrics(odml::PerformanceTimer::Ptr timer,
                                    CoralStatus status);

  void OnProcessCompleted();

  using ProcessCallback =
      base::OnceCallback<void(mojo::Remote<mojom::TitleObserver>,
                              std::vector<GroupData>,
                              CoralResult<void>)>;
  struct ProcessParams : MoveOnly {
    size_t index;
    mojom::GroupRequestPtr request;
    SimpleSession::Ptr session;
    mojo::Remote<mojom::TitleObserver> observer;
    std::vector<GroupData> groups;
    ProcessCallback callback;
  };
  void DoProcess(mojom::GroupRequestPtr request,
                 mojo::Remote<mojom::TitleObserver> observer,
                 std::vector<GroupData> groups,
                 ProcessCallback callback);
  // One-by-one, send the next entry in `groups` to the on device model session
  // to generate the title (using `OnModelOutput` as callback), then form the
  // corresponding group and update `groups`.
  void ProcessEachPrompt(ProcessParams params);
  void EntitiesToMaybeTranslatedTitles(ProcessParams params,
                                       odml::PerformanceTimer::Ptr timer,
                                       std::vector<std::string> titles);
  void OnTranslateResult(ProcessParams params,
                         odml::PerformanceTimer::Ptr timer,
                         std::vector<std::string> titles,
                         std::optional<std::string> translated);
  void OnFormatInputResponse(ProcessParams params,
                             odml::PerformanceTimer::Ptr timer,
                             const std::optional<std::string>& formatted);
  void OnSizeInTokensResponse(ProcessParams params,
                              odml::PerformanceTimer::Ptr timer,
                              std::string prompt,
                              uint32_t size_in_tokens);
  void OnModelOutput(ProcessParams params,
                     odml::PerformanceTimer::Ptr timer,
                     std::string title);

  // Generated groups, along with their titles, are saved to an LRU cache. When
  // we receive request groups, we first check against all entries of LRU cache
  // to see whether any cached group is similar enough to the request group. If
  // so, we can reuse the title without using the model to generate one.
  void CacheGroupTitles(const std::vector<GroupData>& groups);
  std::optional<std::string> MaybeGetCachedTitle(
      const std::vector<EntityWithMetadata>& entities);

  void ReportPromptSizes(size_t index,
                         SimpleSession::Ptr session,
                         std::vector<GroupData> groups,
                         base::OnceClosure on_complete);

  // Called by cache_flush_timer_ to flush the title cache to disk.
  void OnFlushCacheTimer();

  const raw_ref<CoralMetrics> metrics_;

  const raw_ref<on_device_model::mojom::OnDeviceModelPlatformService>
      on_device_model_service_;

  const raw_ref<i18n::Translator> translator_;

  // The default locale of the engine.
  std::optional<std::string> default_locale_;

  // `model_` should only be used after a successful LoadModelResult is received
  // because on device service only binds the model receiver when model loading
  // succeeds.
  mojo::Remote<on_device_model::mojom::OnDeviceModel> model_;
  // The locale of the model we load. This is updated when model_ is
  // bind/unbinded.
  std::optional<std::string> model_locale_;

  // Callbacks that are queued and waiting for the previous request to
  // complete, and flag to indicate that a request is being processed.
  std::queue<base::OnceClosure> pending_callbacks_;
  bool is_processing_ = false;

  // The `title_cache_` is designed to be a HashingLRUCache with `std::string
  // title` as key, and `std::vector<EntityPtr> entities` as value. We use the
  // title as LRU cache map key (i.e., we overwrite and dedup cache entries with
  // the same title) because:
  // 1. Hashing and comparison of string is simpler and more performant than
  // large maps.
  // 2. Logically different groups are very unlikely to share the same title.
  // The different groups that have the same titles are likely the same topic
  // group the user has, but gradually updated through navigation events. In
  // that case, only the most recent group is useful in the cache, so using
  // group title as key is fine (or better).
  // The value is only storing titles instead of entire entities because the
  // title generation only takes entity titles as input. Multiset is used
  // because the number of each titles and the group size are needed to
  // calculate the similarity ratio.
  base::HashingLRUCache<std::string, TitleCacheEntry> title_cache_;
  // Record and the current user to compare whether the user is same when
  // attempting to reuse title cache. We shouldn't reuse cache from other users.
  std::optional<odml::SessionStateManagerInterface::User> current_user_;
  // This is used to trigger the periodic cache flush to disk.
  std::unique_ptr<DelayedRepeatingTimer> cache_flush_timer_;
  // If title_cache_ is dirty and needs flushing.
  bool title_cache_dirty_ = false;

  // For loading and saving the title cache.
  std::unique_ptr<TitleCacheStorageInterface> title_cache_storage_;

  base::WeakPtrFactory<TitleGenerationEngine> weak_ptr_factory_{this};
};

}  // namespace coral

#endif  // ODML_CORAL_TITLE_GENERATION_ENGINE_H_
