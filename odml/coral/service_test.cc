// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/coral/service.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "base/test/test_future.h"
#include "base/types/expected.h"
#include "odml/coral/clustering/engine.h"
#include "odml/coral/embedding/engine.h"
#include "odml/coral/test_util.h"
#include "odml/coral/title_generation/engine.h"
#include "odml/mojom/coral_service.mojom.h"

namespace coral {
namespace {

using base::test::TestFuture;
using testing::_;
using testing::Eq;

// Workaround: base::expected doesn't have a move assignment with a
// base::expected parameter. It only has specializations of base::ok and
// base::unexpected variants.
template <class T, class E>
void MoveAssignExpected(base::expected<T, E>& dest,
                        base::expected<T, E>&& source) {
  if (source.has_value()) {
    dest = base::ok(std::move(*source));
  } else {
    dest = base::unexpected(std::move(source.error()));
  }
}

class MockEmbeddingEngine : public EmbeddingEngineInterface {
 public:
  MockEmbeddingEngine() = default;
  MOCK_METHOD(void,
              Process,
              (const mojom::GroupRequest&, EmbeddingCallback),
              (override));
  void Expect(const mojom::GroupRequest& request,
              CoralResult<EmbeddingResponse> response) {
    MoveAssignExpected(response_, std::move(response));
    EXPECT_CALL(*this, Process(Eq(std::ref(request)), _))
        .WillOnce([this](auto&&, auto&& callback) {
          std::move(callback).Run(std::move(response_));
        });
  }

 private:
  CoralResult<EmbeddingResponse> response_ =
      base::unexpected(mojom::CoralError::kUnknownError);
};

class MockClusteringEngine : public ClusteringEngineInterface {
 public:
  MockClusteringEngine() = default;
  MOCK_METHOD(void,
              Process,
              (const mojom::GroupRequest&,
               const EmbeddingResponse&,
               ClusteringCallback),
              (override));
  void Expect(const mojom::GroupRequest& request,
              EmbeddingResponse embedding_response,
              CoralResult<ClusteringResponse> response) {
    embedding_response_ = std::move(embedding_response);
    MoveAssignExpected(response_, std::move(response));
    EXPECT_CALL(*this, Process(Eq(std::ref(request)),
                               Eq(std::ref(embedding_response_)), _))
        .WillOnce([this](auto&&, auto&&, auto&& callback) {
          std::move(callback).Run(std::move(response_));
        });
  }

 private:
  EmbeddingResponse embedding_response_;
  CoralResult<ClusteringResponse> response_ =
      base::unexpected(mojom::CoralError::kUnknownError);
};

class MockTitleGenerationEngine : public TitleGenerationEngineInterface {
 public:
  MockTitleGenerationEngine() = default;
  MOCK_METHOD(void,
              Process,
              (const mojom::GroupRequest&,
               const ClusteringResponse&,
               TitleGenerationCallback),
              (override));
  void Expect(const mojom::GroupRequest& request,
              ClusteringResponse clustering_response,
              CoralResult<TitleGenerationResponse> response) {
    clustering_response_ = std::move(clustering_response);
    MoveAssignExpected(response_, std::move(response));
    EXPECT_CALL(*this, Process(Eq(std::ref(request)),
                               Eq(std::ref(clustering_response_)), _))
        .WillOnce([this](auto&&, auto&&, auto&& callback) {
          std::move(callback).Run(std::move(response_));
        });
  }

 private:
  ClusteringResponse clustering_response_;
  CoralResult<TitleGenerationResponse> response_ =
      base::unexpected(mojom::CoralError::kUnknownError);
};

class CoralServiceTest : public testing::Test {
  void SetUp() override {
    auto embedding_engine = std::make_unique<MockEmbeddingEngine>();
    auto clustering_engine = std::make_unique<MockClusteringEngine>();
    auto title_generation_engine =
        std::make_unique<MockTitleGenerationEngine>();
    embedding_engine_ = embedding_engine.get();
    clustering_engine_ = clustering_engine.get();
    title_generation_engine_ = title_generation_engine.get();

    service_ = std::make_unique<CoralService>(
        std::move(embedding_engine), std::move(clustering_engine),
        std::move(title_generation_engine));
  }

 protected:
  void ExpectGroupResult(mojom::GroupRequestPtr request,
                         mojom::GroupResultPtr group_result) {
    TestFuture<mojom::GroupResultPtr> group_future;
    service_->Group(std::move(request), group_future.GetCallback());
    EXPECT_EQ(group_future.Take(), group_result);
  }

