// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_CORAL_TITLE_GENERATION_ENGINE_H_
#define ODML_CORAL_TITLE_GENERATION_ENGINE_H_

#include <string>
#include <vector>

#include <base/functional/callback.h>
#include <base/timer/wall_clock_timer.h>
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

  using TitleGenerationCallback = base::OnceCallback<void(
      mojom::GroupRequestPtr, CoralResult<TitleGenerationResponse>)>;
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
  void Process(mojom::GroupRequestPtr request,
               ClusteringResponse clustering_response,
               mojo::PendingRemote<mojom::TitleObserver> observer,
               TitleGenerationCallback callback) override;

 private:
  void EnsureModelLoaded(base::OnceClosure callback);
  void OnModelLoadResult(on_device_model::mojom::LoadModelResult result);
  void SetUnloadModelTimer();
  void UnloadModel();

  void DoProcess(mojom::GroupRequestPtr request,
                 ClusteringResponse clustering_response,
                 TitleGenerationCallback callback);
  // One-by-one, send the next entry in `prompts` to the on device model session
  // to generate the title (using `OnModelOutput` as callback), then form the
  // corresponding group and push to `response`.
  void ProcessEachPrompt(mojom::GroupRequestPtr request,
                         SimpleSession::Ptr session,
                         std::vector<Cluster> clusters,
                         std::vector<std::string> prompts,
                         TitleGenerationResponse response,
                         TitleGenerationCallback callback);
  void OnModelOutput(mojom::GroupRequestPtr request,
                     SimpleSession::Ptr session,
                     std::vector<Cluster> clusters,
                     std::vector<std::string> prompts,
                     TitleGenerationResponse response,
                     TitleGenerationCallback callback,
                     std::string title);

  const raw_ref<on_device_model::mojom::OnDeviceModelPlatformService>
      on_device_model_service_;
  // model_ should only be used when state_ is kLoaded because the remote model
  // service only binds the model receiver when model loading succeeds.
  mojo::Remote<on_device_model::mojom::OnDeviceModel> model_;
  ModelLoadState state_ = ModelLoadState::kNew;

  // Callbacks that are queued and waiting for the model to be loaded.
  std::vector<base::OnceClosure> pending_callbacks_;

  base::WallClockTimer unload_model_timer_;

  base::WeakPtrFactory<TitleGenerationEngine> weak_ptr_factory_{this};
};

}  // namespace coral

#endif  // ODML_CORAL_TITLE_GENERATION_ENGINE_H_
