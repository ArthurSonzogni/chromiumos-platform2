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
#include <base/types/expected_macros.h>

#include "odml/coral/clustering/agglomerative_clustering.h"
#include "odml/coral/clustering/clustering_factory.h"
#include "odml/coral/common.h"
#include "odml/coral/embedding/engine.h"
#include "odml/mojom/coral_service.mojom.h"

namespace coral {

namespace {

using clustering::Distance;
using mojom::CoralError;

const Distance kDefaultAgglomerativeClusteringThreshold = 0.20;

constexpr float kFloatErrorTolerance = 1e-6;

// If the proportion of entities in suppression_context that can be found in a
// group equals or exceeds kSuppressionContextThreshold, then that group should
// be skipped.
// 0.499 is used instead of 0.5 because at precisely half the elements present,
// we want to suppress the group.
constexpr double kSuppressionContextThreshold = 0.499;

// Returns std::nullopt if length of embeddings |a| and |b| don't match.
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
    LOG(WARNING) << "Embedding(s) have zero norm";
    // Return the maximum distance as it's not possible to calculate a valid
    // consine distance when 1 of the vectors is zero.
    return 2.0;
  }

  norm_a = sqrt(norm_a);
  norm_b = sqrt(norm_b);

  return 1 - dot / norm_a / norm_b;
}

// FilterEmptyEmbeddings filters out empty embeddings from the input vector. It
// populates out_embeddings with non-empty embeddings and, if original_indexes
// is provided, stores the original indexes plus index_offset) of the non-empty
// embeddings.
void FilterEmptyEmbeddings(std::vector<Embedding> embeddings,
                           size_t index_offset,
                           std::vector<Embedding>& out_embeddings,
                           std::vector<size_t>* original_indexes) {
  for (size_t i = 0; i < embeddings.size(); ++i) {
    if (embeddings[i].empty()) {
      continue;
    }
    out_embeddings.push_back(std::move(embeddings[i]));
    if (original_indexes) {
      original_indexes->push_back(i + index_offset);
    }
  }
}

// IsGroupSuppressed determines if a group should be suppressed,
// and returns true if it should be. It does so by taking a group, assuming any
// index on and after suppression_context_start is part of the
// suppression_context, then calculate the proportion of suppression context
// that can be found in this group, it it exceeds the
// kSuppressionContextThreshold, then it'll be suppressed.
bool IsGroupSuppressed(const std::vector<int>& group,
                       size_t suppression_context_start,
                       size_t suppression_context_size) {
  if (suppression_context_size == 0) {
    // If no existing entities are supplied, then this group is not part of the
    // existing.
    return false;
  }

  int suppression_context_count = 0;
  for (int idx : group) {
    if (idx >= suppression_context_start) {
      suppression_context_count++;
    }
  }

  return static_cast<double>(suppression_context_count) /
             static_cast<double>(suppression_context_size) >=
         kSuppressionContextThreshold;
}

// FilterSuppressionContextFromGroups filters the supplied groups and removes
// all groups that should be suppressed according to suppression context.
void FilterSuppressionContextFromGroups(clustering::Groups& groups,
                                        size_t suppression_context_start,
                                        size_t suppression_context_size) {
  std::erase_if(groups, [suppression_context_start,
                         suppression_context_size](const std::vector<int>& g) {
    return IsGroupSuppressed(g, suppression_context_start,
                             suppression_context_size);
  });
}

// RemoveSuppressionItemsFromGroups removes any entry from the groups that is
// from the suppression context. After this call, all index in groups is
// guaranteed to be from the original entities.
void RemoveSuppressionItemsFromGroups(clustering::Groups& groups,
                                      size_t suppression_context_start) {
  for (auto& g : groups) {
    std::erase_if(g, [suppression_context_start](int x) {
      return x >= suppression_context_start;
    });
  }
}

