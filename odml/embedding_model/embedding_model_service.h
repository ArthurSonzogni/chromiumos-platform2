// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_EMBEDDING_MODEL_EMBEDDING_MODEL_SERVICE_H_
#define ODML_EMBEDDING_MODEL_EMBEDDING_MODEL_SERVICE_H_

#include <memory>
#include <queue>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <base/containers/unique_ptr_adapters.h>
#include <base/functional/callback.h>
#include <base/uuid.h>
#include <metrics/metrics_library.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <mojo/public/cpp/bindings/receiver_set.h>

#include "odml/embedding_model/model_factory.h"
#include "odml/embedding_model/model_holder.h"
#include "odml/mojom/embedding_model.mojom.h"
#include "odml/mojom/on_device_model_service.mojom.h"

namespace embedding_model {

// EmbeddingModelService provides the service that loads embedding model, which
// can be used for inference, that is converting a string into vector
// representation.
class EmbeddingModelService : public mojom::OnDeviceEmbeddingModelService {
 public:
  EmbeddingModelService(raw_ref<MetricsLibraryInterface> metrics,
                        raw_ref<ModelFactory> factory);
  ~EmbeddingModelService() override;

  EmbeddingModelService(const EmbeddingModelService&) = delete;
  EmbeddingModelService& operator=(const EmbeddingModelService&) = delete;

  void AddReceiver(
      mojo::PendingReceiver<mojom::OnDeviceEmbeddingModelService> receiver) {
    receiver_set_.Add(this, std::move(receiver),
                      base::SequencedTaskRunner::GetCurrentDefault());
  }

  void LoadEmbeddingModel(
      const base::Uuid& uuid,
      mojo::PendingReceiver<mojom::OnDeviceEmbeddingModel> model,
      mojo::PendingRemote<on_device_model::mojom::PlatformModelProgressObserver>
          progress_observer,
      LoadEmbeddingModelCallback callback) override;

 private:
  // This is designed to hold any model loading state/information and the model
  // itself.
  struct ModelLoadingState {
    // Actual reference to the ModelHolder.
    std::unique_ptr<ModelHolder> holder = nullptr;
    // Have we called ModelFactory::BuildRunnerFromUuid()?
    bool factory_create_in_progress = false;
    // Set to true if ModelFactory::BuildRunnerFromUuid() failed.
    // This exist to gate retries. Currently we reset this almost immediately at
    // the end of an attempt, so that callbacks are free to attempt a retry
    // immediately. However, if we want certain form of retry rate limiting or
    // allow retry only after network state change, then this should not be
    // reset at the end of an attempt, but only reset whenever an event that
    // releases a retry attempt occurs.
    bool factory_create_failed = false;
    // Anything here will be called when Load() finishes.
    std::queue<base::OnceCallback<void()>> load_finish_callbacks;
    // This reference is needed because we need to acquire a ModelReference to
    // force the ModelHolder to trigger a Load(). It is only populated during
    // the Load() call. A non-null value indicates Load() is in process().
    std::unique_ptr<ModelReference> in_progress_reference;
  };

  // The methods below basically handles the ModelFactory::BuildRunnerFromUuid()
  // and ModelRunner::Load() process. Both are async so we need many callbacks.
  // The overall process is:
  // EnsureModelReady -> ModelFactory::BuildRunnerFromUuid() ->
  // OnBuildRunnerFromUuidFinish() -> TryLoadModel() -> ModelRunner::Load() _>
  // OnModelLoadFinish() -> OnModelReady(). There are a few skips in these
  // methods that allow one to skip over some of the calls if those are already
  // done.
  void EnsureModelReady(const base::Uuid& uuid,
                        base::OnceCallback<void()> callback);
  void OnBuildRunnerFromUuidFinish(const base::Uuid& uuid,
                                   std::unique_ptr<ModelRunner> result);
  void TryLoadModel(const base::Uuid& uuid);
  void OnModelLoadFinish(const base::Uuid& uuid, bool success);
  void OnModelReady(const base::Uuid& uuid,
                    mojo::PendingReceiver<mojom::OnDeviceEmbeddingModel> model,
                    LoadEmbeddingModelCallback callback);

  // This is called by the ModelWrapper when mojo disconnects or deletes that
  // object.
  void DeleteModelWrapper(base::WeakPtr<mojom::OnDeviceEmbeddingModel> model);

  // Contains not just the loading state for the different models and also the
  // model itself.
  std::unordered_map<base::Uuid, struct ModelLoadingState, base::UuidHash>
      loading_state_;

  // For sending metrics.
  const raw_ref<MetricsLibraryInterface> metrics_;
  // For creating the actual models.
  const raw_ref<ModelFactory> factory_;

  mojo::ReceiverSet<mojom::OnDeviceEmbeddingModelService> receiver_set_;

  // Keeps track of all issued ModelWrapper so we can deal with them
  // appropriately when they're disconnected.
  std::set<std::unique_ptr<mojom::OnDeviceEmbeddingModel>,
           base::UniquePtrComparator>
      model_wrappers_;

  base::WeakPtrFactory<EmbeddingModelService> weak_ptr_factory_{this};
};

}  // namespace embedding_model

#endif  // ODML_EMBEDDING_MODEL_EMBEDDING_MODEL_SERVICE_H_
