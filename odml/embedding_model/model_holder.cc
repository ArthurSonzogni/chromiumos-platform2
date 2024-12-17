// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/embedding_model/model_holder.h"

#include <string>
#include <utility>
#include <vector>

#include <base/types/pass_key.h>

namespace embedding_model {

constexpr char kLoadModelTimeHistogramName[] =
    "OnDeviceModel.Embedding.LoadModelTime";

ModelReference::ModelReference(base::WeakPtr<ModelHolder> holder)
    : holder_(holder) {}

ModelReference::~ModelReference() {
  if (holder_) {
    holder_->Release(this);
  }
}

void ModelReference::Run(mojom::GenerateEmbeddingRequestPtr request,
                         ModelRunner::RunCallback callback) {
  std::unique_ptr<InferenceJobInfo> job =
      make_unique<InferenceJobInfo>(std::move(request), std::move(callback));
  if (holder_) {
    holder_->QueueRequest(std::move(job));
  } else {
    std::move(job->callback)
        .Run(mojom::OnDeviceEmbeddingModelInferenceError::kModelLoadFailed,
             std::vector<float>(0, 0.0));
  }
}

std::string ModelReference::GetModelVersion() {
  if (holder_) {
    return holder_->GetModelVersion();
  } else {
    return "Invalid";
  }
}

ModelHolder::ModelHolder(std::unique_ptr<ModelRunner> model_runner,
                         const raw_ref<MetricsLibraryInterface> metrics)
    : model_runner_(std::move(model_runner)),
      state_(HolderState::NOT_LOADED),
      metrics_(metrics) {}

std::unique_ptr<ModelReference> ModelHolder::Acquire() {
  std::unique_ptr<ModelReference> result =
      std::make_unique<ModelReference>(weak_factory_.GetWeakPtr());
  referenced_.insert(result.get());

  StateCheck();
  return result;
}

void ModelHolder::Release(ModelReference* ref) {
  CHECK_EQ(referenced_.count(ref), 1);
  referenced_.erase(ref);

  StateCheck();
}

void ModelHolder::QueueRequest(std::unique_ptr<InferenceJobInfo> job) {
  queued_tasks_.push(std::move(job));

  StateCheck();
}

void ModelHolder::TriggerLoad() {
  if (state_ != HolderState::NOT_LOADED) {
    // No need to try.
    return;
  }

  state_ = HolderState::LOADING;
  auto timer = odml::PerformanceTimer::Create();
  model_runner_->Load(base::PassKey<ModelHolder>(),
                      base::BindOnce(&ModelHolder::OnLoadFinish,
                                     base::Unretained(this), std::move(timer)));
}

void ModelHolder::OnLoadFinish(odml::PerformanceTimer::Ptr timer,
                               bool success) {
  CHECK_EQ(state_, HolderState::LOADING);
  if (success) {
    state_ = HolderState::LOADED;
    metrics_->SendTimeToUMA(/*name=*/kLoadModelTimeHistogramName,
                            /*sample=*/timer->GetDuration(),
                            /*min=*/base::Milliseconds(1),
                            /*max=*/base::Seconds(20), /*buckets=*/50);
  } else {
    state_ = HolderState::FAILED;
  }

  StateCheck();
}

void ModelHolder::StateCheck() {
  if (state_ == HolderState::LOADED) {
    // Resolve any WaitLoadResult
    while (!wait_load_result_callbacks_.empty()) {
      auto cb = std::move(wait_load_result_callbacks_.front());
      wait_load_result_callbacks_.pop();
      std::move(cb).Run(true);
    }

    // Check if there's any pending task.
    if (!queued_tasks_.empty()) {
      RunJob();
      return;
    }

    // Should we unload the model?
    if (referenced_.empty()) {
      TriggerUnload();
      return;
    }
  } else if (state_ == HolderState::NOT_LOADED) {
    if (!queued_tasks_.empty() || !referenced_.empty()) {
      TriggerLoad();
      return;
    }
  } else if (state_ == HolderState::FAILED) {
    // We should fail all pending jobs because the load is unsuccessful.
    std::vector<float> empty_embeddings;
    while (!queued_tasks_.empty()) {
      std::move(queued_tasks_.front()->callback)
          .Run(mojom::OnDeviceEmbeddingModelInferenceError::kModelLoadFailed,
               empty_embeddings);
      queued_tasks_.pop();
    }

    // WaitLoadResult should be resolved on failure as well.
    while (!wait_load_result_callbacks_.empty()) {
      auto cb = std::move(wait_load_result_callbacks_.front());
      wait_load_result_callbacks_.pop();
      std::move(cb).Run(false);
    }
  }
}

void ModelHolder::TriggerUnload() {
  CHECK_EQ(state_, HolderState::LOADED);

  state_ = HolderState::UNLOADING;
  model_runner_->Unload(
      base::PassKey<ModelHolder>(),
      base::BindOnce(&ModelHolder::OnUnloadFinish, base::Unretained(this)));
}

void ModelHolder::OnUnloadFinish() {
  CHECK_EQ(state_, HolderState::UNLOADING);
  state_ = HolderState::NOT_LOADED;

  StateCheck();
}

void ModelHolder::RunJob() {
  CHECK_EQ(state_, HolderState::LOADED);
  state_ = HolderState::RUNNING;
  model_runner_->Run(
      base::PassKey<ModelHolder>(), std::move(queued_tasks_.front()->request),
      base::BindOnce(&ModelHolder::OnRunFinish, base::Unretained(this)));
}

void ModelHolder::OnRunFinish(mojom::OnDeviceEmbeddingModelInferenceError error,
                              const std::vector<float>& embeddings) {
  CHECK_EQ(state_, HolderState::RUNNING);

  std::move(queued_tasks_.front()->callback).Run(error, embeddings);
  queued_tasks_.pop();
  state_ = HolderState::LOADED;
  StateCheck();
}

std::string ModelHolder::GetModelVersion() {
  return model_runner_->GetModelVersion();
}

bool ModelHolder::IsLoaded() {
  switch (state_) {
    case HolderState::LOADED:
    case HolderState::RUNNING:
      return true;
    default:
      return false;
  }
}

void ModelHolder::WaitLoadResult(WaitLoadResultCallback callback) {
  if (IsLoaded()) {
    std::move(callback).Run(true);
    return;
  }
  if (state_ == HolderState::FAILED) {
    std::move(callback).Run(false);
    return;
  }

  wait_load_result_callbacks_.push(std::move(callback));
}

}  // namespace embedding_model
