// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_CORAL_CLUSTERING_AGGLOMERATIVE_CLUSTERING_H_
#define ODML_CORAL_CLUSTERING_AGGLOMERATIVE_CLUSTERING_H_

#include <optional>
#include <vector>

namespace coral::clustering {

// Type for the distance.
using Distance = float;

// Input type: 2D matrix.
using Matrix = std::vector<std::vector<Distance>>;

// Output type: Groups of indices.
using Groups = std::vector<std::vector<int>>;

// Agglomerative clustering is a hierarchical clustering algorithm.
// Initially, each input node is in its own group. In each round, the pair of
// nodes with the minimum distances are merged into a new node.
// The definition of distance is determined by the linkage type.
// https://en.wikipedia.org/wiki/Hierarchical_clustering
class AgglomerativeClustering {
 public:
  enum class LinkageType {
    // Support other linkage like Single, Complete, Ward when needed.
    kAverage = 0,
  };

  // The input should be a N * N matrix, Crashes if the dimensions don't match.
  explicit AgglomerativeClustering(Matrix distances);
  ~AgglomerativeClustering() = default;

  // Exactly one of |n_clusters| or |threshold| should have value.
  //
  // |n_clusters| specifies the desired number of groups in the output.
  //
  // |threshold| specifies the max distance for pairs of groups to be merged.
  // In the case,the number of final groups is determined solely by |threshold|.
  //
  // When error happens (e.g., wrong input parameter), it returns std::nullopt.
  std::optional<Groups> Run(LinkageType linkage_type,
                            std::optional<int> n_clusters,
                            std::optional<Distance> threshold) const;

 private:
  const Matrix distances_;
};

}  // namespace coral::clustering

#endif  // ODML_CORAL_CLUSTERING_AGGLOMERATIVE_CLUSTERING_H_
