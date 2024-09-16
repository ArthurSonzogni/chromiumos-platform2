// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_EMBEDDING_MODEL_MODEL_HOLDER_H_
#define ODML_EMBEDDING_MODEL_MODEL_HOLDER_H_

#include <memory>
#include <queue>
#include <string>
#include <unordered_set>
#include <vector>

#include <base/memory/weak_ptr.h>

#include "odml/embedding_model/model_info.h"
#include "odml/embedding_model/model_runner.h"

namespace embedding_model {

class ModelHolder;

struct InferenceJobInfo {
  mojom::GenerateEmbeddingRequestPtr request;
  ModelRunner::RunCallback callback;
};

// Each ModelReference instance represents one user of the model, when all
// instances of ModelReference is destroyed, ModelHolder will unload the model.
// Usually this corresponds 1:1 with the OnDeviceEmbeddingModelService object in
// mojo.
class ModelReference {
 public:
  explicit ModelReference(base::WeakPtr<ModelHolder> holder);

  ~ModelReference();

  // Run inference with the model.
  void Run(mojom::GenerateEmbeddingRequestPtr request,
           ModelRunner::RunCallback callback);

  // Basically ModelRunner::GetModelVersion().
  std::string GetModelVersion();

 private:
  base::WeakPtr<ModelHolder> holder_;
};

// ModelHolder is a class for ensuring we don't load multiple copies of the same
// embedding model or the model is unloaded whenever nobody is using it.
// If a model needs to run on a separate thread, then it is designed that the
// ModelHolder will deal with the thread creation and ownership.
class ModelHolder {
 public:
  explicit ModelHolder(std::unique_ptr<ModelRunner> model_runner);

  // Call this to acquire a ModelReference, denoting that someone is using the
  // model.
  std::unique_ptr<ModelReference> Acquire();

  // This should only be called by ModelReference destruction, to denote that
  // the user is no longer using the model.
  void Release(ModelReference* ref);

  // This should only be called by ModelReference, to queue/start the processing
  // of an embedding request.
  void QueueRequest(std::unique_ptr<InferenceJobInfo> job);

  // Basically ModelRunner::GetModelVersion().
  std::string GetModelVersion();

 private:
  enum class HolderState {
    // The model is not loaded.
    NOT_LOADED,
    // The model is in process of being loaded, that is, ModelRunner::Load() has
    // been called but not yet finished.
    LOADING,
    // The model is loaded but it is not inferencing, that is,
    // ModelRunner::Load() finished but there's no in-flight ModelRunner::Run().
    LOADED,
    // The model is running, there's an in-flight ModelRunner::Run() call.
    RUNNING,
    // The model is being unloaded, that is, ModelRunner::Unload() has been
    // called but not yet finished.
    UNLOADING,
  };

  // Attempt to transition from NOT_LOADED into LOADING.
  void TriggerLoad();

  // Called by ModelRunner's Load().
  void OnLoadFinish(bool success);

  // Try to unload the model, ie. transition from LOADED to UNLOADING.
  void TriggerUnload();

  // Called by ModelRunner's Unload();
  void OnUnloadFinish();

  // This checks the current state and do anything that we're supposed to do.
  void StateCheck();

  // Run a job at the front of the queue, assume the model is loaded.
  void RunJob();

  // Called by ModelRunner's Run().
  void OnRunFinish(mojom::OnDeviceEmbeddingModelInferenceError error,
                   const std::vector<float>& embeddings);

  std::unordered_set<ModelReference*> referenced_;

  std::queue<std::unique_ptr<InferenceJobInfo>> queued_tasks_;

  std::unique_ptr<ModelRunner> model_runner_;

  HolderState state_;

  base::WeakPtrFactory<ModelHolder> weak_factory_{this};
};

}  // namespace embedding_model

#endif  // ODML_EMBEDDING_MODEL_MODEL_HOLDER_H_
