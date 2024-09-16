// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_EMBEDDING_MODEL_MODEL_RUNNER_H_
#define ODML_EMBEDDING_MODEL_MODEL_RUNNER_H_

#include <string>
#include <vector>

#include <base/functional/callback.h>

#include "odml/embedding_model/model_info.h"
#include "odml/mojom/embedding_model.mojom.h"

namespace embedding_model {

// ModelRunner is an abstract class that represents the interface to a text
// embedding model. The ModelHolder will hold instance of ModelRunner to ensure
// multiple loads to the same model will result in only 1 instance of
// ModelRunner. All calls to ModelRunner should happen on the same thread, with
// the exception of constructor and destructor.
// Callers to ModelRunner must serialize all operations. if any one of Load(),
// Unload() or Run() is in-flight, then no further calls can be made until the
// current in-flight call has finished.
class ModelRunner {
 public:
  ModelRunner() = default;
  virtual ~ModelRunner() = default;

  using LoadCallback = base::OnceCallback<void(bool success)>;

  // Calling Load() loads the model. Once the load finishes successfully
  // |callback| will be called with true, if not it'll be called with false.
  // Caller should not call this if another call (Load() or Unload()) is in
  // progress.
  virtual void Load(LoadCallback callback) = 0;

  using UnloadCallback = base::OnceCallback<void()>;

  // Calling Unload() unloads the model. Once the model is unloaded, |callback|
  // is called. Caller should not call this if another call (Load() or Unload())
  // is in progress.
  virtual void Unload(UnloadCallback callback) = 0;

  // Return the model version. See mojom::OnDeviceEmbeddingModel::Version() for
  // more info.
  // This may be called any time, no need to serialize this.
  virtual std::string GetModelVersion() = 0;

  using RunCallback =
      base::OnceCallback<void(mojom::OnDeviceEmbeddingModelInferenceError error,
                              const std::vector<float>& embeddings)>;

  // Run() runs the embedding inference, converting a string into a vector
  // embedding.
  virtual void Run(mojom::GenerateEmbeddingRequestPtr request,
                   RunCallback callback) = 0;
};

}  // namespace embedding_model

#endif  // ODML_EMBEDDING_MODEL_MODEL_RUNNER_H_
