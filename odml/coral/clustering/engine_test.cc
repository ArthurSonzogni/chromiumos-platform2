// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/coral/clustering/engine.h"

#include <memory>
#include <string>
#include <utility>

#include <base/strings/stringprintf.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <metrics/metrics_library_mock.h>
#include <mojo/core/embedder/embedder.h>

#include "odml/coral/clustering/mock_agglomerative_clustering.h"
#include "odml/coral/clustering/mock_clustering_factory.h"
#include "odml/coral/metrics.h"
#include "odml/coral/test_util.h"
#include "odml/mojom/coral_service.mojom.h"

namespace coral {

namespace {

using base::test::TestFuture;
using testing::_;
using testing::Matcher;
using testing::MatchResultListener;
using testing::NiceMock;
using testing::Return;

// Creates fake response with the given grouping.
ClusteringResponse FakeClusteringResponse(
    const std::vector<mojom::EntityPtr>& entities,
    const clustering::Groups& groups) {
  ClusteringResponse response;
  for (int i = 0; i < groups.size(); ++i) {
    Cluster cluster;
    for (int j = 0; j < groups[i].size(); ++j) {
      cluster.entities.push_back(entities[groups[i][j]]->Clone());
    }
    response.clusters.push_back(std::move(cluster));
  }
  return response;
}

std::string EntityToString(const mojom::Entity& entity) {
  if (entity.is_app()) {
    const mojom::App& app = *entity.get_app();
    return base::StringPrintf("app<%s,%s>", app.title.c_str(), app.id.c_str());
  } else if (entity.is_tab()) {
    const mojom::Tab& tab = *entity.get_tab();
    return base::StringPrintf("tab<%s,%s>", tab.title.c_str(),
                              tab.url->url.c_str());
  }
  return "<unknown entity type>";
}

// A custom matcher to compare two ClusteringResponse.
class EqualsClusteringResponseMatcher {
 public:
  using is_gtest_matcher = void;

  explicit EqualsClusteringResponseMatcher(ClusteringResponse expected_response)
      : expected_response_(std::move(expected_response)) {}

  bool MatchAndExplain(const ClusteringResponse& response,
                       MatchResultListener* listener) const {
    if (expected_response_.clusters.size() != response.clusters.size()) {
      *listener << "\nExpected: " << expected_response_.clusters.size()
                << " clusters\n"
                << "  Actual: " << response.clusters.size() << " clusters";
      return false;
    }

    for (int i = 0; i < expected_response_.clusters.size(); ++i) {
      const Cluster& expected_cluster = expected_response_.clusters[i];
      const Cluster& cluster = response.clusters[i];
      if (expected_cluster.entities.size() != cluster.entities.size()) {
        *listener << "\nExpected: " << expected_cluster.entities.size()
                  << " items in group " << i
                  << "\n  Actual: " << cluster.entities.size() << " items";
        return false;
      }
      bool matches = true;
      for (int j = 0; j < expected_cluster.entities.size(); ++j) {
        const Cluster& expected_cluster = expected_response_.clusters[i];
        if (!expected_cluster.entities[j]->Equals(*cluster.entities[j])) {
          *listener << "\nItem " << j << " in group " << i
                    << " differs:\nExpected: "
                    << EntityToString(*expected_cluster.entities[j])
                    << "\n  Actual: " << EntityToString(*cluster.entities[j]);
          matches = false;
        }
      }
      if (!matches) {
        return false;
      }
    }
    return true;
  }

  void DescribeTo(std::ostream* os) const {
    *os << "ClusteringResponse equals";
  }

  void DescribeNegationTo(std::ostream* os) const {
    *os << "ClusteringResponse differs";
  }

 private:
  ClusteringResponse expected_response_;
};

Matcher<const ClusteringResponse&> EqualsClusteringResponse(
    ClusteringResponse expected_response) {
  return EqualsClusteringResponseMatcher(std::move(expected_response));
}

}  // namespace

class ClusteringEngineTest : public testing::Test {
 public:
  ClusteringEngineTest() : coral_metrics_(raw_ref(metrics_)) {}

  void SetUp() override { mojo::core::Init(); }

 protected:
  CoralResult<ClusteringResponse> RunTest(
      mojom::GroupRequestPtr request,
      EmbeddingResponse embedding_response,
      const std::optional<clustering::Groups> fake_grouping) {
    auto mock_clustering_factory =
        std::make_unique<clustering::MockClusteringFactory>();
    auto mock_clustering =
        std::make_unique<clustering::MockAgglomerativeClustering>();

    EXPECT_CALL(*mock_clustering, Run(_, _, _)).WillOnce(Return(fake_grouping));

    // Transfer ownership of |mock_clustering| to the caller.
    EXPECT_CALL(*mock_clustering_factory, NewAgglomerativeClustering(_))
        .WillOnce(Return(std::move(mock_clustering)));

    // Transfer ownership of |mock_clustering_factory| to |engine|.
    auto engine = std::make_unique<ClusteringEngine>(
        raw_ref(coral_metrics_), std::move(mock_clustering_factory));

    TestFuture<mojom::GroupRequestPtr, CoralResult<ClusteringResponse>>
        grouping_future;
    engine->Process(std::move(request), std::move(embedding_response),
                    grouping_future.GetCallback());
    auto [_, result] = grouping_future.Take();
    return std::move(result);
  }

