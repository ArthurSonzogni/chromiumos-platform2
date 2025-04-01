// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/coral/embedding/engine.h"

#include <memory>
#include <string>
#include <utility>

#include <base/files/scoped_temp_file.h>
#include <base/test/gmock_callback_support.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <metrics/metrics_library_mock.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <mojo/public/cpp/bindings/receiver_set.h>

#include "odml/coral/metrics.h"
#include "odml/coral/test_util.h"
#include "odml/cros_safety/safety_service_manager_mock.h"
#include "odml/i18n/mock_language_detector.h"
#include "odml/i18n/mock_translator.h"
#include "odml/mojom/coral_service.mojom.h"
#include "odml/mojom/embedding_model.mojom.h"
#include "odml/session_state_manager/fake_session_state_manager.h"

namespace coral {
namespace {
using TextLanguage = ::on_device_model::LanguageDetector::TextLanguage;
using base::test::TestFuture;
using ::testing::_;
using ::testing::AnyNumber;
using ::testing::ElementsAreArray;
using ::testing::Gt;
using ::testing::InSequence;
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
          std::vector<Embedding> embeddings_to_return;
          for (const EmbeddingWithMetadata& embedding :
               GetFakeEmbeddingResponse().embeddings) {
            embeddings_to_return.push_back(embedding.embedding);
          }
          model_ = std::make_unique<FakeEmbeddingModel>(
              should_error_, embeddings_to_return, std::move(model));
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
  MOCK_METHOD(std::unique_ptr<EmbeddingDatabaseInterface>,
              Create,
              (raw_ref<CoralMetrics> metrics,
               const base::FilePath& file_path,
               base::TimeDelta ttl),
              (const override));
};

class FakeEmbeddingDatabase : public EmbeddingDatabaseInterface {
 public:
  MOCK_METHOD(void,
              Put,
              (std::string key, EmbeddingEntry embedding),
              (override));
  MOCK_METHOD(EmbeddingEntry, Get, (const std::string&), (override));
  MOCK_METHOD(bool, Sync, (), (override));
};

}  // namespace

class EmbeddingEngineTest : public testing::Test {
 public:
  EmbeddingEngineTest()
      : coral_metrics_(raw_ref(metrics_)),
        model_service_(raw_ref(should_error_)),
        embedding_database_factory_(new FakeEmbeddingDatabaseFactory()),
        session_state_manager_(
            std::make_unique<odml::FakeSessionStateManager>()) {
    mojo::core::Init();

    // A catch-all so that we don't have to explicitly EXPECT every metrics
    // call.
    EXPECT_CALL(metrics_, SendEnumToUMA).Times(AnyNumber());
    EXPECT_CALL(metrics_, SendTimeToUMA).Times(AnyNumber());
    EXPECT_CALL(metrics_, SendBoolToUMA).Times(AnyNumber());
    EXPECT_CALL(*session_state_manager_, AddObserver(_)).Times(1);
    // ownership of |embedding_database_factory_| is transferred to |engine_|.
    engine_ = std::make_unique<EmbeddingEngine>(
        raw_ref(coral_metrics_), raw_ref(model_service_),
        raw_ref(safety_service_manager_),
        base::WrapUnique(embedding_database_factory_),
        session_state_manager_.get(), raw_ref(language_detector_),
        raw_ref(translator_));

    EXPECT_CALL(safety_service_manager_, ClassifyTextSafety)
        .WillRepeatedly(base::test::RunOnceCallbackRepeatedly<2>(
            cros_safety::mojom::SafetyClassifierVerdict::kPass));
    ON_CALL(language_detector_, IsAvailable).WillByDefault(Return(true));
    ON_CALL(language_detector_, Classify)
        .WillByDefault([](auto&&, auto&& callback) {
          std::move(callback).Run(LanguageDetectionResult{
              TextLanguage{.locale = "en", .confidence = 1.0}});
        });
    EXPECT_CALL(language_detector_, Classify).Times(AnyNumber());
  }

 protected:
  void ExpectSendStatus(bool success, int times = 1) {
    if (success) {
      EXPECT_CALL(metrics_,
                  SendEnumToUMA(metrics::kEmbeddingEngineStatus, 0, _))
          .Times(times);
    } else {
      EXPECT_CALL(metrics_,
                  SendEnumToUMA(metrics::kEmbeddingEngineStatus, Gt(0), _))
          .Times(times);
    }
  }

  void ExpectSendLatency(int times) {
    EXPECT_CALL(metrics_,
                SendTimeToUMA(metrics::kEmbeddingEngineLatency, _, _, _, _))
        .Times(times);
  }

