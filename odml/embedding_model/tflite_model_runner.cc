// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/embedding_model/tflite_model_runner.h"

#include <string>
#include <utility>

namespace embedding_model {

TfliteModelRunner::TfliteModelRunner(ModelInfo&& model_info)
    : model_info_(std::move(model_info)) {}

void TfliteModelRunner::Load(base::PassKey<ModelHolder> passkey,
                             LoadCallback callback) {}

void TfliteModelRunner::Unload(base::PassKey<ModelHolder> passkey,
                               UnloadCallback callback) {}

std::string TfliteModelRunner::GetModelVersion() {
  return model_info_.model_version;
}

void TfliteModelRunner::Run(base::PassKey<ModelHolder> passkey,
                            mojom::GenerateEmbeddingRequestPtr request,
                            RunCallback callback) {}

}  // namespace embedding_model