 private:
  NiceMock<MetricsLibraryMock> metrics_;
  CoralMetrics coral_metrics_;
};

TEST_F(ClusteringEngineTest, Success) {
  auto request = GetFakeGroupRequest();

  clustering::Groups fake_grouping = {
      {0, 2, 1},
      {5},
      {3, 4},
  };

  CoralResult<ClusteringResponse> result =
      RunTest(request->Clone(), GetFakeEmbeddingResponse(), fake_grouping);

  ASSERT_TRUE(result.has_value());
  EXPECT_THAT(*result, EqualsClusteringResponse(FakeClusteringResponse(
                           request->entities, {
                                                  {1, 2, 0},
                                                  {3, 4},
                                                  {5},
                                              })));
}

TEST_F(ClusteringEngineTest, MaxClusters) {
  auto request = GetFakeGroupRequest();
  request->clustering_options->max_clusters = 2;

  clustering::Groups fake_grouping = {
      {0, 2, 1},
      {5},
      {3, 4},
  };

  CoralResult<ClusteringResponse> result =
      RunTest(request->Clone(), GetFakeEmbeddingResponse(), fake_grouping);

  ASSERT_TRUE(result.has_value());
  EXPECT_THAT(*result, EqualsClusteringResponse(FakeClusteringResponse(
                           request->entities, {
                                                  {1, 2, 0},
                                                  {3, 4},
                                              })));
}

TEST_F(ClusteringEngineTest, MaxClustersExceedGroupSize) {
  auto request = GetFakeGroupRequest();
  request->clustering_options->max_clusters = 6;

  clustering::Groups fake_grouping = {
      {0, 2, 1},
      {5},
      {3, 4},
  };

  CoralResult<ClusteringResponse> result =
      RunTest(request->Clone(), GetFakeEmbeddingResponse(), fake_grouping);

  ASSERT_TRUE(result.has_value());
  EXPECT_THAT(*result, EqualsClusteringResponse(FakeClusteringResponse(
                           request->entities, {
                                                  {1, 2, 0},
                                                  {3, 4},
                                                  {5},
                                              })));
}

TEST_F(ClusteringEngineTest, MaxItemsInCluster) {
  auto request = GetFakeGroupRequest();
  request->clustering_options->max_items_in_cluster = 2;

  clustering::Groups fake_grouping = {
      {0, 2, 1},
      {5},
      {3, 4},
  };

  CoralResult<ClusteringResponse> result =
      RunTest(request->Clone(), GetFakeEmbeddingResponse(), fake_grouping);

  ASSERT_TRUE(result.has_value());
  EXPECT_THAT(*result, EqualsClusteringResponse(
                           FakeClusteringResponse(request->entities, {
                                                                         {1, 2},
                                                                         {3, 4},
                                                                         {5},
                                                                     })));
}

TEST_F(ClusteringEngineTest, MaxItemsInClusterExceedsSize) {
  auto request = GetFakeGroupRequest();
  request->clustering_options->max_items_in_cluster = 5;

  clustering::Groups fake_grouping = {
      {0, 2, 1},
      {5},
      {3, 4},
  };

  CoralResult<ClusteringResponse> result =
      RunTest(request->Clone(), GetFakeEmbeddingResponse(), fake_grouping);

  ASSERT_TRUE(result.has_value());
  EXPECT_THAT(*result, EqualsClusteringResponse(FakeClusteringResponse(
                           request->entities, {
                                                  {1, 2, 0},
                                                  {3, 4},
                                                  {5},
                                              })));
}

TEST_F(ClusteringEngineTest, MinItemsInCluster) {
  auto request = GetFakeGroupRequest();
  request->clustering_options->min_items_in_cluster = 2;

  clustering::Groups fake_grouping = {
      {0, 2, 1},
      {5},
      {3, 4},
  };

  CoralResult<ClusteringResponse> result =
      RunTest(request->Clone(), GetFakeEmbeddingResponse(), fake_grouping);

  ASSERT_TRUE(result.has_value());
  EXPECT_THAT(*result, EqualsClusteringResponse(FakeClusteringResponse(
                           request->entities, {
                                                  {1, 2, 0},
                                                  {3, 4},
                                              })));
}

TEST_F(ClusteringEngineTest, GroupingError) {
  auto request = GetFakeGroupRequest();

  CoralResult<ClusteringResponse> result =
      RunTest(std::move(request), GetFakeEmbeddingResponse(), std::nullopt);

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(mojom::CoralError::kClusteringError, result.error());
}

TEST(ClusteringEngineRealImplementationTest, Success) {
  mojo::core::Init();
  NiceMock<MetricsLibraryMock> metrics;
  CoralMetrics coral_metrics((raw_ref(metrics)));
  auto engine = std::make_unique<ClusteringEngine>(
      raw_ref(coral_metrics),
      std::make_unique<clustering::ClusteringFactory>());

  // Fake data from coral/test_util.h
  auto request = GetFakeGroupRequest();
  auto embedding_response = GetFakeEmbeddingResponse();

  TestFuture<mojom::GroupRequestPtr, CoralResult<ClusteringResponse>>
      grouping_future;
  engine->Process(request->Clone(), std::move(embedding_response),
                  grouping_future.GetCallback());
  auto [_, result] = grouping_future.Take();

  ASSERT_TRUE(result.has_value());
  EXPECT_THAT(*result, EqualsClusteringResponse(FakeClusteringResponse(
                           request->entities, {
                                                  {1, 2, 0},
                                                  {3, 4},
                                                  {5},
                                              })));
}

TEST(MatrixCalculationTest, Success) {
  const std::vector<Embedding> embeddings = {{0, 1, 2}, {1, 5, 9}, {3, 6, 7}};
  std::optional<clustering::Matrix> distances =
      internal::DistanceMatrix(embeddings);

  clustering::Matrix expected_distances = {{0, 0.00562328, 0.0774688},
                                           {0.00562328, 0, 0.0427719},
                                           {0.0774688, 0.0427719, 0}};
  ASSERT_TRUE(distances.has_value());
  const int n = embeddings.size();
  for (int i = 0; i < n; ++i) {
    for (int j = 0; j < n; ++j) {
      EXPECT_NEAR(expected_distances[i][j], (*distances)[i][j], 1e-6);
    }
  }
}

TEST(MatrixCalculationTest, EmptyEmbedding) {
  const std::vector<Embedding> embeddings = {};
  std::optional<clustering::Matrix> distances =
      internal::DistanceMatrix(embeddings);

  ASSERT_TRUE(distances.has_value());
  EXPECT_EQ(0, distances->size());
}

TEST(MatrixCalculationTest, OneEmbedding) {
  const std::vector<Embedding> embeddings = {{1, 2, 3}};
  std::optional<clustering::Matrix> distances =
      internal::DistanceMatrix(embeddings);

  ASSERT_TRUE(distances.has_value());
  EXPECT_EQ(0.0, (*distances)[0][0]);
}

TEST(MatrixCalculationTest, EmbeddingsLengthNotMatch) {
  const std::vector<Embedding> embeddings = {{0, 1, 2}, {1, 5}, {3, 6, 7}};
  std::optional<clustering::Matrix> distances =
      internal::DistanceMatrix(embeddings);

  ASSERT_FALSE(distances.has_value());
}

TEST(MatrixCalculationTest, ZeroNormEmbeddings) {
  std::vector<Embedding> embeddings = {{0, 1, 2}, {0, 0, 0}, {3, 6, 7}};
  std::optional<clustering::Matrix> distances =
      internal::DistanceMatrix(embeddings);

  ASSERT_FALSE(distances.has_value());

  embeddings = {{0, 0, 1e-6}, {0, 1e-6, 0}, {1e-6, 0, 0}};
  distances = internal::DistanceMatrix(embeddings);

  ASSERT_TRUE(distances.has_value());
}

TEST(CalculateVectorCenterTest, Success) {
  const std::vector<Embedding> embeddings = {
      {0, 1, 2}, {1, 5, 9}, {3, 6, 7}, {6, 7, 10}};
  std::optional<Embedding> center =
      internal::CalculateVectorCenter(embeddings, {0, 2, 3});

  ASSERT_TRUE(center.has_value());
  const std::vector<float> expected_center = {0.250185, 0.526905, 0.783878};
  EXPECT_EQ(expected_center.size(), center->size());
  for (int i = 0; i < center->size(); ++i) {
    EXPECT_NEAR(expected_center[i], (*center)[i], 1e-6);
  }
}

TEST(CalculateVectorCenterTest, UnmatchedEmbeddingSize) {
  const std::vector<Embedding> embeddings = {
      {0, 1, 2}, {1, 5, 9}, {3, 6, 7}, {6, 7, 10, 11}};
  std::optional<Embedding> center =
      internal::CalculateVectorCenter(embeddings, {0, 2, 3});

  ASSERT_FALSE(center.has_value());
}

TEST(CalculateVectorCenterTest, ZeroLength) {
  const std::vector<Embedding> embeddings = {
      {0, 1, 2}, {1, 5, 9}, {0, 0, 0}, {6, 7, 10}};
  std::optional<Embedding> center =
      internal::CalculateVectorCenter(embeddings, {0, 2, 3});

  ASSERT_FALSE(center.has_value());
}

}  // namespace coral
