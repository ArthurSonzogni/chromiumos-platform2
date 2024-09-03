// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/coral/clustering/agglomerative_clustering.h"

#include <cmath>

#include <base/logging.h>
#include <testing/gmock/include/gmock/gmock.h>
#include <testing/gtest/include/gtest/gtest.h>

using std::vector;
using testing::Optional;
using testing::UnorderedElementsAre;

namespace coral::clustering {

namespace {

class AgglomerativeClusteringTest : public testing::Test {
 public:
  AgglomerativeClusteringTest() {}
  ~AgglomerativeClusteringTest() {}

 protected:
  struct Point {
    int x, y;
  };

  Matrix GenDistances(const vector<Point>& points) {
    Matrix matrix;
    const int n = points.size();
    matrix.resize(n);

    for (int i = 0; i < n; ++i) {
      for (int j = 0; j < n; ++j) {
        Distance delta_x = (points[i].x - points[j].x);
        Distance delta_y = (points[i].y - points[j].y);
        matrix[i].push_back(sqrt(delta_x * delta_x + delta_y * delta_y));
      }
    }
    return matrix;
  }
};

TEST_F(AgglomerativeClusteringTest, SmallTestByNumCluster) {
  vector<Point> points = {{0, 0}, {1, 1}, {3, 0}, {4, 5}, {6, 0}};
  AgglomerativeClustering clustering(GenDistances(points));
  std::optional<Groups> groups;

  groups = clustering.Run(AgglomerativeClustering::LinkageType::kAverage, 1,
                          std::nullopt);
  EXPECT_THAT(
      groups,
      Optional(UnorderedElementsAre(UnorderedElementsAre(0, 1, 2, 3, 4))));

  groups = clustering.Run(AgglomerativeClustering::LinkageType::kAverage, 2,
                          std::nullopt);
  EXPECT_THAT(groups,
              Optional(UnorderedElementsAre(UnorderedElementsAre(3),
                                            UnorderedElementsAre(0, 1, 2, 4))));

  groups = clustering.Run(AgglomerativeClustering::LinkageType::kAverage, 3,
                          std::nullopt);
  EXPECT_THAT(groups, Optional(UnorderedElementsAre(
                          UnorderedElementsAre(3), UnorderedElementsAre(4),
                          UnorderedElementsAre(0, 1, 2))));

  groups = clustering.Run(AgglomerativeClustering::LinkageType::kAverage, 4,
                          std::nullopt);
  EXPECT_THAT(groups,
              Optional(UnorderedElementsAre(
                  UnorderedElementsAre(2), UnorderedElementsAre(3),
                  UnorderedElementsAre(4), UnorderedElementsAre(0, 1))));

  groups = clustering.Run(AgglomerativeClustering::LinkageType::kAverage, 5,
                          std::nullopt);
  EXPECT_THAT(groups, Optional(UnorderedElementsAre(
                          UnorderedElementsAre(0), UnorderedElementsAre(1),
                          UnorderedElementsAre(2), UnorderedElementsAre(3),
                          UnorderedElementsAre(4))));
}

TEST_F(AgglomerativeClusteringTest, SmallTestByThreshold) {
  vector<Point> points = {{0, 0}, {1, 1}, {3, 0}, {4, 5}, {6, 0}};

  AgglomerativeClustering clustering(GenDistances(points));
  std::optional<Groups> groups;

  groups = clustering.Run(AgglomerativeClustering::LinkageType::kAverage,
                          std::nullopt, 1);
  EXPECT_THAT(groups, Optional(UnorderedElementsAre(
                          UnorderedElementsAre(0), UnorderedElementsAre(1),
                          UnorderedElementsAre(2), UnorderedElementsAre(3),
                          UnorderedElementsAre(4))));

  groups = clustering.Run(AgglomerativeClustering::LinkageType::kAverage,
                          std::nullopt, 2);
  EXPECT_THAT(groups, Optional(UnorderedElementsAre(
                          UnorderedElementsAre(0, 1), UnorderedElementsAre(2),
                          UnorderedElementsAre(3), UnorderedElementsAre(4))));

  groups = clustering.Run(AgglomerativeClustering::LinkageType::kAverage,
                          std::nullopt, 3);
  EXPECT_THAT(groups, Optional(UnorderedElementsAre(
                          UnorderedElementsAre(0, 1, 2),
                          UnorderedElementsAre(3), UnorderedElementsAre(4))));

  groups = clustering.Run(AgglomerativeClustering::LinkageType::kAverage,
                          std::nullopt, 5);
  EXPECT_THAT(groups,
              Optional(UnorderedElementsAre(UnorderedElementsAre(0, 1, 2, 4),
                                            UnorderedElementsAre(3))));

  groups = clustering.Run(AgglomerativeClustering::LinkageType::kAverage,
                          std::nullopt, 6);
  EXPECT_THAT(
      groups,
      Optional(UnorderedElementsAre(UnorderedElementsAre(0, 1, 3, 2, 4))));
}

TEST_F(AgglomerativeClusteringTest, BigTestTestByNumCluster) {
  vector<Point> points = {{46, 83}, {6, 81},  {8, 91},  {86, 83}, {28, 55},
                          {86, 45}, {33, 36}, {61, 57}, {58, 10}, {66, 93},
                          {97, 45}, {35, 6},  {80, 38}, {38, 46}, {6, 42},
                          {81, 99}, {98, 38}, {8, 43},  {47, 8},  {9, 98}};
  AgglomerativeClustering clustering(GenDistances(points));
  std::optional<Groups> groups;

  groups = clustering.Run(AgglomerativeClustering::LinkageType::kAverage, 2,
                          std::nullopt);
  EXPECT_THAT(
      groups,
      Optional(UnorderedElementsAre(
          UnorderedElementsAre(0, 3, 5, 7, 9, 10, 12, 15, 16),
          UnorderedElementsAre(1, 2, 4, 6, 8, 11, 13, 14, 17, 18, 19))));

  groups = clustering.Run(AgglomerativeClustering::LinkageType::kAverage, 5,
                          std::nullopt);
  EXPECT_THAT(groups, Optional(UnorderedElementsAre(
                          UnorderedElementsAre(0, 3, 7, 9, 15),
                          UnorderedElementsAre(4, 6, 13, 14, 17),
                          UnorderedElementsAre(1, 2, 19),
                          UnorderedElementsAre(5, 10, 12, 16),
                          UnorderedElementsAre(8, 11, 18))));

  groups = clustering.Run(AgglomerativeClustering::LinkageType::kAverage, 10,
                          std::nullopt);
  EXPECT_THAT(
      groups,
      Optional(UnorderedElementsAre(
          UnorderedElementsAre(4, 6, 13), UnorderedElementsAre(9, 15),
          UnorderedElementsAre(1, 2, 19), UnorderedElementsAre(5, 10, 12, 16),
          UnorderedElementsAre(11), UnorderedElementsAre(8, 18),
          UnorderedElementsAre(0), UnorderedElementsAre(14, 17),
          UnorderedElementsAre(3), UnorderedElementsAre(7))));
}

TEST_F(AgglomerativeClusteringTest, BigTestTestByThreshold) {
  vector<Point> points = {{46, 83}, {6, 81},  {8, 91},  {86, 83}, {28, 55},
                          {86, 45}, {33, 36}, {61, 57}, {58, 10}, {66, 93},
                          {97, 45}, {35, 6},  {80, 38}, {38, 46}, {6, 42},
                          {81, 99}, {98, 38}, {8, 43},  {47, 8},  {9, 98}};
  AgglomerativeClustering clustering(GenDistances(points));
  std::optional<Groups> groups;

  groups = clustering.Run(AgglomerativeClustering::LinkageType::kAverage,
                          std::nullopt, 15);
  EXPECT_THAT(groups,
              Optional(UnorderedElementsAre(
                  UnorderedElementsAre(1, 2, 19), UnorderedElementsAre(6, 13),
                  UnorderedElementsAre(8, 18), UnorderedElementsAre(14, 17),
                  UnorderedElementsAre(11), UnorderedElementsAre(5, 12),
                  UnorderedElementsAre(0), UnorderedElementsAre(15),
                  UnorderedElementsAre(3), UnorderedElementsAre(7),
                  UnorderedElementsAre(4), UnorderedElementsAre(9),
                  UnorderedElementsAre(10, 16))));

  groups = clustering.Run(AgglomerativeClustering::LinkageType::kAverage,
                          std::nullopt, 35);
  EXPECT_THAT(
      groups,
      Optional(UnorderedElementsAre(
          UnorderedElementsAre(0, 7), UnorderedElementsAre(4, 6, 13, 14, 17),
          UnorderedElementsAre(3, 9, 15), UnorderedElementsAre(5, 10, 12, 16),
          UnorderedElementsAre(8, 11, 18), UnorderedElementsAre(1, 2, 19))));

  groups = clustering.Run(AgglomerativeClustering::LinkageType::kAverage,
                          std::nullopt, 55);
  EXPECT_THAT(groups, Optional(UnorderedElementsAre(
                          UnorderedElementsAre(0, 3, 5, 7, 9, 10, 12, 15, 16),
                          UnorderedElementsAre(4, 6, 8, 11, 13, 14, 17, 18),
                          UnorderedElementsAre(1, 2, 19))));
}

TEST_F(AgglomerativeClusteringTest, BadParameters) {
  vector<Point> points = {{0, 0}, {1, 1}, {3, 0}, {4, 5}, {6, 0}};

  AgglomerativeClustering clustering(GenDistances(points));
  std::optional<Groups> groups;

  // n_clusters > 5.
  groups = clustering.Run(AgglomerativeClustering::LinkageType::kAverage, 6,
                          std::nullopt);
  EXPECT_FALSE(groups.has_value());

  // n_clusters < 0.
  groups = clustering.Run(AgglomerativeClustering::LinkageType::kAverage, -1,
                          std::nullopt);
  EXPECT_FALSE(groups.has_value());

  // threshold < 0.
  groups = clustering.Run(AgglomerativeClustering::LinkageType::kAverage,
                          std::nullopt, -3);
  EXPECT_FALSE(groups.has_value());

  // Both n_clusters and threshold are missing.
  groups = clustering.Run(AgglomerativeClustering::LinkageType::kAverage,
                          std::nullopt, std::nullopt);
  EXPECT_FALSE(groups.has_value());

  // Both n_clusters and threshold are given.
  groups = clustering.Run(AgglomerativeClustering::LinkageType::kAverage, 5, 3);
  EXPECT_FALSE(groups.has_value());
}

TEST_F(AgglomerativeClusteringTest, OnePoint) {
  vector<Point> points = {{0, 0}};

  AgglomerativeClustering clustering(GenDistances(points));
  std::optional<Groups> groups;

  groups = clustering.Run(AgglomerativeClustering::LinkageType::kAverage, 1,
                          std::nullopt);
  EXPECT_THAT(groups, Optional(UnorderedElementsAre(UnorderedElementsAre(0))));

  groups = clustering.Run(AgglomerativeClustering::LinkageType::kAverage,
                          std::nullopt, 3);
  EXPECT_THAT(groups, Optional(UnorderedElementsAre(UnorderedElementsAre(0))));
}

TEST_F(AgglomerativeClusteringTest, ZeroPoint) {
  vector<Point> points = {};

  AgglomerativeClustering clustering(GenDistances(points));
  std::optional<Groups> groups;

  groups = clustering.Run(AgglomerativeClustering::LinkageType::kAverage, 0,
                          std::nullopt);
  EXPECT_THAT(groups, Optional(UnorderedElementsAre()));
}

}  // namespace
}  // namespace coral::clustering