  void ExpectSendLoadModelLatency(int times) {
    EXPECT_CALL(metrics_,
                SendTimeToUMA(metrics::kLoadEmbeddingModelLatency, _, _, _, _))
        .Times(times);
  }

  void ExpectSendGenerateEmbeddingLatency(int times) {
    EXPECT_CALL(metrics_,
                SendTimeToUMA(metrics::kGenerateEmbeddingLatency, _, _, _, _))
        .Times(times);
  }

  void ExpectSendModelLoaded(bool is_loaded, int times = 1) {
    EXPECT_CALL(metrics_,
                SendBoolToUMA(metrics::kEmbeddingModelLoaded, is_loaded))
        .Times(times);
  }

  void ExpectSendCacheHit(bool is_cache_hit, int times = 1) {
    EXPECT_CALL(metrics_,
                SendBoolToUMA(metrics::kEmbeddingCacheHit, is_cache_hit))
        .Times(times);
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};

  // Controls the result of next GenerateEmbeddings call.
  bool should_error_ = false;

  NiceMock<MetricsLibraryMock> metrics_;

  CoralMetrics coral_metrics_;

  FakeEmbeddingModelService model_service_;

  FakeEmbeddingDatabaseFactory* embedding_database_factory_;

  std::unique_ptr<odml::FakeSessionStateManager> session_state_manager_;
  cros_safety::SafetyServiceManagerMock safety_service_manager_;

  on_device_model::MockLanguageDetector language_detector_;
  NiceMock<i18n::MockTranslator> translator_;

  std::unique_ptr<EmbeddingEngine> engine_;
};

TEST_F(EmbeddingEngineTest, Success) {
  ExpectSendStatus(true, 2);
  ExpectSendLatency(2);
  ExpectSendLoadModelLatency(1);
  ExpectSendGenerateEmbeddingLatency(12);
  {
    InSequence s;
    ExpectSendModelLoaded(false);
    ExpectSendModelLoaded(true);
  }
  ExpectSendCacheHit(false, 12);
  std::unique_ptr<FakeEmbeddingModel> fake_model;
  bool should_error = false;
  EmbeddingResponse fake_response = GetFakeEmbeddingResponse();
  std::vector<Embedding> embeddings_to_return;
  for (int i = 0; i < 2; i++) {
    for (const EmbeddingWithMetadata& embedding : fake_response.embeddings) {
      embeddings_to_return.push_back(embedding.embedding);
    }
  }
  EXPECT_CALL(model_service_, LoadEmbeddingModel)
      .WillOnce([&fake_model, &should_error, &embeddings_to_return](
                    auto&&, auto&& model, auto&&, auto&& callback) {
        fake_model = std::make_unique<FakeEmbeddingModel>(
            raw_ref(should_error), std::move(embeddings_to_return),
            std::move(model));
        std::move(callback).Run(
            on_device_model::mojom::LoadModelResult::kSuccess);
      });

  TestFuture<mojom::GroupRequestPtr, CoralResult<EmbeddingResponse>>
      embedding_future1, embedding_future2;
  engine_->Process(GetFakeGroupRequest(), embedding_future1.GetCallback());
  engine_->Process(GetFakeGroupRequest(), embedding_future2.GetCallback());
  std::vector<CoralResult<EmbeddingResponse>> results;
  results.push_back(std::get<1>(embedding_future1.Take()));
  results.push_back(std::get<1>(embedding_future2.Take()));
  for (auto& result : results) {
    ASSERT_TRUE(result.has_value());
    EmbeddingResponse response = std::move(*result);
    auto fake_response = GetFakeEmbeddingResponse();
    EXPECT_EQ(response, fake_response);
  }
}

