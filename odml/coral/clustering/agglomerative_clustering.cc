// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/coral/clustering/agglomerative_clustering.h"

#include <cmath>
#include <memory>
#include <queue>
#include <unordered_map>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/check_op.h>
#include <base/logging.h>

namespace coral::clustering {

namespace {

// Represents the distance from one node to other nodes in the form of
// (node_id, distance).
using DistanceMap = std::unordered_map<int, Distance>;

// A tree representing the dendrogram.
class TreeNode {
 public:
  TreeNode(TreeNode* left, TreeNode* right, const int id)
      : left_(left), right_(right), id_(id), active_(true) {}
  ~TreeNode() = default;

  // Merge this node to another one. Update all pairs of distances for all
  // |nodes|.
  std::unique_ptr<TreeNode> Merge(TreeNode* rhs, const int new_node_id) {
    active_ = false;
    rhs->active_ = false;

    return std::make_unique<TreeNode>(this, rhs, new_node_id);
  }

  // Returns ID of all leaf nodes rooted at this node.
  void GetIDs(std::vector<int>& group) const {
    if (!left_ && !right_) {
      group.push_back(id_);
    }
    if (left_) {
      left_->GetIDs(group);
    }
    if (right_) {
      right_->GetIDs(group);
    }
  }

  int get_id() const { return id_; }

  int is_active() const { return active_; }

 private:
  // A binary tree.
  TreeNode* const left_;
  TreeNode* const right_;
  // Node id.
  const int id_;
  // Whether this node has not been merged yet.
  bool active_;
};

// A list of nodes.
using NodeList = std::vector<std::unique_ptr<TreeNode>>;

class LinkageBase {
 public:
  explicit LinkageBase(const Matrix& distances) : distances_(distances) {}
  virtual ~LinkageBase() = default;

  // Merge |node_1_id| and |node_1_id| to |new_node_id|.
  // Update the distance matrix.
  virtual void Merge(int node_1_id,
                     int node_2_id,
                     int new_node_id,
                     const NodeList& nodes) = 0;

  // Returns the distances from |node_id| to all active nodes whose id <|
  // node_id|. It is one-sided since the distances is symmetric.
  virtual DistanceMap GetDistances(int node_id,
                                   const NodeList& nodes) const = 0;

 protected:
  Matrix distances_;
};

// Average linkage means the average of all pairs of distances from leaves in
// sub tree A to leaves in sub tree B.
class LinkageAverage : public LinkageBase {
 public:
  explicit LinkageAverage(const Matrix& distances) : LinkageBase(distances) {
    const int n = distances_.size();
    // There are at most 2*n nodes during the process.
    // Resize all the vectors to avoid re-allocation.
    sizes_.resize(2 * n);
    distances_.resize(2 * n);

    for (int i = 0; i < 2 * n; ++i) {
      // Resize the 2D distance matrix.
      distances_[i].resize(2 * n);
    }

    // Initially, each leaf node has size 1.
    for (int i = 0; i < n; ++i) {
      sizes_[i] = 1;
    }
  }
  ~LinkageAverage() = default;

  void Merge(const int node_1_id,
             const int node_2_id,
             const int new_node_id,
             const NodeList& nodes) override {
    sizes_[new_node_id] = sizes_[node_1_id] + sizes_[node_2_id];

    for (int i = 0; i < new_node_id; ++i) {
      // Skip calculating distances of already merged nodes.
      if (!nodes[i]->is_active()) {
        continue;
      }

      // In average linkage, |distances_map| records the sum of all pairs of
      // distances from a node to another node.
      const Distance sum = distances_[node_1_id][i] + distances_[node_2_id][i];
      distances_[i][new_node_id] = sum;
      distances_[new_node_id][i] = sum;
    }
  }

  DistanceMap GetDistances(const int node_id,
                           const NodeList& nodes) const override {
    CHECK_NE(sizes_[node_id], 0);

    DistanceMap distances_map;
    for (int i = 0; i < node_id; ++i) {
      CHECK_NE(sizes_[i], 0);
      if (!nodes[i]->is_active()) {
        continue;
      }
      // Average distance = sum of all pair of distances / number of pairs.
      distances_map[i] = distances_[node_id][i] / sizes_[node_id] / sizes_[i];
    }
    return distances_map;
  }