  void ExpectCacheEmbeddingsOk(mojom::CacheEmbeddingsRequestPtr request) {
    TestFuture<mojom::CacheEmbeddingsResultPtr> cache_future;
    service_->CacheEmbeddings(std::move(request), cache_future.GetCallback());
    EXPECT_TRUE(cache_future.Take()->is_response());
  }

  void ExpectCacheEmbeddingsError(mojom::CacheEmbeddingsRequestPtr request,
                                  mojom::CoralError error) {
    TestFuture<mojom::CacheEmbeddingsResultPtr> cache_future;
    service_->CacheEmbeddings(std::move(request), cache_future.GetCallback());
    auto result = cache_future.Take();
    ASSERT_TRUE(result->is_error());
    EXPECT_EQ(result->get_error(), error);
  }

  MockEmbeddingEngine* embedding_engine_;
  MockClusteringEngine* clustering_engine_;
  MockTitleGenerationEngine* title_generation_engine_;

 private:
  std::unique_ptr<CoralService> service_;
};

TEST_F(CoralServiceTest, GroupSuccess) {
  auto request = GetFakeGroupRequest();
  embedding_engine_->Expect(*request, GetFakeEmbeddingResponse());
  clustering_engine_->Expect(*request, GetFakeEmbeddingResponse(),
                             GetFakeClusteringResponse());
  title_generation_engine_->Expect(*request, GetFakeClusteringResponse(),
                                   GetFakeTitleGenerationResponse());
  ExpectGroupResult(std::move(request), GetFakeGroupResult());
}

TEST_F(CoralServiceTest, EmbeddingFailed) {
  auto request = GetFakeGroupRequest();
  embedding_engine_->Expect(*request,
                            base::unexpected(mojom::CoralError::kUnknownError));
  ExpectGroupResult(std::move(request), mojom::GroupResult::NewError(
                                            mojom::CoralError::kUnknownError));
}

TEST_F(CoralServiceTest, ClusteringFailed) {
  auto request = GetFakeGroupRequest();
  embedding_engine_->Expect(*request, GetFakeEmbeddingResponse());
  clustering_engine_->Expect(
      *request, GetFakeEmbeddingResponse(),
      base::unexpected(mojom::CoralError::kUnknownError));
  ExpectGroupResult(std::move(request), mojom::GroupResult::NewError(
                                            mojom::CoralError::kUnknownError));
}

TEST_F(CoralServiceTest, TitleGenerationFailed) {
  auto request = GetFakeGroupRequest();
  embedding_engine_->Expect(*request, GetFakeEmbeddingResponse());
  clustering_engine_->Expect(*request, GetFakeEmbeddingResponse(),
                             GetFakeClusteringResponse());
  title_generation_engine_->Expect(
      *request, GetFakeClusteringResponse(),
      base::unexpected(mojom::CoralError::kUnknownError));
  ExpectGroupResult(std::move(request), mojom::GroupResult::NewError(
                                            mojom::CoralError::kUnknownError));
}

TEST_F(CoralServiceTest, CacheEmbeddingsSuccess) {
  auto embedding_options = mojom::EmbeddingOptions::New();
  embedding_options->request_safety_thresholds = mojom::SafetyThresholds::New();
  auto request = mojom::CacheEmbeddingsRequest::New(GetFakeEntities(),
                                                    embedding_options.Clone());
  auto expected_group_request = mojom::GroupRequest::New();
  expected_group_request->entities = GetFakeEntities();
  expected_group_request->embedding_options = std::move(embedding_options);
  embedding_engine_->Expect(*expected_group_request,
                            GetFakeEmbeddingResponse());
  ExpectCacheEmbeddingsOk(std::move(request));
}

TEST_F(CoralServiceTest, CacheEmbeddingsFailed) {
  auto embedding_options = mojom::EmbeddingOptions::New();
  embedding_options->request_safety_thresholds = mojom::SafetyThresholds::New();
  auto request = mojom::CacheEmbeddingsRequest::New(GetFakeEntities(),
                                                    embedding_options.Clone());
  auto expected_group_request = mojom::GroupRequest::New();
  expected_group_request->entities = GetFakeEntities();
  expected_group_request->embedding_options = std::move(embedding_options);
  embedding_engine_->Expect(*expected_group_request,
                            base::unexpected(mojom::CoralError::kUnknownError));
  ExpectCacheEmbeddingsError(std::move(request),
                             mojom::CoralError::kUnknownError);
}

}  // namespace
}  // namespace coral