TEST_F(EmbeddingEngineTest, TextLanguage) {
  std::unique_ptr<FakeEmbeddingModel> fake_model;
  bool should_error = false;
  EmbeddingResponse fake_response = GetFakeEmbeddingResponse();
  std::vector<Embedding> embeddings_to_return;
  for (int i = 1; i < fake_response.embeddings.size(); i++) {
    embeddings_to_return.push_back(fake_response.embeddings[i].embedding);
  }
  EXPECT_CALL(model_service_, LoadEmbeddingModel)
      .WillOnce([&fake_model, &should_error, &embeddings_to_return](
                    auto&&, auto&& model, auto&&, auto&& callback) {
        fake_model = std::make_unique<FakeEmbeddingModel>(
            raw_ref(should_error), std::move(embeddings_to_return),
            std::move(model));
        std::move(callback).Run(
            on_device_model::mojom::LoadModelResult::kSuccess);
      });
  // Simulate that one of the entries got classified as an unsupported language.
  EXPECT_CALL(language_detector_, Classify("ABC 1", _))
      .WillOnce([](auto&&, auto&& callback) {
        std::move(callback).Run(LanguageDetectionResult{
            TextLanguage{.locale = "zh", .confidence = 1.0}});
      });
  // Simulate that one of the entries is a non-English supported language. This
  // should trigger a DLC download.
  EXPECT_CALL(language_detector_, Classify("ABC app 1", _))
      .WillOnce([](auto&&, auto&& callback) {
        std::move(callback).Run(LanguageDetectionResult{
            TextLanguage{.locale = "ja", .confidence = 1.0}});
      });
  // Simulate that one of the entries is a language not supported by the
  // language model. This should trigger a DLC download and a translation.
  EXPECT_CALL(language_detector_, Classify("ABC app 2", _))
      .WillOnce([](auto&&, auto&& callback) {
        std::move(callback).Run(LanguageDetectionResult{
            TextLanguage{.locale = "pt", .confidence = 1.0}});
      });
  EXPECT_CALL(translator_, DownloadDlc(i18n::LangPair{"ja", "en"}, _, _))
      .Times(1);
  EXPECT_CALL(translator_, DownloadDlc(i18n::LangPair{"pt", "en"}, _, _))
      .Times(1);
  EXPECT_CALL(translator_,
              Translate(i18n::LangPair{"pt", "en"}, "ABC app 2", _))
      .WillOnce([](auto&&, auto&&, auto&& callback) {
        std::move(callback).Run("ABC app 2 translated");
      });

  TestFuture<mojom::GroupRequestPtr, CoralResult<EmbeddingResponse>>
      embedding_future;
  engine_->Process(GetFakeGroupRequest(), embedding_future.GetCallback());
  auto [_, result] = embedding_future.Take();
  ASSERT_TRUE(result.has_value());
  EmbeddingResponse response = std::move(*result);
  fake_response = GetFakeEmbeddingResponse();
  // The first entry has unsupported language.
  fake_response.embeddings[0].embedding.clear();
  fake_response.embeddings[0].language_result.clear();
  fake_response.embeddings[1].language_result =
      LanguageDetectionResult{TextLanguage{.locale = "ja", .confidence = 1.0}};
  fake_response.embeddings[2].language_result =
      LanguageDetectionResult{TextLanguage{.locale = "pt", .confidence = 1.0}};
  EXPECT_EQ(response, fake_response);
}

TEST_F(EmbeddingEngineTest, CacheEmbeddingsOnlySuccess) {
  EXPECT_CALL(metrics_, SendEnumToUMA(metrics::kEmbeddingEngineStatus, _, _))
      .Times(0);
  EXPECT_CALL(metrics_,
              SendTimeToUMA(metrics::kEmbeddingEngineLatency, _, _, _, _))
      .Times(0);
  EXPECT_CALL(metrics_, SendBoolToUMA(metrics::kEmbeddingModelLoaded, _))
      .Times(0);
  EXPECT_CALL(metrics_, SendBoolToUMA(metrics::kEmbeddingCacheHit, _)).Times(0);
  ExpectSendLoadModelLatency(1);
  ExpectSendGenerateEmbeddingLatency(12);
  // A CacheEmbeddings request has no clustering and title generation options
  // fields.
  auto request = GetFakeGroupRequest();
  request->clustering_options.reset();
  request->title_generation_options.reset();

  std::unique_ptr<FakeEmbeddingModel> fake_model;
  bool should_error = false;
  EmbeddingResponse fake_response = GetFakeEmbeddingResponse();
  std::vector<Embedding> embeddings_to_return;
  for (int i = 0; i < 2; i++) {
    for (const EmbeddingWithMetadata& embedding : fake_response.embeddings) {
      embeddings_to_return.push_back(embedding.embedding);
    }
  }
  EXPECT_CALL(model_service_, LoadEmbeddingModel)
      .WillOnce([&fake_model, &should_error, &embeddings_to_return](
                    auto&&, auto&& model, auto&&, auto&& callback) {
        fake_model = std::make_unique<FakeEmbeddingModel>(
            raw_ref(should_error), std::move(embeddings_to_return),
            std::move(model));
        std::move(callback).Run(
            on_device_model::mojom::LoadModelResult::kSuccess);
      });

  TestFuture<mojom::GroupRequestPtr, CoralResult<EmbeddingResponse>>
      embedding_future1, embedding_future2;
  engine_->Process(request.Clone(), embedding_future1.GetCallback());
  engine_->Process(request.Clone(), embedding_future2.GetCallback());
  std::vector<CoralResult<EmbeddingResponse>> results;
  results.push_back(std::get<1>(embedding_future1.Take()));
  results.push_back(std::get<1>(embedding_future2.Take()));
  for (auto& result : results) {
    ASSERT_TRUE(result.has_value());
    EmbeddingResponse response = std::move(*result);
    auto fake_response = GetFakeEmbeddingResponse();
    EXPECT_EQ(response, fake_response);
  }
}

