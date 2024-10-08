// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_EMBEDDING_MODEL_TFLITE_MODEL_RUNNER_H_
#define ODML_EMBEDDING_MODEL_TFLITE_MODEL_RUNNER_H_

#include <string>

#include "odml/embedding_model/model_info.h"
#include "odml/embedding_model/model_runner.h"

namespace embedding_model {

class TfliteModelRunner : public ModelRunner {
 public:
  explicit TfliteModelRunner(ModelInfo&& model_info);

  void Load(base::PassKey<ModelHolder> passkey, LoadCallback callback) override;

  void Unload(base::PassKey<ModelHolder> passkey,
              UnloadCallback callback) override;

  std::string GetModelVersion() override;

  void Run(base::PassKey<ModelHolder> passkey,
           mojom::GenerateEmbeddingRequestPtr request,
           RunCallback callback) override;

 private:
  ModelInfo model_info_;
};

}  // namespace embedding_model

#endif  // ODML_EMBEDDING_MODEL_TFLITE_MODEL_RUNNER_H_
