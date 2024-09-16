// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/embedding_model/model_factory.h"

#include <utility>

#include "odml/embedding_model/model_runner.h"
#include "odml/embedding_model/tflite_model_runner.h"

namespace embedding_model {

ModelFactory::ModelFactory() {}

std::unique_ptr<ModelRunner> ModelFactory::BuildRunnerFromInfo(
    ModelInfo&& info) {
  if (info.model_type == kEmbeddingTfliteModelType) {
    std::unique_ptr<TfliteModelRunner> result =
        std::make_unique<TfliteModelRunner>(std::move(info));
    return result;
  }
  return nullptr;
}

void ModelFactory::BuildRunnerFromUuid(const base::Uuid& uuid,
                                       BuildRunnerFromUuidCallback callback) {
  std::move(callback).Run(nullptr);
}

}  // namespace embedding_model
