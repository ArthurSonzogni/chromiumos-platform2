// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/coral/embedding/engine.h"

#include <memory>
#include <string>
#include <utility>

#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <mojo/public/cpp/bindings/receiver_set.h>

#include "odml/coral/test_util.h"
#include "odml/mojom/coral_service.mojom.h"
#include "odml/mojom/embedding_model.mojom.h"

namespace coral {
namespace {
using base::test::TestFuture;
using ::testing::NiceMock;
using ::testing::Return;

using embedding_model::mojom::OnDeviceEmbeddingModel;
using embedding_model::mojom::OnDeviceEmbeddingModelInferenceError;
using embedding_model::mojom::OnDeviceEmbeddingModelService;

class FakeEmbeddingModel : public OnDeviceEmbeddingModel {
 public:
  explicit FakeEmbeddingModel(
      const raw_ref<bool> should_error,
      mojo::PendingReceiver<OnDeviceEmbeddingModel> receiver)
      : should_error_(should_error),
        embeddings_to_return_(GetFakeEmbeddingResponse().embeddings),
        receiver_(this, std::move(receiver)) {}

  void GenerateEmbedding(
      embedding_model::mojom::GenerateEmbeddingRequestPtr request,
      base::OnceCallback<void(OnDeviceEmbeddingModelInferenceError,
                              const std::vector<float>&)> callback) override {
    if (*should_error_ || times_called_ >= embeddings_to_return_.size()) {
      std::move(callback).Run(OnDeviceEmbeddingModelInferenceError::kTooLong,
                              {});
      return;
    }
    std::move(callback).Run(OnDeviceEmbeddingModelInferenceError::kSuccess,
                            embeddings_to_return_[times_called_++]);
  }

  void Version(base::OnceCallback<void(const std::string&)> callback) override {
    std::move(callback).Run("1.0");
  }

 private:
  // Controls the result of next GenerateEmbeddings call.
  const raw_ref<bool> should_error_;

  std::vector<Embedding> embeddings_to_return_;
  size_t times_called_ = 0;

  mojo::Receiver<OnDeviceEmbeddingModel> receiver_;
};

class FakeEmbeddingModelService : public OnDeviceEmbeddingModelService {
 public:
  explicit FakeEmbeddingModelService(const raw_ref<bool> should_error)
      : should_error_(should_error) {
    ON_CALL(*this, LoadEmbeddingModel)
        .WillByDefault([this](auto&&, auto&& model, auto&&, auto&& callback) {
          model_ = std::make_unique<FakeEmbeddingModel>(should_error_,
                                                        std::move(model));
          std::move(callback).Run(
              on_device_model::mojom::LoadModelResult::kSuccess);
        });
  }

  MOCK_METHOD(void,
              LoadEmbeddingModel,
              (const base::Uuid& uuid,
               mojo::PendingReceiver<OnDeviceEmbeddingModel> model,
               mojo::PendingRemote<
                   on_device_model::mojom::PlatformModelProgressObserver>
                   progress_observer,
               LoadEmbeddingModelCallback callback),
              (override));

 private:
  // Controls the result of next GenerateEmbeddings call.
  const raw_ref<bool> should_error_;

  std::unique_ptr<OnDeviceEmbeddingModel> model_;
};

}  // namespace

class EmbeddingEngineTest : public testing::Test {
 public:
  EmbeddingEngineTest()
      : model_service_(raw_ref(should_error_)),
        engine_(std::make_unique<EmbeddingEngine>(raw_ref(model_service_))) {
    mojo::core::Init();
  }

 protected:
  base::test::TaskEnvironment task_environment_;

  // Controls the result of next GenerateEmbeddings call.
  bool should_error_ = false;

  FakeEmbeddingModelService model_service_;

  std::unique_ptr<EmbeddingEngine> engine_;
};

TEST_F(EmbeddingEngineTest, Success) {
  auto request = GetFakeGroupRequest();

  TestFuture<mojom::GroupRequestPtr, CoralResult<EmbeddingResponse>>
      embedding_future;
  engine_->Process(std::move(request), embedding_future.GetCallback());
  auto [_, result] = embedding_future.Take();
  ASSERT_TRUE(result.has_value());
  EmbeddingResponse response = std::move(*result);
  auto fake_response = GetFakeEmbeddingResponse();
  EXPECT_EQ(response, fake_response);
}

TEST_F(EmbeddingEngineTest, NoInput) {
  auto request = mojom::GroupRequest::New();
  request->embedding_options = mojom::EmbeddingOptions::New();
  request->clustering_options = mojom::ClusteringOptions::New();
  request->title_generation_options = mojom::TitleGenerationOptions::New();

  TestFuture<mojom::GroupRequestPtr, CoralResult<EmbeddingResponse>>
      embedding_future;
  engine_->Process(std::move(request), embedding_future.GetCallback());
  auto [_, result] = embedding_future.Take();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->embeddings.size(), 0);
}

TEST_F(EmbeddingEngineTest, ExecuteError) {
  auto request = GetFakeGroupRequest();
  should_error_ = true;

  TestFuture<mojom::GroupRequestPtr, CoralResult<EmbeddingResponse>>
      embedding_future;
  engine_->Process(std::move(request), embedding_future.GetCallback());
  auto [_, result] = embedding_future.Take();
  EXPECT_EQ(result.error(), mojom::CoralError::kModelExecutionFailed);
}

TEST_F(EmbeddingEngineTest, InvalidInput) {
  auto request = mojom::GroupRequest::New();
  request->embedding_options = mojom::EmbeddingOptions::New();
  request->clustering_options = mojom::ClusteringOptions::New();
  request->title_generation_options = mojom::TitleGenerationOptions::New();
  request->entities.push_back(mojom::Entity::NewUnknown(false));

  TestFuture<mojom::GroupRequestPtr, CoralResult<EmbeddingResponse>>
      embedding_future;
  engine_->Process(std::move(request), embedding_future.GetCallback());
  auto [_, result] = embedding_future.Take();
  EXPECT_EQ(result.error(), mojom::CoralError::kInvalidArgs);
}

// Test that multiple Process at the same time, without the previous call
// returning, will still have only loaded the model once. Ensures no potential
// race condition happens.
TEST_F(EmbeddingEngineTest, ModelOnlyLoadedOnce) {
  EXPECT_CALL(model_service_, LoadEmbeddingModel).Times(1);
  auto request = GetFakeGroupRequest();

  engine_->Process(request.Clone(), base::DoNothing());
  engine_->Process(std::move(request), base::DoNothing());
}

}  // namespace coral
