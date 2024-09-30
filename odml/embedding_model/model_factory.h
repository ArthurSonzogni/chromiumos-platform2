// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_EMBEDDING_MODEL_MODEL_FACTORY_H_
#define ODML_EMBEDDING_MODEL_MODEL_FACTORY_H_

#include <memory>

#include <base/functional/callback.h>
#include <base/uuid.h>

#include "odml/embedding_model/model_runner.h"
#include "odml/utils/odml_shim_loader.h"

namespace embedding_model {

// Base class for ModelFactory interface.
class ModelFactory {
 public:
  ModelFactory() = default;
  virtual ~ModelFactory() = default;

  // Builds a ModelRunner with the given information. This does not initialize
  // or load the model. As this is only the creation of a class, it's
  // synchronous.
  virtual std::unique_ptr<ModelRunner> BuildRunnerFromInfo(
      ModelInfo&& info) = 0;

  using BuildRunnerFromUuidCallback =
      base::OnceCallback<void(std::unique_ptr<ModelRunner> result)>;

  // For the given UUID, load the DLC, examine its content and if it's an
  // embedding model, create the ModelRunner.
  virtual void BuildRunnerFromUuid(const base::Uuid& uuid,
                                   BuildRunnerFromUuidCallback callback) = 0;
};

// This produce the ModelRunner objects for each of the models.
class ModelFactoryImpl : public ModelFactory {
 public:
  explicit ModelFactoryImpl(const raw_ref<odml::OdmlShimLoader> shim_loader);
  virtual ~ModelFactoryImpl() = default;

  std::unique_ptr<ModelRunner> BuildRunnerFromInfo(ModelInfo&& info) override;

  void BuildRunnerFromUuid(const base::Uuid& uuid,
                           BuildRunnerFromUuidCallback callback) override;

 private:
  // For access to the odml-shim functions, which contains a wrapper to
  // SentencePiece library.
  const raw_ref<odml::OdmlShimLoader> shim_loader_;
};

}  // namespace embedding_model

#endif  // ODML_EMBEDDING_MODEL_MODEL_FACTORY_H_
