// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/coral/clustering/engine.h"

#include <memory>
#include <utility>

#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <mojo/core/embedder/embedder.h>

#include "odml/coral/clustering/mock_agglomerative_clustering.h"
#include "odml/coral/clustering/mock_clustering_factory.h"
#include "odml/coral/test_util.h"
#include "odml/mojom/coral_service.mojom.h"

namespace coral {

namespace {

using base::test::TestFuture;
using testing::_;
using testing::Return;

// Creates a tab with title "title|id|" and url "url|id|".
mojom::EntityPtr FakeTab(const int id) {
  return mojom::Entity::NewTab(
      mojom::Tab::New("title" + std::to_string(id),
                      url::mojom::Url::New("url" + std::to_string(id))));
}

// Creates GroupRequest with |n| entities.
mojom::GroupRequestPtr FakeGroupRequestWithNumEntities(const int n) {
  auto request = mojom::GroupRequest::New();
  request->embedding_options = mojom::EmbeddingOptions::New();
  request->clustering_options = mojom::ClusteringOptions::New();
  request->title_generation_options = mojom::TitleGenerationOptions::New();
  for (int i = 0; i < n; ++i) {
    request->entities.push_back(FakeTab(i));
  }
  return request;
}

// Creates fake response with the given grouping.
ClusteringResponse FakeClusteringResponse(const clustering::Groups& groups) {
  ClusteringResponse response;
  for (int i = 0; i < groups.size(); ++i) {
    Cluster cluster;
    for (int j = 0; j < groups[i].size(); ++j) {
      cluster.entities.push_back(FakeTab(groups[i][j]));
    }
    response.clusters.push_back(std::move(cluster));
  }
  return response;
}

// Compares two ClusteringResponse.
void ExpectResponseEqual(const ClusteringResponse& expected_response,
                         const ClusteringResponse& response) {
  EXPECT_EQ(expected_response.clusters.size(), response.clusters.size());

  for (int i = 0; i < expected_response.clusters.size(); ++i) {
    const Cluster& expected_cluster = expected_response.clusters[i];
    const Cluster& cluster = response.clusters[i];
    EXPECT_EQ(expected_cluster.entities.size(), cluster.entities.size());
    // Assume the entity is always tab but not app.
    for (int j = 0; j < expected_cluster.entities.size(); ++j) {
      EXPECT_TRUE(expected_cluster.entities[j]->Equals(*cluster.entities[j]))
          << "item " << j << " in group " << i << " differs ("
          << expected_cluster.entities[j]->get_tab()->title << ", "
          << cluster.entities[j]->get_tab()->title << ")";
    }
  }
}

}  // namespace

class ClusteringEngineTest : public testing::Test {
 public:
  ClusteringEngineTest() {}

  void SetUp() override { mojo::core::Init(); }

