// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_CORAL_CLUSTERING_ENGINE_H_
#define ODML_CORAL_CLUSTERING_ENGINE_H_

#include <memory>
#include <vector>

#include <base/functional/callback.h>

#include "odml/coral/clustering/agglomerative_clustering.h"
#include "odml/coral/clustering/clustering_factory.h"
#include "odml/coral/common.h"
#include "odml/coral/embedding/engine.h"
#include "odml/coral/metrics.h"
#include "odml/mojom/coral_service.mojom.h"

namespace coral {

namespace internal {

// Calculate the distance matrix for a list of embeddings.
// It always returns a matrix by N*N.
std::optional<clustering::Matrix> DistanceMatrix(
    const std::vector<Embedding>& embeddings);

// Given a list of embedding vectors, calculate the center vector
// of a subset specified by |indices|.
// std::nullopt is returned if
// 1. The dimensions of vectors are not consistent.
// 2. Any vector has zero length.
// 3. The subset |indices| is empty.
std::optional<Embedding> CalculateVectorCenter(
    const std::vector<Embedding>& embeddings, const std::vector<int>& indices);

}  // namespace internal

struct Cluster : public MoveOnly {
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

  using ClusteringCallback = base::OnceCallback<void(
      mojom::GroupRequestPtr, CoralResult<ClusteringResponse>)>;
  virtual void Process(mojom::GroupRequestPtr request,
                       EmbeddingResponse embedding_response,
                       ClusteringCallback callback) = 0;
};

class ClusteringEngine : public ClusteringEngineInterface {
 public:
  ClusteringEngine(raw_ref<CoralMetrics> metrics,
                   std::unique_ptr<clustering::ClusteringFactoryInterface>
                       clustering_factory);
  ~ClusteringEngine() = default;

  void Process(mojom::GroupRequestPtr request,
               EmbeddingResponse embedding_response,
               ClusteringCallback callback) override;

 private:
  // Report metrics and return to callback.
  void HandleProcessResult(ClusteringCallback callback,
                           PerformanceTimer::Ptr timer,
                           mojom::GroupRequestPtr request,
                           CoralResult<ClusteringResponse> result);

  const raw_ref<CoralMetrics> metrics_;
  std::unique_ptr<clustering::ClusteringFactoryInterface> clustering_factory_;

  base::WeakPtrFactory<ClusteringEngine> weak_ptr_factory_{this};
};

}  // namespace coral

#endif  // ODML_CORAL_CLUSTERING_ENGINE_H_
