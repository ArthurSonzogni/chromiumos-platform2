// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/coral/service.h"

#include <base/test/test_future.h>
#include <base/types/expected.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <metrics/metrics_library_mock.h>

#include "odml/coral/clustering/engine.h"
#include "odml/coral/embedding/engine.h"
#include "odml/coral/test_util.h"
#include "odml/coral/title_generation/engine.h"
#include "odml/embedding_model/embedding_model_service.h"
#include "odml/mojom/coral_service.mojom.h"
#include "odml/on_device_model/mock_on_device_model_service.h"

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
              (mojom::GroupRequestPtr, EmbeddingCallback),
              (override));
  void Expect(CoralResult<EmbeddingResponse> response) {
    MoveAssignExpected(response_, std::move(response));
    EXPECT_CALL(*this, Process(_, _))
        .WillOnce([this](auto&& request, auto&& callback) {
          std::move(callback).Run(std::move(request), std::move(response_));
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
              (mojom::GroupRequestPtr, EmbeddingResponse, ClusteringCallback),
              (override));
  void Expect(EmbeddingResponse embedding_response,
              CoralResult<ClusteringResponse> response) {
    embedding_response_ = std::move(embedding_response);
    MoveAssignExpected(response_, std::move(response));
    EXPECT_CALL(*this, Process(_, Eq(std::ref(embedding_response_)), _))
        .WillOnce([this](auto&& request, auto&&, auto&& callback) {
          std::move(callback).Run(std::move(request), std::move(response_));
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
              (mojom::GroupRequestPtr,
               ClusteringResponse,
               TitleGenerationCallback),
              (override));
  void Expect(ClusteringResponse clustering_response,
              CoralResult<TitleGenerationResponse> response) {
    clustering_response_ = std::move(clustering_response);
    MoveAssignExpected(response_, std::move(response));
    EXPECT_CALL(*this, Process(_, Eq(std::ref(clustering_response_)), _))
        .WillOnce([this](auto&& request, auto&&, auto&& callback) {
          std::move(callback).Run(std::move(request), std::move(response_));
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

// Test that we can construct CoralService with the real constructor.
TEST(CoralServiceConstructTest, Construct) {
  MetricsLibraryMock metrics;
  on_device_model::MockOnDeviceModelService model_service;
  embedding_model::EmbeddingModelService embedding_service((raw_ref(metrics)));
  CoralService service((raw_ref(model_service)), raw_ref(embedding_service));
}

TEST_F(CoralServiceTest, GroupSuccess) {
  auto request = GetFakeGroupRequest();
  embedding_engine_->Expect(GetFakeEmbeddingResponse());
  clustering_engine_->Expect(GetFakeEmbeddingResponse(),
                             GetFakeClusteringResponse());
  title_generation_engine_->Expect(GetFakeClusteringResponse(),
                                   GetFakeTitleGenerationResponse());
  ExpectGroupResult(std::move(request), GetFakeGroupResult());
}

TEST_F(CoralServiceTest, EmbeddingFailed) {
  auto request = GetFakeGroupRequest();
  embedding_engine_->Expect(base::unexpected(mojom::CoralError::kUnknownError));
  ExpectGroupResult(std::move(request), mojom::GroupResult::NewError(
                                            mojom::CoralError::kUnknownError));
}

TEST_F(CoralServiceTest, ClusteringFailed) {
  auto request = GetFakeGroupRequest();
  embedding_engine_->Expect(GetFakeEmbeddingResponse());
  clustering_engine_->Expect(
      GetFakeEmbeddingResponse(),
      base::unexpected(mojom::CoralError::kUnknownError));
  ExpectGroupResult(std::move(request), mojom::GroupResult::NewError(
                                            mojom::CoralError::kUnknownError));
}

TEST_F(CoralServiceTest, TitleGenerationFailed) {
  auto request = GetFakeGroupRequest();
  embedding_engine_->Expect(GetFakeEmbeddingResponse());
  clustering_engine_->Expect(GetFakeEmbeddingResponse(),
                             GetFakeClusteringResponse());
  title_generation_engine_->Expect(
      GetFakeClusteringResponse(),
      base::unexpected(mojom::CoralError::kUnknownError));
  ExpectGroupResult(std::move(request), mojom::GroupResult::NewError(
                                            mojom::CoralError::kUnknownError));
}

TEST_F(CoralServiceTest, CacheEmbeddingsSuccess) {
  auto request = mojom::CacheEmbeddingsRequest::New(
      GetFakeEntities(), mojom::EmbeddingOptions::New());
  embedding_engine_->Expect(GetFakeEmbeddingResponse());
  ExpectCacheEmbeddingsOk(std::move(request));
}

TEST_F(CoralServiceTest, CacheEmbeddingsFailed) {
  auto request = mojom::CacheEmbeddingsRequest::New(
      GetFakeEntities(), mojom::EmbeddingOptions::New());
  embedding_engine_->Expect(base::unexpected(mojom::CoralError::kUnknownError));
  ExpectCacheEmbeddingsError(std::move(request),
                             mojom::CoralError::kUnknownError);
}

}  // namespace
}  // namespace coral
