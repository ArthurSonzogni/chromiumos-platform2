// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_CORAL_EMBEDDING_ENGINE_H_
#define ODML_CORAL_EMBEDDING_ENGINE_H_

#include <vector>

#include <base/functional/callback.h>

#include "odml/coral/common.h"
#include "odml/mojom/coral_service.mojom.h"

namespace coral {

using Embedding = std::vector<float>;

struct EmbeddingResponse : public MoveOnly {
  bool operator==(const EmbeddingResponse&) const = default;

  std::vector<Embedding> embeddings;
};

class EmbeddingEngineInterface {
 public:
  virtual ~EmbeddingEngineInterface() = default;

  using EmbeddingCallback =
      base::OnceCallback<void(CoralResult<EmbeddingResponse>)>;
  virtual void Process(const mojom::GroupRequest& request,
                       EmbeddingCallback callback) = 0;
};

class EmbeddingEngine : public EmbeddingEngineInterface {
 public:
  EmbeddingEngine();
  ~EmbeddingEngine() = default;

  // EmbeddingEngineInterface overrides.
  void Process(const mojom::GroupRequest& request,
               EmbeddingCallback callback) override;
};

}  // namespace coral

#endif  // ODML_CORAL_EMBEDDING_ENGINE_H_
