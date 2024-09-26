// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/coral/embedding/engine.h"

#include <memory>
#include <string>
#include <utility>

#include <base/files/scoped_temp_file.h>
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
#include "odml/session_state_manager/fake_session_state_manager.h"

namespace coral {
namespace {
using base::test::TestFuture;
using ::testing::_;
using ::testing::ElementsAreArray;
using ::testing::NiceMock;
using ::testing::Optional;
using ::testing::Return;

using embedding_model::mojom::OnDeviceEmbeddingModel;
using embedding_model::mojom::OnDeviceEmbeddingModelInferenceError;
using embedding_model::mojom::OnDeviceEmbeddingModelService;

class FakeEmbeddingModel : public OnDeviceEmbeddingModel {
 public:
  explicit FakeEmbeddingModel(
      const raw_ref<bool> should_error,
      std::vector<Embedding> embeddings_to_return,
      mojo::PendingReceiver<OnDeviceEmbeddingModel> receiver)
      : should_error_(should_error),
        embeddings_to_return_(std::move(embeddings_to_return)),
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
          model_ = std::make_unique<FakeEmbeddingModel>(
              should_error_, GetFakeEmbeddingResponse().embeddings,
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

class FakeEmbeddingDatabaseFactory : public EmbeddingDatabaseFactory {
 public:
  MOCK_METHOD(std::unique_ptr<EmbeddingDatabase>,
              Create,
              (const base::FilePath& file_path, base::TimeDelta ttl),
              (const override));
};

}  // namespace

class EmbeddingEngineTest : public testing::Test {
 public:
  EmbeddingEngineTest()
      : model_service_(raw_ref(should_error_)),
        embedding_database_factory_(new FakeEmbeddingDatabaseFactory()),
        session_state_manager_(
            std::make_unique<odml::FakeSessionStateManager>()) {
    mojo::core::Init();

    EXPECT_CALL(*session_state_manager_, AddObserver(_)).Times(1);
    // ownership of |embedding_database_factory_| is transferred to |engine_|.
    engine_ = std::make_unique<EmbeddingEngine>(
        raw_ref(model_service_), base::WrapUnique(embedding_database_factory_),
        session_state_manager_.get());
  }

 protected:
  base::test::TaskEnvironment task_environment_;

  // Controls the result of next GenerateEmbeddings call.
  bool should_error_ = false;

  FakeEmbeddingModelService model_service_;

  FakeEmbeddingDatabaseFactory* embedding_database_factory_;

