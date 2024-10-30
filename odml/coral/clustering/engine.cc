// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/coral/clustering/engine.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

#include <base/functional/callback.h>
#include <base/types/expected.h>

#include "odml/coral/clustering/agglomerative_clustering.h"
#include "odml/coral/clustering/clustering_factory.h"
#include "odml/coral/embedding/engine.h"
#include "odml/mojom/coral_service.mojom.h"

namespace coral {

namespace {

using clustering::Distance;
using mojom::CoralError;

const Distance kDefaultAgglomerativeClusteringThreshold = 0.24;

constexpr float kFloatErrorTolerance = 1e-6;

}  // namespace

namespace internal {

// Returns std::nullopt if
// 1. Length of embeddings |a| and |b| don't match.
// 2. Embeddings have zero norm.
std::optional<Distance> CosineDistance(const Embedding& a, const Embedding& b) {
  if (a.size() != b.size()) {
    LOG(ERROR) << "Embedding sizes don't match: (" << a.size() << ", "
               << b.size() << ")";
    return std::nullopt;
  }

  const int n = a.size();

  Distance dot = 0.0, norm_a = 0.0, norm_b = 0.0;
  for (int i = 0; i < n; ++i) {
    dot += a[i] * b[i];
    norm_a += a[i] * a[i];
    norm_b += b[i] * b[i];
  }
  if (norm_a == 0.0 || norm_b == 0.0) {
    LOG(ERROR) << "Embedding(s) have zero norm";
    return std::nullopt;
  }

  norm_a = sqrt(norm_a);
  norm_b = sqrt(norm_b);

  return 1 - dot / norm_a / norm_b;
}

// Returns std::nullopt if CosineDistance() returns std::nullopt.
std::optional<clustering::Matrix> DistanceMatrix(
    const std::vector<Embedding>& embeddings) {
  const int n = embeddings.size();
  // Returns distance matrix of size n * n.
  clustering::Matrix distances;
  distances.resize(n);
  for (int i = 0; i < n; ++i) {
    distances[i].resize(n);
  }

  for (int i = 0; i < n; ++i) {
    distances[i][i] = 0.0;
    for (int j = i + 1; j < n; ++j) {
      const std::optional<Distance> distance =
          CosineDistance(embeddings[i], embeddings[j]);
      if (!distance.has_value()) {
        LOG(ERROR) << "Unable to calculate distance of embeddings (" << i
                   << ", " << j << ")";
        return std::nullopt;
      }
      distances[i][j] = distances[j][i] = *distance;
    }
  }
  return distances;
}

std::optional<Embedding> CalculateVectorCenter(
    const std::vector<Embedding>& embeddings, const std::vector<int>& indices) {
  if (indices.size() < 1) {
    return std::nullopt;
  }

  // The size of the embedding vector.
  const int size = embeddings[0].size();
  Embedding center;
  center.resize(size);

  for (const int index : indices) {
    if (embeddings[index].size() != size) {
      LOG(ERROR) << "embedding sizes doesn't match: " << size << " and "
                 << embeddings[index].size();
      return std::nullopt;
    }

    // Since embeddings are consider as high dimensional vectors instead of
    // points, normalize them to unit length so the center is meaningful.
    float norm = 0.0;
    for (int i = 0; i < size; ++i) {
      norm += embeddings[index][i] * embeddings[index][i];
    }
    if (norm == 0.0) {
      LOG(ERROR) << "embedding vector of length 0";
      return std::nullopt;
    }
    norm = sqrt(norm);

    for (int i = 0; i < size; ++i) {
      center[i] += embeddings[index][i] / norm;
    }
  }
  for (int i = 0; i < size; ++i) {
    center[i] /= indices.size();
  }
  return center;
}

}  // namespace internal

ClusteringEngine::ClusteringEngine(
    raw_ref<CoralMetrics> metrics,
    std::unique_ptr<clustering::ClusteringFactoryInterface> clustering_factory)
    : metrics_(metrics), clustering_factory_(std::move(clustering_factory)) {}

void ClusteringEngine::Process(mojom::GroupRequestPtr request,
                               EmbeddingResponse embedding_response,
                               ClusteringCallback callback) {
  std::optional<clustering::Matrix> matrix =
      internal::DistanceMatrix(embedding_response.embeddings);
  if (!matrix.has_value()) {
    std::move(callback).Run(std::move(request),
                            base::unexpected(CoralError::kClusteringError));
    return;
  }
  std::unique_ptr<clustering::AgglomerativeClusteringInterface> clustering =
      clustering_factory_->NewAgglomerativeClustering(std::move(*matrix));

  std::optional<clustering::Groups> groups = clustering->Run(
      clustering::AgglomerativeClustering::LinkageType::kAverage, std::nullopt,
      kDefaultAgglomerativeClusteringThreshold);

  if (!groups.has_value()) {
    std::move(callback).Run(std::move(request),
                            base::unexpected(CoralError::kClusteringError));
    return;
  }

  // Sort groups by size in descending order.
  std::sort(groups->begin(), groups->end(),
            [](const std::vector<int>& a, const std::vector<int>& b) {
              return a.size() > b.size();
            });

  // The distance of each embedding to the center of its belonging group.
  std::vector<float> distance_to_center;
  distance_to_center.resize(embedding_response.embeddings.size());

  for (auto& group : *groups) {
    std::optional<Embedding> center =
        internal::CalculateVectorCenter(embedding_response.embeddings, group);
    if (!center.has_value()) {
      std::move(callback).Run(std::move(request),
                              base::unexpected(CoralError::kClusteringError));
      return;
    }

    for (int i = 0; i < group.size(); ++i) {
      std::optional<float> distance = internal::CosineDistance(
          *center, embedding_response.embeddings[group[i]]);
      if (!distance.has_value()) {
        LOG(ERROR) << "Failed to calcualte cosine distance to the center";
        std::move(callback).Run(std::move(request),
                                base::unexpected(CoralError::kClusteringError));
        return;
      }
      distance_to_center[group[i]] = *distance;
      VLOG(1) << "distance_to_center of " << group[i] << " : " << *distance;
    }

    // Sort by the distance to center in ascending order.
    // If the difference of the distances is small, deem them as equal and sort
    // by indices.
    std::sort(group.begin(), group.end(),
              [&distance_to_center](const int a, const int b) {
                if (fabs(distance_to_center[a] - distance_to_center[b]) <
                    kFloatErrorTolerance) {
                  return a < b;
                }
                return distance_to_center[a] < distance_to_center[b];
              });
  }

  unsigned int max_items_in_cluster =
      request->clustering_options->max_items_in_cluster;
  unsigned int min_items_in_cluster =
      request->clustering_options->min_items_in_cluster;
  unsigned int max_clusters = request->clustering_options->max_clusters;

  ClusteringResponse response;

  unsigned int num_groups = groups->size();
  // 0 means no limitation.
  if (max_clusters > 0 && max_clusters < num_groups) {
    num_groups = max_clusters;
  }
  for (int i = 0; i < num_groups; ++i) {
    const std::vector<int>& group = (*groups)[i];

    unsigned int num_items = group.size();
    // 0 means no limitation.
    // groups are sorted by descending size already. So we can exit the loop
    // now.
    if (min_items_in_cluster > 0 && num_items < min_items_in_cluster) {
      break;
    }

    // 0 means no limitation.
    if (max_items_in_cluster > 0 && max_items_in_cluster < num_items) {
      num_items = max_items_in_cluster;
    }

    Cluster cluster;
    for (int j = 0; j < num_items; ++j) {
      // Make a clone since the request are to be moved when running the
      // callback later.
      cluster.entities.push_back(request->entities[group[j]]->Clone());
    }
    response.clusters.push_back(std::move(cluster));
  }
  std::move(callback).Run(std::move(request), std::move(response));
}

}  // namespace coral