// Filters the groups and produces the final resulting group that matches the
// requirements in clustering_options.
// The input groups must have been sorted by their group size in descending
// order and have each group sorted internally by distance to group center.
std::vector<ClusteringEngine::IndexGroup> FilterGroupsByOptions(
    clustering::Groups& groups,
    const mojom::ClusteringOptions& clustering_options) {
  unsigned int max_items_in_cluster = clustering_options.max_items_in_cluster;
  unsigned int min_items_in_cluster = clustering_options.min_items_in_cluster;
  unsigned int max_clusters = clustering_options.max_clusters;

  std::vector<ClusteringEngine::IndexGroup> result;
  unsigned int num_groups = groups.size();
  // 0 means no limitation.
  if (max_clusters > 0 && max_clusters < num_groups) {
    num_groups = max_clusters;
  }
  for (int i = 0; i < num_groups; ++i) {
    const ClusteringEngine::IndexGroup& group = groups[i];

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

    result.emplace_back(group.begin(), group.begin() + num_items);
  }

  return result;
}

// For each of the group, sort their elements by the distance between the
// element and the group's center. The ordering of the groups does not change.
// Given that this method accepts groups that holds any index that is consistent
// with valid_embeddings, the indices in groups must be smaller than size of
// valid_embeddings. In case of error, there are no guarantee in groups state.
// That is, this is error-unsafe.
CoralStatus SortWithinClusterGroup(
    clustering::Groups& groups,
    const std::vector<Embedding>& valid_embeddings) {
  // The distance of each embedding to the center of its belonging group.
  std::vector<float> distance_to_center;
  distance_to_center.resize(valid_embeddings.size());

  for (auto& group : groups) {
    std::optional<Embedding> center =
        internal::CalculateVectorCenter(valid_embeddings, group);
    if (!center.has_value()) {
      return base::unexpected(CoralError::kClusteringError);
    }

    for (int i = 0; i < group.size(); ++i) {
      std::optional<float> distance =
          CosineDistance(*center, valid_embeddings[group[i]]);
      if (!distance.has_value()) {
        LOG(ERROR) << "Failed to calcualte cosine distance to the center";
        return base::unexpected(CoralError::kClusteringError);
      }
      CHECK_LT(group[i], distance_to_center.size());
      distance_to_center[group[i]] = *distance;
      VLOG(1) << "distance_to_center of " << group[i] << " : " << *distance;
    }

    // Sort by the distance to center in ascending order.
    // If the difference of the distances is small, deem them as equal and sort
    // by indices.
    // Note that no stable sort is needed as the ordering from clustering
    // algorithm is not taken into consideration.
    std::ranges::sort(group, [&distance_to_center](const int a, const int b) {
      CHECK_LT(a, distance_to_center.size());
      CHECK_LT(b, distance_to_center.size());
      if (fabs(distance_to_center[a] - distance_to_center[b]) <
          kFloatErrorTolerance) {
        return a < b;
      }
      return distance_to_center[a] < distance_to_center[b];
    });
  }
  return base::ok();
}

// Sort the groups by the number of indices in each group, in descending order.
CoralStatus SortGroupsBySize(clustering::Groups& groups) {
  // Sort groups by size in descending order.
  // Note that no stable sort is needed as the ordering from clustering
  // algorithm is not taken into consideration.
  std::ranges::sort(groups, [](const ClusteringEngine::IndexGroup& a,
                               const ClusteringEngine::IndexGroup& b) {
    return a.size() > b.size();
  });
  return base::ok();
}

}  // namespace

