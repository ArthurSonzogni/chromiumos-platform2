// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_CORAL_TITLE_GENERATION_ENGINE_H_
#define ODML_CORAL_TITLE_GENERATION_ENGINE_H_

#include <queue>
#include <string>
#include <unordered_set>
#include <vector>

#include <base/containers/lru_cache.h>
#include <base/functional/callback.h>
#include <base/timer/wall_clock_timer.h>
#include <base/token.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "odml/coral/clustering/engine.h"
#include "odml/coral/common.h"
#include "odml/coral/metrics.h"
#include "odml/coral/title_generation/simple_session.h"
#include "odml/mojom/coral_service.mojom.h"
#include "odml/mojom/on_device_model.mojom.h"
#include "odml/mojom/on_device_model_service.mojom.h"
#include "odml/session_state_manager/session_state_manager.h"

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
  virtual void PrepareResource() {}

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
      odml::SessionStateManagerInterface* session_state_manager);
  ~TitleGenerationEngine() override = default;

  // TitleGenerationEngineInterface overrides.
  void PrepareResource() override;
  void Process(mojom::GroupRequestPtr request,
               ClusteringResponse clustering_response,
               mojo::PendingRemote<mojom::TitleObserver> observer,
               TitleGenerationCallback callback) override;

  // SessionStateManagerInterface overrides.
  void OnUserLoggedIn(
      const odml::SessionStateManagerInterface::User& user) override;
  void OnUserLoggedOut() override;

 private:
  void EnsureModelLoaded(base::OnceClosure callback);
  void OnModelLoadResult(base::OnceClosure callback,
                         PerformanceTimer::Ptr timer,
                         on_device_model::mojom::LoadModelResult result);
  void SetUnloadModelTimer();
  void UnloadModel();

  struct GroupData {
    base::Token id;
    std::optional<std::string> title;
    std::string prompt;
    std::vector<mojom::EntityPtr> entities;
  };
  void ReplyGroupsWithoutTitles(
      const std::vector<GroupData>& groups,
      TitleGenerationEngine::TitleGenerationCallback callback);
  // Used as the DoProcess callback in the case that no observer provided, so
  // titles have to be returned in the TitleGenerationResponse.
  void ReplyGroupsWithTitles(
      PerformanceTimer::Ptr timer,
      TitleGenerationEngine::TitleGenerationCallback callback,
      base::OnceClosure on_complete,
      mojo::Remote<mojom::TitleObserver> unused_observer,
      SimpleSession::Ptr session,
      std::vector<GroupData> groups,
      CoralResult<void> result);
  // Used as the DoProcess callback in the case that observer is provided, so
  // the title generation response is already returned and here we just have to
  // handle title generation failure.
  void OnAllTitleGenerationFinished(PerformanceTimer::Ptr timer,
                                    base::OnceClosure on_complete,
                                    mojo::Remote<mojom::TitleObserver> observer,
                                    SimpleSession::Ptr session,
                                    std::vector<GroupData> groups,
                                    CoralResult<void> result);

  void ReportTitleGenerationMetrics(PerformanceTimer::Ptr timer,
                                    CoralStatus status);

  void OnProcessCompleted();

  using ProcessCallback =
      base::OnceCallback<void(mojo::Remote<mojom::TitleObserver>,
                              SimpleSession::Ptr,
                              std::vector<GroupData>,
                              CoralResult<void>)>;
  void DoProcess(mojom::GroupRequestPtr request,
                 mojo::Remote<mojom::TitleObserver> observer,
                 std::vector<GroupData> groups,
                 ProcessCallback callback);
  // One-by-one, send the next entry in `groups` to the on device model session
  // to generate the title (using `OnModelOutput` as callback), then form the
  // corresponding group and update `groups`.
  void ProcessEachPrompt(size_t index,
                         mojom::GroupRequestPtr request,
                         SimpleSession::Ptr session,
                         mojo::Remote<mojom::TitleObserver> observer,
                         std::vector<GroupData> groups,
                         ProcessCallback callback);
  void OnModelOutput(size_t index,
                     mojom::GroupRequestPtr request,
                     SimpleSession::Ptr session,
                     mojo::Remote<mojom::TitleObserver> observer,
                     std::vector<GroupData> groups,
                     ProcessCallback callback,
                     PerformanceTimer::Ptr timer,
                     std::string title);

  // Generated groups, along with their titles, are saved to an LRU cache. When
  // we receive request groups, we first check against all entries of LRU cache
  // to see whether any cached group is similar enough to the request group. If
  // so, we can reuse the title without using the model to generate one.
  void CacheGroupTitles(const std::vector<GroupData>& groups);
  std::optional<std::string> MaybeGetCachedTitle(
      const std::vector<mojom::EntityPtr>& entities);

  void ReportPromptSizes(size_t index,
                         SimpleSession::Ptr session,
                         std::vector<GroupData> groups,
                         base::OnceClosure on_complete);
  void OnSizeInTokensResponse(size_t index,
                              SimpleSession::Ptr session,
                              std::vector<GroupData> groups,
                              base::OnceClosure on_complete,
                              uint32_t size_in_tokens);

  const raw_ref<CoralMetrics> metrics_;

  const raw_ref<on_device_model::mojom::OnDeviceModelPlatformService>
      on_device_model_service_;

  // `model_` should only be used after a successful LoadModelResult is received
  // because on device service only binds the model receiver when model loading
  // succeeds.
  mojo::Remote<on_device_model::mojom::OnDeviceModel> model_;

  // Callbacks that are queued and waiting for the previous request to
  // complete, and flag to indicate that a request is being processed.
  std::queue<base::OnceClosure> pending_callbacks_;
  bool is_processing_ = false;

  base::WallClockTimer unload_model_timer_;

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
  struct TitleCacheEntry {
    std::unordered_multiset<std::string> entity_titles;
    odml::SessionStateManagerInterface::User user;
  };
  base::HashingLRUCache<std::string, TitleCacheEntry> title_cache_;
  // Record and the current user to compare whether the user is same when
  // attempting to reuse title cache. We shouldn't reuse cache from other users.
  std::optional<odml::SessionStateManagerInterface::User> current_user_;

  base::WeakPtrFactory<TitleGenerationEngine> weak_ptr_factory_{this};
};

}  // namespace coral

#endif  // ODML_CORAL_TITLE_GENERATION_ENGINE_H_
