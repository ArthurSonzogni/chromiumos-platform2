// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_EMBEDDING_MODEL_MODEL_FACTORY_H_
#define ODML_EMBEDDING_MODEL_MODEL_FACTORY_H_

#include <memory>

#include <base/functional/callback.h>
#include <base/uuid.h>

#include "odml/embedding_model/model_runner.h"

namespace embedding_model {

// This produce the ModelRunner objects for each of the models.
class ModelFactory {
 public:
  ModelFactory();
  virtual ~ModelFactory() = default;

  // Builds a ModelRunner with the given information. This does not initialize
  // or load the model. As this is only the creation of a class, it's
  // synchronous.
  virtual std::unique_ptr<ModelRunner> BuildRunnerFromInfo(ModelInfo&& info);

  using BuildRunnerFromUuidCallback =
      base::OnceCallback<void(std::unique_ptr<ModelRunner> result)>;

  // For the given UUID, load the DLC, examine its content and if it's an
  // embedding model, create the ModelRunner.
  virtual void BuildRunnerFromUuid(const base::Uuid& uuid,
                                   BuildRunnerFromUuidCallback callback);
};

}  // namespace embedding_model

#endif  // ODML_EMBEDDING_MODEL_MODEL_FACTORY_H_
