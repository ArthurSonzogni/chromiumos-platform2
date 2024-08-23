// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_CORAL_CLUSTERING_ENGINE_H_
#define ODML_CORAL_CLUSTERING_ENGINE_H_

#include <vector>

#include <base/functional/callback.h>

#include "odml/coral/common.h"
#include "odml/coral/embedding/engine.h"
#include "odml/mojom/coral_service.mojom.h"

namespace coral {

struct Cluster {
  bool operator==(const Cluster&) const = default;
  std::vector<mojom::EntityPtr> entities;
};

struct ClusteringResponse : public MoveOnly {
  bool operator==(const ClusteringResponse&) const = default;

  // Sorted by descending rank of importance.
  std::vector<Cluster> clusters;
};

class ClusteringEngineInterface {
 public:
  virtual ~ClusteringEngineInterface() = default;

  using ClusteringCallback =
      base::OnceCallback<void(CoralResult<ClusteringResponse>)>;
  virtual void Process(const mojom::GroupRequest& request,
                       const EmbeddingResponse& embedding_response,
                       ClusteringCallback callback) = 0;
};

class ClusteringEngine : public ClusteringEngineInterface {
 public:
  ClusteringEngine();
  ~ClusteringEngine() = default;

  using ClusteringCallback =
      base::OnceCallback<void(CoralResult<ClusteringResponse>)>;
  void Process(const mojom::GroupRequest& request,
               const EmbeddingResponse& embedding_response,
               ClusteringCallback callback) override;
};

}  // namespace coral

#endif  // ODML_CORAL_CLUSTERING_ENGINE_H_