  std::unique_ptr<odml::FakeSessionStateManager> session_state_manager_;

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
// returning, will still have only loaded the model once, and both calls will
// have received the correct model load result.
TEST_F(EmbeddingEngineTest, ConcurrentModelLoadFailed) {
  base::OnceCallback<void(on_device_model::mojom::LoadModelResult)>
      load_model_callback;
  EXPECT_CALL(model_service_, LoadEmbeddingModel)
      .WillOnce([&](auto&&, auto&&, auto&&, auto&& callback) {
        load_model_callback = std::move(callback);
      });
  auto request = GetFakeGroupRequest();

  TestFuture<mojom::GroupRequestPtr, CoralResult<EmbeddingResponse>>
      embedding_future1, embedding_future2;
  engine_->Process(request.Clone(), embedding_future1.GetCallback());
  engine_->Process(std::move(request), embedding_future2.GetCallback());

  ASSERT_TRUE(load_model_callback);
  std::move(load_model_callback)
      .Run(on_device_model::mojom::LoadModelResult::kFailedToLoadLibrary);

  auto [_, result] = embedding_future1.Take();
  EXPECT_EQ(result.error(), mojom::CoralError::kLoadModelFailed);

  std::tie(_, result) = embedding_future2.Take();
  EXPECT_EQ(result.error(), mojom::CoralError::kLoadModelFailed);
}

TEST_F(EmbeddingEngineTest, ConcurrentModelLoadSuccess) {
  auto request = mojom::GroupRequest::New();
  request->embedding_options = mojom::EmbeddingOptions::New();
  request->clustering_options = mojom::ClusteringOptions::New();
  request->title_generation_options = mojom::TitleGenerationOptions::New();
  request->entities.push_back(mojom::Entity::NewTab(
      mojom::Tab::New("ABC 1", url::mojom::Url::New("abc1.com"))));

  TestFuture<mojom::GroupRequestPtr, CoralResult<EmbeddingResponse>>
      embedding_future1, embedding_future2;
  engine_->Process(request.Clone(), embedding_future1.GetCallback());
  engine_->Process(std::move(request), embedding_future2.GetCallback());

  auto fake_response = GetFakeEmbeddingResponse();
  auto [_, result] = embedding_future1.Take();
  EXPECT_TRUE(result.has_value());

  std::tie(_, result) = embedding_future2.Take();
  EXPECT_TRUE(result.has_value());
}

TEST_F(EmbeddingEngineTest, WithEmbeddingDatabase) {
  auto request = GetFakeGroupRequest();
  std::vector<Embedding> fake_embeddings =
      GetFakeEmbeddingResponse().embeddings;
  std::vector<std::string> cache_keys;
  for (const mojom::EntityPtr& entity : request->entities) {
    std::optional<std::string> cache_key = internal::EntityToCacheKey(
        *entity, internal::EntityToEmbeddingPrompt(*entity), "1.0");
    ASSERT_TRUE(cache_key.has_value());
    cache_keys.push_back(std::move(*cache_key));
  }

  // Fake database for fake user 1.
  base::ScopedTempFile database_file_1;
  ASSERT_TRUE(database_file_1.Create());
  std::unique_ptr<EmbeddingDatabase> database_1 =
      EmbeddingDatabase::Create(database_file_1.path(), base::Seconds(0));
  ASSERT_TRUE(database_1);
  database_1->Put(cache_keys[1], fake_embeddings[1]);
  database_1->Put(cache_keys[4], fake_embeddings[4]);
  EmbeddingDatabase* raw_database_1 = database_1.get();

  // Fake database for fake user 2.
  base::ScopedTempFile database_file_2;
  ASSERT_TRUE(database_file_2.Create());
  std::unique_ptr<EmbeddingDatabase> database_2 =
      EmbeddingDatabase::Create(database_file_2.path(), base::Seconds(0));
  ASSERT_TRUE(database_2);
  database_2->Put(cache_keys[0], fake_embeddings[0]);
  database_2->Put(cache_keys[5], fake_embeddings[5]);
  EmbeddingDatabase* raw_database_2 = database_2.get();

  // Ownership of |database_1| and |database_2| are transferred.
  EXPECT_CALL(*embedding_database_factory_, Create(_, _))
      .WillOnce(Return(std::move(database_1)))
      .WillOnce(Return(std::move(database_2)));

  std::unique_ptr<FakeEmbeddingModel> fake_model;
  bool should_error = false;
  std::vector<Embedding> embeddings_to_return = {
      // Called by the first Process() for fake user 1.
      fake_embeddings[0], fake_embeddings[2], fake_embeddings[3],
      fake_embeddings[5],
      // Called by the second Process() with no user logged in.
      fake_embeddings[0], fake_embeddings[1], fake_embeddings[2],
      fake_embeddings[3], fake_embeddings[4], fake_embeddings[5],
      // Called by the first Process() for fake user 2.
      fake_embeddings[1], fake_embeddings[2], fake_embeddings[3],
      fake_embeddings[4]};
  EXPECT_CALL(model_service_, LoadEmbeddingModel)
      .WillOnce([&fake_model, &should_error, &embeddings_to_return](
                    auto&&, auto&& model, auto&&, auto&& callback) {
        fake_model = std::make_unique<FakeEmbeddingModel>(
            raw_ref(should_error), std::move(embeddings_to_return),
            std::move(model));
        std::move(callback).Run(
            on_device_model::mojom::LoadModelResult::kSuccess);
      });

  engine_->OnUserLoggedIn(::odml::SessionStateManagerInterface::User(
      "fake_user_1", "fake_user_hash_1"));
  {
    TestFuture<mojom::GroupRequestPtr, CoralResult<EmbeddingResponse>>
        embedding_future;

    engine_->Process(request.Clone(), embedding_future.GetCallback());

    auto [_, result] = embedding_future.Take();
    ASSERT_TRUE(result.has_value());
    EmbeddingResponse response = std::move(*result);
    auto fake_response = GetFakeEmbeddingResponse();
    EXPECT_EQ(response, fake_response);

    EXPECT_THAT(raw_database_1->Get(cache_keys[0]),
                Optional(ElementsAreArray(fake_embeddings[0])));
    EXPECT_THAT(raw_database_1->Get(cache_keys[2]),
                Optional(ElementsAreArray(fake_embeddings[2])));
    EXPECT_THAT(raw_database_1->Get(cache_keys[3]),
                Optional(ElementsAreArray(fake_embeddings[3])));
    EXPECT_THAT(raw_database_1->Get(cache_keys[5]),
                Optional(ElementsAreArray(fake_embeddings[5])));
  }

  engine_->OnUserLoggedOut();
  {
    TestFuture<mojom::GroupRequestPtr, CoralResult<EmbeddingResponse>>
        embedding_future;
    engine_->Process(request.Clone(), embedding_future.GetCallback());
    auto [_, result] = embedding_future.Take();
    ASSERT_TRUE(result.has_value());
    EmbeddingResponse response = std::move(*result);
    auto fake_response = GetFakeEmbeddingResponse();
    EXPECT_EQ(response, fake_response);
  }

  engine_->OnUserLoggedIn(::odml::SessionStateManagerInterface::User(
      "fake_user_2", "fake_user_hash_2"));
  {
    TestFuture<mojom::GroupRequestPtr, CoralResult<EmbeddingResponse>>
        embedding_future;
    engine_->Process(request.Clone(), embedding_future.GetCallback());
    auto [_, result] = embedding_future.Take();
    ASSERT_TRUE(result.has_value());
    EmbeddingResponse response = std::move(*result);
    auto fake_response = GetFakeEmbeddingResponse();
    EXPECT_EQ(response, fake_response);

    EXPECT_THAT(raw_database_2->Get(cache_keys[1]),
                Optional(ElementsAreArray(fake_embeddings[1])));
    EXPECT_THAT(raw_database_2->Get(cache_keys[2]),
                Optional(ElementsAreArray(fake_embeddings[2])));
    EXPECT_THAT(raw_database_2->Get(cache_keys[3]),
                Optional(ElementsAreArray(fake_embeddings[3])));
    EXPECT_THAT(raw_database_2->Get(cache_keys[4]),
                Optional(ElementsAreArray(fake_embeddings[4])));
  }
}

}  // namespace coral