 private:
  // Needed to calculate the average distances.
  std::vector<int> sizes_;
};

// Get all the groups from the forest.
Groups GetGroups(const NodeList& nodes) {
  Groups groups;
  for (const auto& node : nodes) {
    // active means it is a root node.
    if (node->is_active()) {
      std::vector<int> group;

      node->GetIDs(group);
      groups.push_back(group);
    }
  }
  return groups;
}

// Represents a pair of nodes and their distance.
struct QueueNode {
  Distance value_;
  TreeNode* node_1;
  TreeNode* node_2;
};

}  // namespace

AgglomerativeClustering::AgglomerativeClustering(Matrix distances)
    : distances_(std::move(distances)) {
  for (const std::vector<Distance>& distance : distances) {
    CHECK_EQ(distances.size(), distance.size());
  }
}

std::optional<Groups> AgglomerativeClustering::Run(
    LinkageType linkage_type,
    std::optional<int> n_clusters,
    std::optional<Distance> threshold) const {
  NodeList nodes;
  // A min priority queue.
  std::priority_queue<QueueNode, std::vector<QueueNode>,
                      decltype([](const QueueNode& a, const QueueNode& b) {
                        return a.value_ > b.value_;
                      })>
      queue;

  const int n = distances_.size();

  DLOG(INFO) << "Start grouping with size: " << n;

  if (!(n_clusters.has_value() ^ threshold.has_value())) {
    LOG(ERROR) << "Exactly one of n_clusters or threshold should be given.";
    return std::nullopt;
  }

  if (n_clusters.has_value()) {
    DLOG(INFO) << "n_clusters: " << *n_clusters;
    if (*n_clusters < 0 || *n_clusters > n) {
      LOG(ERROR) << "Bad number of n_clusters: " << *n_clusters;
      return std::nullopt;
    }
  }

  if (threshold.has_value()) {
    DLOG(INFO) << "threhsold: " << *threshold;
    if (*threshold < 0) {
      LOG(ERROR) << "Bad threshold: " << *threshold;
      return std::nullopt;
    }
  }

  // There are at most n*2 nodes. Set the capacity so there would be no
  // re-allocation during the process.
  nodes.reserve(n * 2);

  // Initialization.
  // Build up all the leaf nodes.
  for (int i = 0; i < n; ++i) {
    nodes.push_back(std::make_unique<TreeNode>(nullptr, nullptr, i));
  }

  // Push all pairs of distances into the priority queue.
  for (int i = 0; i < n; ++i) {
    for (int j = 0; j < i; ++j) {
      DLOG(INFO) << "Adding (" << j << ", " << i
                 << "), value: " << distances_[j][i];
      queue.push(QueueNode{
          .value_ = distances_[j][i],
          .node_1 = nodes[j].get(),
          .node_2 = nodes[i].get(),
      });
    }
  }

  std::unique_ptr<LinkageBase> linkage;
  switch (linkage_type) {
    case LinkageType::kAverage:
      linkage = std::make_unique<LinkageAverage>(distances_);
      break;
    default:
      LOG(ERROR) << "Unimplemented linkage";
      return std::nullopt;
  }

  while (!queue.empty()) {
    // Current number of groups.
    const int num_groups = n - (nodes.size() - n);
    if (n_clusters.has_value() && num_groups <= *n_clusters) {
      DLOG(INFO) << "Met n_clusters, break";
      break;
    }

    const QueueNode selected = queue.top();
    queue.pop();

    const int node_1_id = selected.node_1->get_id();
    const int node_2_id = selected.node_2->get_id();
    DLOG(INFO) << "Min distance (" << node_1_id << ", " << node_2_id
               << "), value: " << selected.value_;
    if (!selected.node_1->is_active() || !selected.node_2->is_active()) {
      continue;
    }

    if (threshold.has_value() && selected.value_ > *threshold) {
      DLOG(INFO) << "Exceeds threshold, break";
      break;
    }

    const int new_node_id = nodes.size();
    CHECK_LT(new_node_id, 2 * n);

    DLOG(INFO) << "Merging (" << node_1_id << ", " << node_2_id << ") as "
               << new_node_id;
    nodes.push_back(selected.node_1->Merge(selected.node_2, new_node_id));

    linkage->Merge(node_1_id, node_2_id, new_node_id, nodes);

    // Add pairs of distances from the new node to all unmerged nodes.
    const DistanceMap new_distances = linkage->GetDistances(new_node_id, nodes);
    for (const auto& [id, distance] : new_distances) {
      DLOG(INFO) << "Adding (" << id << ", " << new_node_id
                 << "), value: " << distance;
      queue.push(QueueNode{
          .value_ = distance,
          .node_1 = nodes[id].get(),
          .node_2 = nodes[new_node_id].get(),
      });
    }
  }

  return GetGroups(nodes);
}

}  // namespace coral::clustering
