// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_EMBEDDING_MODEL_MODEL_FACTORY_MOCK_H_
#define ODML_EMBEDDING_MODEL_MODEL_FACTORY_MOCK_H_

#include <memory>

#include <gmock/gmock.h>

#include "odml/embedding_model/model_factory.h"

namespace embedding_model {

class ModelFactoryMock : public ModelFactory {
 public:
  MOCK_METHOD(std::unique_ptr<ModelRunner>,
              BuildRunnerFromInfo,
              (ModelInfo && info),
              (override));

  MOCK_METHOD(void,
              BuildRunnerFromUuid,
              (const base::Uuid& uuid, BuildRunnerFromUuidCallback callback),
              (override));
};

}  // namespace embedding_model

#endif  // ODML_EMBEDDING_MODEL_MODEL_FACTORY_MOCK_H_
