// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/coral/service.h"

#include <vector>

#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <base/types/expected.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <metrics/metrics_library_mock.h>

#include "odml/coral/clustering/engine.h"
#include "odml/coral/embedding/engine.h"
#include "odml/coral/test_util.h"
#include "odml/coral/title_generation/engine.h"
#include "odml/cros_safety/safety_service_manager_mock.h"
#include "odml/embedding_model/embedding_model_service.h"
#include "odml/embedding_model/model_factory_mock.h"
#include "odml/i18n/mock_translator.h"
#include "odml/mojom/coral_service.mojom.h"
#include "odml/on_device_model/mock_on_device_model_service.h"

namespace coral {
namespace {

using base::test::TestFuture;
using testing::_;
using testing::Eq;
using testing::Gt;
using testing::NiceMock;

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
  MockEmbeddingEngine() {
    ON_CALL(*this, Process)
        .WillByDefault([this](auto&& request, auto&& callback) {
          for (int i = 0; i < requests_.size(); i++) {
            if (request->entities == requests_[i]) {
              CoralResult response = std::move(responses_[i]);
              responses_.erase(responses_.begin() + i);
              requests_.erase(requests_.begin() + i);
              std::move(callback).Run(std::move(request), std::move(response));
              return;
            }
          }
          NOTREACHED();
        });
  }

  MOCK_METHOD(void,
              Process,
              (mojom::GroupRequestPtr, EmbeddingCallback),
              (override));

  void Expect(std::vector<mojom::EntityPtr> request,
              CoralResult<EmbeddingResponse> response) {
    requests_.push_back(std::move(request));
    responses_.push_back(std::move(response));
  }