 protected:
  CoralResult<ClusteringResponse> RunTest(
      mojom::GroupRequestPtr request,
      const std::optional<clustering::Groups> fake_grouping) const {
    auto mock_clustering_factory =
        std::make_unique<clustering::MockClusteringFactory>();
    auto mock_clustering =
        std::make_unique<clustering::MockAgglomerativeClustering>();

    EXPECT_CALL(*mock_clustering, Run(_, _, _)).WillOnce(Return(fake_grouping));

    // Transfer ownership of |mock_clustering| to the caller.
    EXPECT_CALL(*mock_clustering_factory, NewAgglomerativeClustering(_))
        .WillOnce(Return(std::move(mock_clustering)));

    // Transfer ownership of |mock_clustering_factory| to |engine|.
    auto engine =
        std::make_unique<ClusteringEngine>(std::move(mock_clustering_factory));

    // Empty. Since we mocked out the clustering implementation, the content
    // doesn't matter.
    EmbeddingResponse embedding_response;

    TestFuture<mojom::GroupRequestPtr, CoralResult<ClusteringResponse>>
        grouping_future;
    engine->Process(std::move(request), std::move(embedding_response),
                    grouping_future.GetCallback());
    auto [_, result] = grouping_future.Take();
    return std::move(result);
  }
};

TEST_F(ClusteringEngineTest, Success) {
  auto request = FakeGroupRequestWithNumEntities(6);

  clustering::Groups fake_grouping = {
      {0, 2, 5},
      {4},
      {1, 3},
  };

  CoralResult<ClusteringResponse> result =
      RunTest(std::move(request), fake_grouping);

  ClusteringResponse expected_response = FakeClusteringResponse({
      {0, 2, 5},
      {1, 3},
      {4},
  });

  ASSERT_TRUE(result.has_value());
  ExpectResponseEqual(expected_response, *result);
}

TEST_F(ClusteringEngineTest, MaxClusters) {
  auto request = FakeGroupRequestWithNumEntities(6);
  request->clustering_options->max_clusters = 2;

  clustering::Groups fake_grouping = {
      {0, 2, 5},
      {4},
      {1, 3},
  };

  CoralResult<ClusteringResponse> result =
      RunTest(std::move(request), fake_grouping);

  ClusteringResponse expected_response = FakeClusteringResponse({
      {0, 2, 5},
      {1, 3},
  });

  ASSERT_TRUE(result.has_value());
  ExpectResponseEqual(expected_response, *result);
}

TEST_F(ClusteringEngineTest, MaxClustersExceedGroupSize) {
  auto request = FakeGroupRequestWithNumEntities(6);
  request->clustering_options->max_clusters = 6;

  clustering::Groups fake_grouping = {
      {0, 2, 5},
      {4},
      {1, 3},
  };

  CoralResult<ClusteringResponse> result =
      RunTest(std::move(request), fake_grouping);

  ClusteringResponse expected_response = FakeClusteringResponse({
      {0, 2, 5},
      {1, 3},
      {4},
  });

  ASSERT_TRUE(result.has_value());
  ExpectResponseEqual(expected_response, *result);
}

TEST_F(ClusteringEngineTest, MaxItemsInCluster) {
  auto request = FakeGroupRequestWithNumEntities(6);
  request->clustering_options->max_items_in_cluster = 2;

  clustering::Groups fake_grouping = {
      {0, 2, 5},
      {4},
      {1, 3},
  };

  CoralResult<ClusteringResponse> result =
      RunTest(std::move(request), fake_grouping);

  ClusteringResponse expected_response = FakeClusteringResponse({
      {0, 2},
      {1, 3},
      {4},
  });

  ASSERT_TRUE(result.has_value());
  ExpectResponseEqual(expected_response, *result);
}

TEST_F(ClusteringEngineTest, MaxItemsInClusterExceedsSize) {
  auto request = FakeGroupRequestWithNumEntities(6);
  request->clustering_options->max_items_in_cluster = 5;

  clustering::Groups fake_grouping = {
      {0, 2, 5},
      {4},
      {1, 3},
  };

  CoralResult<ClusteringResponse> result =
      RunTest(std::move(request), fake_grouping);

  ClusteringResponse expected_response = FakeClusteringResponse({
      {0, 2, 5},
      {1, 3},
      {4},
  });

  ASSERT_TRUE(result.has_value());
  ExpectResponseEqual(expected_response, *result);
}

TEST_F(ClusteringEngineTest, MinItemsInCluster) {
  auto request = FakeGroupRequestWithNumEntities(6);
  request->clustering_options->min_items_in_cluster = 2;

  clustering::Groups fake_grouping = {
      {0, 2, 5},
      {4},
      {1, 3},
  };

  CoralResult<ClusteringResponse> result =
      RunTest(std::move(request), fake_grouping);

  ClusteringResponse expected_response = FakeClusteringResponse({
      {0, 2, 5},
      {1, 3},
  });

  ASSERT_TRUE(result.has_value());
  ExpectResponseEqual(expected_response, *result);
}

TEST_F(ClusteringEngineTest, GroupingError) {
  auto request = FakeGroupRequestWithNumEntities(6);

  CoralResult<ClusteringResponse> result =
      RunTest(std::move(request), std::nullopt);

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(mojom::CoralError::kClusteringError, result.error());
}

TEST(ClusteringEngineRealImplementationTest, Success) {
  mojo::core::Init();
  auto engine = std::make_unique<ClusteringEngine>(
      std::make_unique<clustering::ClusteringFactory>());

  // Fake data from coral/test_util.h
  auto request = GetFakeGroupRequest();
  auto embedding_response = GetFakeEmbeddingResponse();

  TestFuture<mojom::GroupRequestPtr, CoralResult<ClusteringResponse>>
      grouping_future;
  engine->Process(std::move(request), std::move(embedding_response),
                  grouping_future.GetCallback());
  auto [_, result] = grouping_future.Take();

  ASSERT_TRUE(result.has_value());
  ExpectResponseEqual(GetFakeClusteringResponse(), *result);
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

}  // namespace coral
