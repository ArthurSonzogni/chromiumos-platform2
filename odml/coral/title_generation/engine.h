// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_CORAL_TITLE_GENERATION_ENGINE_H_
#define ODML_CORAL_TITLE_GENERATION_ENGINE_H_

#include <queue>
#include <string>
#include <vector>

#include <base/functional/callback.h>
#include <base/timer/wall_clock_timer.h>
#include <base/token.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "odml/coral/clustering/engine.h"
#include "odml/coral/common.h"
#include "odml/coral/title_generation/simple_session.h"
#include "odml/mojom/coral_service.mojom.h"
#include "odml/mojom/on_device_model.mojom.h"
#include "odml/mojom/on_device_model_service.mojom.h"

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

class TitleGenerationEngine : public TitleGenerationEngineInterface {
 public:
  explicit TitleGenerationEngine(
      raw_ref<on_device_model::mojom::OnDeviceModelPlatformService>
          on_device_model_service);
  ~TitleGenerationEngine() override = default;

  // TitleGenerationEngineInterface overrides.
  void PrepareResource() override;
  void Process(mojom::GroupRequestPtr request,
               ClusteringResponse clustering_response,
               mojo::PendingRemote<mojom::TitleObserver> observer,
               TitleGenerationCallback callback) override;

 private:
  void EnsureModelLoaded(base::OnceClosure callback);
  void OnModelLoadResult(base::OnceClosure callback,
                         on_device_model::mojom::LoadModelResult result);
  void SetUnloadModelTimer();
  void UnloadModel();

  struct GroupData {
    base::Token id;
    std::string title;
    std::string prompt;
    std::vector<mojom::EntityPtr> entities;
    // When the operation fails in the middle, we need to know which groups we
    // haven't updated to the title observer.
    bool updated_to_observer = false;
  };
  // This moves out `entities` field from GroupData to avoid copy since the
  // field is only needed for response, and we return the response here.
  void ReplyGroupsWithoutTitles(
      std::vector<GroupData>& groups,
      TitleGenerationEngine::TitleGenerationCallback callback);
  // Used as the DoProcess callback in the case that no observer provided, so
  // titles have to be returned in the TitleGenerationResponse.
  void ReplyGroupsWithTitles(
      TitleGenerationEngine::TitleGenerationCallback callback,
      mojo::Remote<mojom::TitleObserver> unused_observer,
      std::vector<GroupData> groups,
      CoralResult<void> result);
  // Used as the DoProcess callback in the case that observer is provided, so
  // the title generation response is already returned and here we just have to
  // handle title generation failure.
  void OnAllTitleGenerationFinished(mojo::Remote<mojom::TitleObserver> observer,
                                    std::vector<GroupData> groups,
                                    CoralResult<void> result);

  void OnProcessCompleted();

  using ProcessCallback =
      base::OnceCallback<void(mojo::Remote<mojom::TitleObserver>,
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
                     std::string title);

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

  base::WeakPtrFactory<TitleGenerationEngine> weak_ptr_factory_{this};
};

}  // namespace coral

#endif  // ODML_CORAL_TITLE_GENERATION_ENGINE_H_