namespace internal {

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
    const std::vector<Embedding>& embeddings,
    const ClusteringEngine::IndexGroup& indices) {
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

CoralResult<std::vector<ClusteringEngine::IndexGroup>>
ClusteringEngine::ProcessContiguous(
    const std::vector<Embedding>& valid_embeddings,
    const mojom::ClusteringOptions& clustering_options,
    size_t suppression_context_start) {
  CHECK(valid_embeddings.size() >= suppression_context_start);
  size_t suppression_context_size =
      valid_embeddings.size() - suppression_context_start;

  std::optional<clustering::Matrix> matrix =
      internal::DistanceMatrix(valid_embeddings);
  if (!matrix.has_value()) {
    return base::unexpected(CoralError::kClusteringError);
  }

  std::unique_ptr<clustering::AgglomerativeClusteringInterface> clustering =
      clustering_factory_->NewAgglomerativeClustering(
          std::move(matrix.value()));

  std::optional<clustering::Groups> groups = clustering->Run(
      clustering::AgglomerativeClustering::LinkageType::kAverage, std::nullopt,
      kDefaultAgglomerativeClusteringThreshold);

  if (!groups.has_value()) {
    return base::unexpected(CoralError::kClusteringError);
  }

  FilterSuppressionContextFromGroups(groups.value(), suppression_context_start,
                                     suppression_context_size);

  RemoveSuppressionItemsFromGroups(groups.value(), suppression_context_start);

  RETURN_IF_ERROR(SortWithinClusterGroup(groups.value(), valid_embeddings));

  RETURN_IF_ERROR(SortGroupsBySize(groups.value()));

  unsigned int min_items_in_cluster = clustering_options.min_items_in_cluster;
  // Report metrics for all groups. For the GeneratedGroupCount metric, we
  // report all groups that are valid (>= min_items_in_cluster). For the
  // GroupItemCount metric, we send it for all groups, without any restrictions.
  int valid_groups = 0;
  for (const IndexGroup& group : *groups) {
    metrics_->SendGroupItemCount(group.size());
    if (group.size() >= min_items_in_cluster) {
      valid_groups++;
    }
  }
  metrics_->SendGeneratedGroupCount(valid_groups);

  return base::ok(FilterGroupsByOptions(groups.value(), clustering_options));
}

CoralResult<std::vector<ClusteringEngine::IndexGroup>>
ClusteringEngine::ProcessInternal(
    const mojom::GroupRequest& request,
    std::vector<Embedding> embeddings,
    std::vector<Embedding> suppression_context_embeddings) {
  // Some entries might not have successfully generated embeddings. They'll be
  // marked as empty by the embedding engine.
  std::vector<Embedding> valid_embeddings;
  std::vector<size_t> original_indexes;
  // We need this here because embeddings is consumed by
  // FilterEmptyEmbeddings().
  const size_t suppression_context_original_start = embeddings.size();
  FilterEmptyEmbeddings(std::move(embeddings),
                        /*index_offset=*/0,
                        /*out_embeddings=*/valid_embeddings, &original_indexes);
  metrics_->SendEmbeddingFilteredCount(request.entities.size() -
                                       valid_embeddings.size());
  const size_t suppression_context_start = valid_embeddings.size();
  FilterEmptyEmbeddings(std::move(suppression_context_embeddings),
                        /*index_offset=*/suppression_context_original_start,
                        /*out_embeddings=*/valid_embeddings,
                        /*original_indexes=*/nullptr);

  metrics_->SendClusteringInputCount(valid_embeddings.size());

  std::vector<IndexGroup> result;
  ASSIGN_OR_RETURN(
      result, ProcessContiguous(valid_embeddings, *request.clustering_options,
                                suppression_context_start));
  for (int i = 0; i < result.size(); i++) {
    for (int j = 0; j < result[i].size(); j++) {
      result[i][j] = original_indexes[result[i][j]];
    }
  }
  return base::ok(result);
}

void ClusteringEngine::Process(
    mojom::GroupRequestPtr request,
    EmbeddingResponse embedding_response,
    EmbeddingResponse suppression_context_embedding_response,
    ClusteringCallback callback) {
  auto timer = odml::PerformanceTimer::Create();

  std::vector<Embedding> embeddings;
  for (EmbeddingWithMetadata& embedding : embedding_response.embeddings) {
    embeddings.push_back(std::move(embedding.embedding));
  }
  std::vector<Embedding> suppression_context_embedding;
  for (EmbeddingWithMetadata& embedding :
       suppression_context_embedding_response.embeddings) {
    suppression_context_embedding.push_back(std::move(embedding.embedding));
  }

  CoralResult<std::vector<IndexGroup>> result =
      ClusteringEngine::ProcessInternal(
          *request, std::move(embeddings),
          std::move(suppression_context_embedding));

  if (result.has_value()) {
    metrics_->SendClusteringEngineLatency(timer->GetDuration());
    metrics_->SendClusteringEngineStatus(base::ok());

    ClusteringResponse response;
    for (const auto& group : result.value()) {
      Cluster cluster;
      for (int index : group) {
        cluster.entities.push_back(
            {.entity = request->entities[index]->Clone(),
             .language_result = std::move(
                 embedding_response.embeddings[index].language_result)});
      }
      response.clusters.push_back(std::move(cluster));
    }
    std::move(callback).Run(std::move(request), std::move(response));
  } else {
    metrics_->SendClusteringEngineStatus(base::unexpected(result.error()));
    std::move(callback).Run(std::move(request),
                            base::unexpected(result.error()));
  }
}

}  // namespace coral