TEST_F(EmbeddingEngineTest, FailThenSuccess) {
  ExpectSendStatus(false);
  ExpectSendStatus(true);
  ExpectSendLatency(1);
  ExpectSendGenerateEmbeddingLatency(6);
  TestFuture<mojom::GroupRequestPtr, CoralResult<EmbeddingResponse>>
      embedding_future1, embedding_future2;
  should_error_ = true;
  engine_->Process(GetFakeGroupRequest(), embedding_future1.GetCallback());
  auto [req1, result1] = embedding_future1.Take();
  EXPECT_EQ(result1.error(), mojom::CoralError::kModelExecutionFailed);

  should_error_ = false;
  engine_->Process(GetFakeGroupRequest(), embedding_future2.GetCallback());
  auto [req2, result2] = embedding_future2.Take();
  ASSERT_TRUE(result2.has_value());
  EmbeddingResponse response = std::move(*result2);
  auto fake_response = GetFakeEmbeddingResponse();
  EXPECT_EQ(response, fake_response);
}

TEST_F(EmbeddingEngineTest, NoInput) {
  ExpectSendStatus(true);
  ExpectSendLatency(1);
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

TEST_F(EmbeddingEngineTest, InvalidInput) {
  ExpectSendStatus(false);
  ExpectSendLatency(0);
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

TEST_F(EmbeddingEngineTest, WithEmbeddingDatabase) {
  ExpectSendStatus(true, 3);
  ExpectSendLatency(3);
  // 6*3 input embeddings, with 4 cache hits.
  ExpectSendGenerateEmbeddingLatency(14);
  ExpectSendCacheHit(true, 4);
  ExpectSendCacheHit(false, 14);
  auto request = GetFakeGroupRequest();
  std::vector<EmbeddingWithMetadata> fake_embeddings =
      GetFakeEmbeddingResponse().embeddings;
  std::vector<EmbeddingEntry> fake_embedding_entries;
  // When language results are out, the engine will write to database first. At
  // this moment embeddings are not generated yet.
  std::vector<EmbeddingEntry> language_only_entries;
  std::vector<EmbeddingEntry> language_safety_entries;
  for (const auto& fake_embedding : fake_embeddings) {
    fake_embedding_entries.push_back(
        EmbeddingEntry{.embedding = fake_embedding.embedding,
                       .safety_verdict = true,
                       .languages = fake_embedding.language_result});
    language_only_entries.push_back(
        EmbeddingEntry{.languages = LanguageDetectionResult{
                           TextLanguage{.locale = "en", .confidence = 1.0}}});
    language_safety_entries.push_back(
        EmbeddingEntry{.safety_verdict = true,
                       .languages = LanguageDetectionResult{
                           TextLanguage{.locale = "en", .confidence = 1.0}}});
  }
  std::vector<std::string> cache_keys;
  for (const mojom::EntityPtr& entity : request->entities) {
    std::optional<std::string> cache_key = internal::EntityToCacheKey(
        *entity, internal::EntityToEmbeddingPrompt(*entity), "1.0");
    ASSERT_TRUE(cache_key.has_value());
    cache_keys.push_back(std::move(*cache_key));
  }

  // Fake database for fake user 1.
  // Ownership is transferred to |engine_| later.
  FakeEmbeddingDatabase* database_1 = new FakeEmbeddingDatabase();
  EXPECT_CALL(*database_1, Get(_)).Times(AnyNumber());
  EXPECT_CALL(*database_1, Get(cache_keys[1]))
      .WillOnce(Return(fake_embedding_entries[1]));
  EXPECT_CALL(*database_1, Get(cache_keys[4]))
      .WillOnce(Return(fake_embedding_entries[4]));
  EXPECT_CALL(*database_1, Put(cache_keys[0], language_only_entries[0]))
      .Times(1);
  EXPECT_CALL(*database_1, Put(cache_keys[2], language_only_entries[2]))
      .Times(1);
  EXPECT_CALL(*database_1, Put(cache_keys[3], language_only_entries[3]))
      .Times(1);
  EXPECT_CALL(*database_1, Put(cache_keys[5], language_only_entries[5]))
      .Times(1);
  EXPECT_CALL(*database_1, Put(cache_keys[0], language_safety_entries[0]))
      .Times(1);
  EXPECT_CALL(*database_1, Put(cache_keys[2], language_safety_entries[2]))
      .Times(1);
  EXPECT_CALL(*database_1, Put(cache_keys[3], language_safety_entries[3]))
      .Times(1);
  EXPECT_CALL(*database_1, Put(cache_keys[5], language_safety_entries[5]))
      .Times(1);
  EXPECT_CALL(*database_1, Put(cache_keys[0], fake_embedding_entries[0]))
      .Times(1);
  EXPECT_CALL(*database_1, Put(cache_keys[2], fake_embedding_entries[2]))
      .Times(1);
  EXPECT_CALL(*database_1, Put(cache_keys[3], fake_embedding_entries[3]))
      .Times(1);
  EXPECT_CALL(*database_1, Put(cache_keys[5], fake_embedding_entries[5]))
      .Times(1);

  // Fake database for fake user 2.
  // Ownership is transferred to |engine_| later.
  FakeEmbeddingDatabase* database_2 = new FakeEmbeddingDatabase();
  EXPECT_CALL(*database_2, Get(_)).Times(AnyNumber());
  EXPECT_CALL(*database_2, Get(cache_keys[0]))
      .WillOnce(Return(fake_embedding_entries[0]));
  EXPECT_CALL(*database_2, Get(cache_keys[5]))
      .WillOnce(Return(fake_embedding_entries[5]));
  EXPECT_CALL(*database_2, Put(cache_keys[1], language_only_entries[1]))
      .Times(1);
  EXPECT_CALL(*database_2, Put(cache_keys[2], language_only_entries[2]))
      .Times(1);
  EXPECT_CALL(*database_2, Put(cache_keys[3], language_only_entries[3]))
      .Times(1);
  EXPECT_CALL(*database_2, Put(cache_keys[4], language_only_entries[4]))
      .Times(1);
  EXPECT_CALL(*database_2, Put(cache_keys[1], language_safety_entries[1]))
      .Times(1);
  EXPECT_CALL(*database_2, Put(cache_keys[2], language_safety_entries[2]))
      .Times(1);
  EXPECT_CALL(*database_2, Put(cache_keys[3], language_safety_entries[3]))
      .Times(1);
  EXPECT_CALL(*database_2, Put(cache_keys[4], language_safety_entries[4]))
      .Times(1);
  EXPECT_CALL(*database_2, Put(cache_keys[1], fake_embedding_entries[1]))
      .Times(1);
  EXPECT_CALL(*database_2, Put(cache_keys[2], fake_embedding_entries[2]))
      .Times(1);
  EXPECT_CALL(*database_2, Put(cache_keys[3], fake_embedding_entries[3]))
      .Times(1);
  EXPECT_CALL(*database_2, Put(cache_keys[4], fake_embedding_entries[4]))
      .Times(1);

  // Ownership of |database_1| and |database_2| are transferred.
  EXPECT_CALL(*embedding_database_factory_, Create(_, _, _))
      .WillOnce(Return(base::WrapUnique(database_1)))
      .WillOnce(Return(base::WrapUnique(database_2)));

  std::unique_ptr<FakeEmbeddingModel> fake_model;
  bool should_error = false;
  std::vector<Embedding> embeddings_to_return = {
      // Called by the first Process() for fake user 1.
      fake_embeddings[0].embedding, fake_embeddings[2].embedding,
      fake_embeddings[3].embedding, fake_embeddings[5].embedding,
      // Called by the second Process() with no user logged in.
      fake_embeddings[0].embedding, fake_embeddings[1].embedding,
      fake_embeddings[2].embedding, fake_embeddings[3].embedding,
      fake_embeddings[4].embedding, fake_embeddings[5].embedding,
      // Called by the first Process() for fake user 2.
      fake_embeddings[1].embedding, fake_embeddings[2].embedding,
      fake_embeddings[3].embedding, fake_embeddings[4].embedding};
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
  }
  EXPECT_CALL(*database_1, Sync()).Times(3);
  task_environment_.FastForwardBy(internal::kEmbeddingDatabaseSyncPeriod * 3);

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
  // Doesn't increase count of Sync() calls of |database_1|.
  task_environment_.FastForwardBy(internal::kEmbeddingDatabaseSyncPeriod * 3);

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
  }
  EXPECT_CALL(*database_2, Sync()).Times(5);
  task_environment_.FastForwardBy(internal::kEmbeddingDatabaseSyncPeriod * 5);
}
}  // namespace coral
