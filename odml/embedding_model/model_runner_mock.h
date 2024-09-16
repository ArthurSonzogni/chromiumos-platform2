// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_EMBEDDING_MODEL_MODEL_RUNNER_MOCK_H_
#define ODML_EMBEDDING_MODEL_MODEL_RUNNER_MOCK_H_

#include <string>

#include <gmock/gmock.h>

#include "odml/embedding_model/model_info.h"

namespace embedding_model {

class ModelRunnerMock : public ModelRunner {
 public:
  MOCK_METHOD(void, Load, (LoadCallback callback), (override));

  MOCK_METHOD(void, Unload, (UnloadCallback callback), (override));

  MOCK_METHOD(void,
              Run,
              (mojom::GenerateEmbeddingRequestPtr request,
               RunCallback callback),
              (override));

  MOCK_METHOD(std::string, GetModelVersion, (), (override));
};

}  // namespace embedding_model

#endif  // ODML_EMBEDDING_MODEL_MODEL_RUNNER_MOCK_H_
