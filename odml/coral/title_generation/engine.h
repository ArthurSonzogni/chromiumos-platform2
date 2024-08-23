// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_CORAL_TITLE_GENERATION_ENGINE_H_
#define ODML_CORAL_TITLE_GENERATION_ENGINE_H_

#include <vector>

#include <base/functional/callback.h>

#include "odml/coral/clustering/engine.h"
#include "odml/coral/common.h"
#include "odml/mojom/coral_service.mojom.h"

namespace coral {

struct TitleGenerationResponse : public MoveOnly {
  bool operator==(const TitleGenerationResponse&) const = default;

  std::vector<mojom::GroupPtr> groups;
};

class TitleGenerationEngineInterface {
 public:
  virtual ~TitleGenerationEngineInterface() = default;

  using TitleGenerationCallback =
      base::OnceCallback<void(CoralResult<TitleGenerationResponse>)>;
  virtual void Process(const mojom::GroupRequest& request,
                       const ClusteringResponse& clustering_response,
                       TitleGenerationCallback callback) = 0;
};

class TitleGenerationEngine : public TitleGenerationEngineInterface {
 public:
  TitleGenerationEngine();
  ~TitleGenerationEngine() = default;

  // TitleGenerationEngineInterface overrides.
  void Process(const mojom::GroupRequest& request,
               const ClusteringResponse& clustering_response,
               TitleGenerationCallback callback) override;
};

}  // namespace coral

#endif  // ODML_CORAL_TITLE_GENERATION_ENGINE_H_
