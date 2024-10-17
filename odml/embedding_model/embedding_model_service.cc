// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/embedding_model/embedding_model_service.h"

#include <memory>
#include <queue>
#include <string>
#include <utility>

#include "odml/embedding_model/model_holder.h"

namespace embedding_model {

class ModelWrapper final : public mojom::OnDeviceEmbeddingModel {
 public:
  ModelWrapper(
      std::unique_ptr<ModelReference> ref,
      mojo::PendingReceiver<mojom::OnDeviceEmbeddingModel> receiver,
      base::OnceCallback<void(base::WeakPtr<mojom::OnDeviceEmbeddingModel>)>
          on_delete)
      : ref_(std::move(ref)),
        on_delete_(std::move(on_delete)),
        receiver_(this, std::move(receiver)) {
    receiver_.set_disconnect_handler(base::BindRepeating(
        &ModelWrapper::ModelDisconnected, weak_ptr_factory_.GetWeakPtr()));
  }

  void GenerateEmbedding(mojom::GenerateEmbeddingRequestPtr request,
                         GenerateEmbeddingCallback callback) override {
    ref_->Run(std::move(request), std::move(callback));
  }

  void Version(VersionCallback callback) override {
    std::move(callback).Run(ref_->GetModelVersion());
  }

 private:
  void ModelDisconnected() {
    // The ModelWrapper may be destroyed after this call, so no member should be
    // used afterwards.
    std::move(on_delete_).Run(weak_ptr_factory_.GetWeakPtr());
  }

  std::unique_ptr<ModelReference> ref_;

  base::OnceCallback<void(base::WeakPtr<mojom::OnDeviceEmbeddingModel>)>
      on_delete_;
  mojo::Receiver<mojom::OnDeviceEmbeddingModel> receiver_;
  base::WeakPtrFactory<ModelWrapper> weak_ptr_factory_{this};
};

EmbeddingModelService::EmbeddingModelService(
    raw_ref<MetricsLibraryInterface> metrics, raw_ref<ModelFactory> factory)
    : metrics_(metrics), factory_(factory) {}

EmbeddingModelService::~EmbeddingModelService() = default;

void EmbeddingModelService::LoadEmbeddingModel(
    const base::Uuid& uuid,
    mojo::PendingReceiver<mojom::OnDeviceEmbeddingModel> model,
    mojo::PendingRemote<on_device_model::mojom::PlatformModelProgressObserver>
        progress_observer,
    LoadEmbeddingModelCallback callback) {
  auto finish_callback = base::BindOnce(&EmbeddingModelService::OnModelReady,
                                        base::Unretained(this), uuid,
                                        std::move(model), std::move(callback));

  EnsureModelReady(uuid, std::move(finish_callback));
}

void EmbeddingModelService::EnsureModelReady(
    const base::Uuid& uuid, base::OnceCallback<void()> callback) {
  auto itr = loading_state_.find(uuid);
  if (itr != loading_state_.end() && itr->second.holder &&
      itr->second.holder->IsLoaded()) {
    std::move(callback).Run();
    return;
  }
  if (itr == loading_state_.end()) {
    bool insert_ok = false;
    std::tie(itr, insert_ok) =
        loading_state_.insert(std::make_pair(uuid, ModelLoadingState()));
    CHECK(insert_ok);
  }

  if (itr->second.factory_create_failed) {
    // If it failed, then we don't retry.
    std::move(callback).Run();
    return;
  }

  itr->second.load_finish_callbacks.push(std::move(callback));

  // If holder is available, skip to that step directly.
  if (itr->second.holder) {
    TryLoadModel(uuid);
    return;
  }

  // Trigger model create if needed. If currently in progress, then on finish
  // our callback will be triggered automatically so we don't need to do
  // anything.
  if (!itr->second.factory_create_in_progress) {
    itr->second.factory_create_in_progress = true;
    factory_->BuildRunnerFromUuid(
        uuid,
        base::BindOnce(&EmbeddingModelService::OnBuildRunnerFromUuidFinish,
                       base::Unretained(this), uuid));
  }
}

void EmbeddingModelService::OnBuildRunnerFromUuidFinish(
    const base::Uuid& uuid, std::unique_ptr<ModelRunner> result) {
  auto itr = loading_state_.find(uuid);
  CHECK(itr != loading_state_.end());
  itr->second.factory_create_in_progress = false;
  if (result) {
    // If successful, then we need to prepare the model holders.
    itr->second.holder = std::make_unique<ModelHolder>(std::move(result));

    // Trigger the model load.
    TryLoadModel(uuid);
  } else {
    itr->second.factory_create_failed = true;
    OnModelLoadFinish(uuid, false);
  }
}

void EmbeddingModelService::TryLoadModel(const base::Uuid& uuid) {
  auto itr = loading_state_.find(uuid);
  CHECK(itr != loading_state_.end());
  CHECK(itr->second.holder);

  if (itr->second.holder->IsLoaded()) {
    OnModelLoadFinish(uuid, true);
    return;
  }

  // If Load() is not in progress, trigger it. If it's in progress, then it's
  // finish callback will handle the rest for us.
  if (!itr->second.in_progress_reference) {
    std::unique_ptr<ModelReference> ref = itr->second.holder->Acquire();
    itr->second.in_progress_reference = std::move(ref);
    itr->second.holder->WaitLoadResult(
        base::BindOnce(&EmbeddingModelService::OnModelLoadFinish,
                       base::Unretained(this), uuid));
  }
}

void EmbeddingModelService::OnModelLoadFinish(const base::Uuid& uuid,
                                              bool success) {
  auto itr = loading_state_.find(uuid);
  CHECK(itr != loading_state_.end());

  if (success) {
    CHECK(itr->second.holder->IsLoaded());
  } else {
    CHECK(!itr->second.holder || !itr->second.holder->IsLoaded());
    itr->second.in_progress_reference.reset();
  }

  // Whether successful or not, we notify all the pending calls.
  while (!itr->second.load_finish_callbacks.empty()) {
    base::OnceCallback<void()> cb =
        std::move(itr->second.load_finish_callbacks.front());
    itr->second.load_finish_callbacks.pop();
    std::move(cb).Run();
  }
  itr->second.in_progress_reference.reset();
}

void EmbeddingModelService::OnModelReady(
    const base::Uuid& uuid,
    mojo::PendingReceiver<mojom::OnDeviceEmbeddingModel> model,
    LoadEmbeddingModelCallback callback) {
  auto itr = loading_state_.find(uuid);
  if (itr == loading_state_.end() || !itr->second.holder ||
      !itr->second.holder->IsLoaded()) {
    // Load failed.
    std::move(callback).Run(
        on_device_model::mojom::LoadModelResult::kFailedToLoadLibrary);
    return;
  }

  std::unique_ptr<ModelWrapper> wrapper = std::make_unique<ModelWrapper>(
      itr->second.holder->Acquire(), std::move(model),
      base::BindOnce(&EmbeddingModelService::DeleteModelWrapper,
                     base::Unretained(this)));
  model_wrappers_.insert(std::move(wrapper));
  std::move(callback).Run(on_device_model::mojom::LoadModelResult::kSuccess);
}

void EmbeddingModelService::DeleteModelWrapper(
    base::WeakPtr<mojom::OnDeviceEmbeddingModel> model) {
  if (!model) {
    return;
  }

  auto it = model_wrappers_.find(model.get());
  CHECK(it != model_wrappers_.end());
  model_wrappers_.erase(it);
}

}  // namespace embedding_model