 private:
  // We're using vector and not hash maps because N=1 or 2 in most cases.
  std::vector<std::vector<mojom::EntityPtr>> requests_;
  std::vector<CoralResult<EmbeddingResponse>> responses_;
};

class MockClusteringEngine : public ClusteringEngineInterface {
 public:
  MockClusteringEngine() = default;
  MOCK_METHOD(void,
              Process,
              (mojom::GroupRequestPtr,
               EmbeddingResponse,
               EmbeddingResponse,
               ClusteringCallback),
              (override));
  void Expect(EmbeddingResponse embedding_response,
              CoralResult<ClusteringResponse> response) {
    embedding_response_ = std::move(embedding_response);
    MoveAssignExpected(response_, std::move(response));
    EXPECT_CALL(*this, Process(_, Eq(std::ref(embedding_response_)), _, _))
        .WillOnce([this](auto&& request, auto&&, auto&&, auto&& callback) {
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
               mojo::PendingRemote<mojom::TitleObserver>,
               TitleGenerationCallback),
              (override));
  void Expect(ClusteringResponse clustering_response,
              CoralResult<TitleGenerationResponse> response) {
    clustering_response_ = std::move(clustering_response);
    MoveAssignExpected(response_, std::move(response));
    EXPECT_CALL(*this, Process(_, Eq(std::ref(clustering_response_)), _, _))
        .WillOnce([this](auto&&, auto&&, auto&&, auto&& callback) {
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
        raw_ref(metrics_), std::move(embedding_engine),
        std::move(clustering_engine), std::move(title_generation_engine));
  }

 protected:
  void ExpectGroupResult(mojom::GroupRequestPtr request,
                         mojom::GroupResultPtr group_result) {
    TestFuture<mojom::GroupResultPtr> group_future;
    service_->Group(std::move(request), mojo::NullRemote(),
                    group_future.GetCallback());
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

  void ExpectSendGroupStatus(bool success) {
    if (success) {
      EXPECT_CALL(metrics_, SendEnumToUMA(metrics::kGroupStatus, 0, _));
    } else {
      EXPECT_CALL(metrics_, SendEnumToUMA(metrics::kGroupStatus, Gt(0), _));
    }
  }

  void ExpectSendCacheEmbeddingsStatus(bool success) {
    if (success) {
      EXPECT_CALL(metrics_,
                  SendEnumToUMA(metrics::kCacheEmbeddingsStatus, 0, _));
    } else {
      EXPECT_CALL(metrics_,
                  SendEnumToUMA(metrics::kCacheEmbeddingsStatus, Gt(0), _));
    }
  }

  void ExpectSendGroupLatency(int times) {
    EXPECT_CALL(metrics_, SendTimeToUMA(metrics::kGroupLatency, _, _, _, _))
        .Times(times);
  }

  void ExpectSendCacheEmbeddingsLatency(int times) {
    EXPECT_CALL(metrics_,
                SendTimeToUMA(metrics::kCacheEmbeddingsLatency, _, _, _, _))
        .Times(times);
  }

  MockEmbeddingEngine* embedding_engine_;
  MockClusteringEngine* clustering_engine_;
  MockTitleGenerationEngine* title_generation_engine_;

 private:
  NiceMock<MetricsLibraryMock> metrics_;
  std::unique_ptr<CoralService> service_;
};

// Test that we can construct CoralService with the real constructor.
TEST(CoralServiceConstructTest, Construct) {
  base::test::SingleThreadTaskEnvironment task_environment;

  MetricsLibraryMock metrics;
  embedding_model::ModelFactoryMock embedding_model_factory;
  on_device_model::MockOnDeviceModelService model_service;
  cros_safety::SafetyServiceManagerMock safety_service_manager;
  embedding_model::EmbeddingModelService embedding_service(
      (raw_ref(metrics)), raw_ref(embedding_model_factory));
  i18n::MockTranslator translator;
  CoralService service((raw_ref(metrics)), (raw_ref(model_service)),
                       (raw_ref(embedding_service)), nullptr,
                       raw_ref(safety_service_manager), raw_ref(translator));
}

TEST_F(CoralServiceTest, GroupSuccess) {
  ExpectSendGroupStatus(true);
  ExpectSendGroupLatency(1);
  auto request = GetFakeGroupRequest();
  embedding_engine_->Expect(GetFakeEntities(), GetFakeEmbeddingResponse());
  embedding_engine_->Expect(std::vector<mojom::EntityPtr>(),
                            EmbeddingResponse{});
  clustering_engine_->Expect(GetFakeEmbeddingResponse(),
                             GetFakeClusteringResponse());
  title_generation_engine_->Expect(GetFakeClusteringResponse(),
                                   GetFakeTitleGenerationResponse());
  ExpectGroupResult(std::move(request), GetFakeGroupResult());
}

TEST_F(CoralServiceTest, EmbeddingFailed) {
  ExpectSendGroupStatus(false);
  ExpectSendGroupLatency(0);
  auto request = GetFakeGroupRequest();
  embedding_engine_->Expect(GetFakeEntities(),
                            base::unexpected(mojom::CoralError::kUnknownError));
  ExpectGroupResult(std::move(request), mojom::GroupResult::NewError(
                                            mojom::CoralError::kUnknownError));
}

TEST_F(CoralServiceTest, ClusteringFailed) {
  ExpectSendGroupStatus(false);
  ExpectSendGroupLatency(0);
  auto request = GetFakeGroupRequest();
  embedding_engine_->Expect(GetFakeEntities(), GetFakeEmbeddingResponse());
  embedding_engine_->Expect(std::vector<mojom::EntityPtr>(),
                            EmbeddingResponse{});
  clustering_engine_->Expect(
      GetFakeEmbeddingResponse(),
      base::unexpected(mojom::CoralError::kUnknownError));
  ExpectGroupResult(std::move(request), mojom::GroupResult::NewError(
                                            mojom::CoralError::kUnknownError));
}

TEST_F(CoralServiceTest, TitleGenerationFailed) {
  ExpectSendGroupStatus(false);
  ExpectSendGroupLatency(0);
  auto request = GetFakeGroupRequest();
  embedding_engine_->Expect(GetFakeEntities(), GetFakeEmbeddingResponse());
  embedding_engine_->Expect(std::vector<mojom::EntityPtr>(),
                            EmbeddingResponse{});
  clustering_engine_->Expect(GetFakeEmbeddingResponse(),
                             GetFakeClusteringResponse());
  title_generation_engine_->Expect(
      GetFakeClusteringResponse(),
      base::unexpected(mojom::CoralError::kUnknownError));
  ExpectGroupResult(std::move(request), mojom::GroupResult::NewError(
                                            mojom::CoralError::kUnknownError));
}

TEST_F(CoralServiceTest, CacheEmbeddingsSuccess) {
  ExpectSendCacheEmbeddingsStatus(true);
  ExpectSendCacheEmbeddingsLatency(1);
  auto request = mojom::CacheEmbeddingsRequest::New(
      GetFakeEntities(), mojom::EmbeddingOptions::New());
  embedding_engine_->Expect(GetFakeEntities(), GetFakeEmbeddingResponse());
  ExpectCacheEmbeddingsOk(std::move(request));
}

TEST_F(CoralServiceTest, CacheEmbeddingsFailed) {
  ExpectSendCacheEmbeddingsStatus(false);
  ExpectSendCacheEmbeddingsLatency(0);
  auto request = mojom::CacheEmbeddingsRequest::New(
      GetFakeEntities(), mojom::EmbeddingOptions::New());
  embedding_engine_->Expect(GetFakeEntities(),
                            base::unexpected(mojom::CoralError::kUnknownError));
  ExpectCacheEmbeddingsError(std::move(request),
                             mojom::CoralError::kUnknownError);
}

}  // namespace
}  // namespace coral
