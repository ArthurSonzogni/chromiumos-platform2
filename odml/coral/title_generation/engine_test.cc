// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/coral/title_generation/engine.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

#include <base/files/scoped_temp_dir.h>
#include <base/run_loop.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <metrics/metrics_library_mock.h>
#include <ml_core/dlc/dlc_client.h>
#include <mojo/core/embedder/embedder.h>

#include "odml/coral/metrics.h"
#include "odml/coral/test_util.h"
#include "odml/mojom/coral_service.mojom-shared.h"
#include "odml/mojom/coral_service.mojom.h"
#include "odml/on_device_model/fake/on_device_model_fake.h"
#include "odml/on_device_model/features.h"
#include "odml/on_device_model/on_device_model_service.h"
#include "odml/session_state_manager/session_state_manager.h"
#include "odml/utils/odml_shim_loader_mock.h"

namespace coral {
namespace {
using base::test::TaskEnvironment;
using base::test::TestFuture;
using ::testing::_;
using ::testing::AnyNumber;
using ::testing::Gt;
using ::testing::InSequence;
using ::testing::IsEmpty;
using ::testing::Le;
using ::testing::NiceMock;
using ::testing::Optional;
using ::testing::Return;

constexpr char kFakeModelName[] = "5f30b8ca-2447-445e-9716-a6da073fae51";

std::vector<mojom::EntityPtr> CloneEntities(
    const std::vector<mojom::EntityPtr>& entities) {
  std::vector<mojom::EntityPtr> ret;
  for (const mojom::EntityPtr& entity : entities) {
    ret.push_back(entity.Clone());
  }
  return ret;
}

}  // namespace

class MockTitleCacheStorage : public TitleCacheStorageInterface {
 public:
  MockTitleCacheStorage() = default;

  MOCK_METHOD(bool,
              Load,
              (const odml::SessionStateManagerInterface::User&,
               (base::HashingLRUCache<std::string, TitleCacheEntry>&)),
              (override));
  MOCK_METHOD(bool,
              Save,
              (const odml::SessionStateManagerInterface::User&,
               (const base::HashingLRUCache<std::string, TitleCacheEntry>&)),
              (override));
};

class FakeObserver : public mojom::TitleObserver {
 public:
  // We need to know the expected number of updates, and end the run loop after
  // receiving all of them.
  FakeObserver(int expected_updates, base::RunLoop* run_loop)
      : expected_updates_(expected_updates), run_loop_(run_loop) {}
  void TitleUpdated(const base::Token& group_id,
                    const std::string& title) override {
    titles_[group_id] = title;
    expected_updates_--;
    if (expected_updates_ == 0) {
      run_loop_->Quit();
    }
  }

  mojo::PendingRemote<mojom::TitleObserver> BindRemote() {
    return receiver_.BindNewPipeAndPassRemote();
  }

  std::optional<std::string> GetTitle(const base::Token& group_id) {
    auto it = titles_.find(group_id);
    if (it == titles_.end()) {
      return std::nullopt;
    }
    return it->second;
  }

 private:
  mojo::Receiver<mojom::TitleObserver> receiver_{this};
  std::unordered_map<base::Token, std::string, base::TokenHash> titles_;

  int expected_updates_;
  base::RunLoop* run_loop_;
};

class TitleGenerationEngineTest : public testing::Test {
 public:
  TitleGenerationEngineTest()
      : coral_metrics_(raw_ref(metrics_)),
        model_service_(raw_ref(metrics_), raw_ref(shim_loader_)) {}
  void SetUp() override {
    task_environment_ = std::make_unique<TaskEnvironment>(
        TaskEnvironment::TimeSource::MOCK_TIME,
        TaskEnvironment::MainThreadType::DEFAULT);

    fake_ml::SetupFakeChromeML(raw_ref(metrics_), raw_ref(shim_loader_));
    mojo::core::Init();
    // A catch-all so that we don't have to explicitly EXPECT every metrics
    // call.
    EXPECT_CALL(metrics_, SendToUMA).Times(AnyNumber());
    EXPECT_CALL(metrics_, SendEnumToUMA).Times(AnyNumber());
    EXPECT_CALL(metrics_, SendTimeToUMA).Times(AnyNumber());
    EXPECT_CALL(metrics_, SendBoolToUMA).Times(AnyNumber());
    EXPECT_CALL(metrics_, SendLinearToUMA).Times(AnyNumber());
    // Set DlcClient to return paths from /build.
    auto dlc_path = base::FilePath("testdata").Append(kFakeModelName);
    cros::DlcClient::SetDlcPathForTest(&dlc_path);
    ON_CALL(shim_loader_, IsShimReady).WillByDefault(Return(true));
    ON_CALL(shim_loader_, GetFunctionPointer("FormatInput"))
        .WillByDefault(Return(reinterpret_cast<void*>(FormatInputSignature(
            [](const std::string& uuid, Feature feature,
               const std::unordered_map<std::string, std::string>& fields)
                -> std::optional<std::string> {
              auto it = fields.find("prompt");
              if (it == fields.end()) {
                return std::nullopt;
              }
              // Do nothing to the input string.
              return it->second;
            }))));
    temp_dir_ = std::make_unique<base::ScopedTempDir>();
    ASSERT_TRUE(temp_dir_->CreateUniqueTempDir());
    std::unique_ptr<TitleCacheStorage> title_cache_storage =
        std::make_unique<TitleCacheStorage>(temp_dir_->GetPath());
    title_cache_storage_ = title_cache_storage.get();

    engine_ = std::make_unique<TitleGenerationEngine>(
        raw_ref(coral_metrics_), raw_ref(model_service_),
        /*session_state_manager=*/nullptr, std::move(title_cache_storage));
  }

 protected:
  void ExpectSendStatus(bool success, int times = 1) {
    if (success) {
      EXPECT_CALL(metrics_,
                  SendEnumToUMA(metrics::kTitleGenerationEngineStatus, 0, _))
          .Times(times);
    } else {
      EXPECT_CALL(metrics_, SendEnumToUMA(metrics::kTitleGenerationEngineStatus,
                                          Gt(0), _))
          .Times(times);
    }
  }

  void ExpectSendLatency(int times) {
    EXPECT_CALL(metrics_, SendTimeToUMA(metrics::kTitleGenerationEngineLatency,
                                        _, _, _, _))
        .Times(times);
  }

  void ExpectSendLoadModelLatency(int times) {
    EXPECT_CALL(
        metrics_,
        SendTimeToUMA(metrics::kLoadTitleGenerationModelLatency, _, _, _, _))
        .Times(times);
  }

  void ExpectSendGenerateTitleMetrics(int times) {
    EXPECT_CALL(metrics_,
                SendTimeToUMA(metrics::kGenerateTitleLatency, _, _, _, _))
        .Times(times);
    EXPECT_CALL(metrics_,
                SendLinearToUMA(metrics::kTitleLengthInCharacters, _, _))
        .Times(times);
    EXPECT_CALL(metrics_, SendLinearToUMA(metrics::kTitleLengthInWords, _, _))
        .Times(times);
  }

  void ExpectSendInputTokenSize(int times) {
    EXPECT_CALL(metrics_,
                SendToUMA(metrics::kTitleGenerationInputTokenSize, _, _, _, _))
        .Times(times);
  }

  void ExpectSendModelLoaded(bool is_loaded, int times = 1) {
    EXPECT_CALL(metrics_,
                SendBoolToUMA(metrics::kTitleGenerationModelLoaded, is_loaded))
        .Times(times);
  }

  void ExpectSendCacheHit(bool is_cache_hit, int times = 1) {
    EXPECT_CALL(metrics_, SendBoolToUMA(metrics::kTitleCacheHit, is_cache_hit))
        .Times(times);
    if (is_cache_hit) {
      EXPECT_CALL(metrics_, SendPercentageToUMA(
                                metrics::kTitleCacheDifferenceRatio, Le(25)))
          .Times(times);
    } else {
      EXPECT_CALL(metrics_, SendPercentageToUMA(
                                metrics::kTitleCacheDifferenceRatio, Gt(25)))
          .Times(times);
    }
  }

  std::unique_ptr<base::test::TaskEnvironment> task_environment_;
  NiceMock<MetricsLibraryMock> metrics_;
  CoralMetrics coral_metrics_;
  NiceMock<odml::OdmlShimLoaderMock> shim_loader_;
  on_device_model::OnDeviceModelService model_service_;
  std::unique_ptr<base::ScopedTempDir> temp_dir_;
  TitleCacheStorage* title_cache_storage_;

  std::unique_ptr<TitleGenerationEngine> engine_;
};

TEST_F(TitleGenerationEngineTest, Success) {
  ExpectSendStatus(true, 2);
  ExpectSendLatency(2);
  ExpectSendLoadModelLatency(1);
  ExpectSendGenerateTitleMetrics(6);
  ExpectSendInputTokenSize(6);
  {
    InSequence s;
    ExpectSendModelLoaded(false);
    ExpectSendModelLoaded(true);
  }
  ExpectSendCacheHit(false, 6);
  // Test that concurrent requests can be handled.
  TestFuture<CoralResult<TitleGenerationResponse>> title_future1, title_future2;
  engine_->Process(GetFakeGroupRequest(), GetFakeClusteringResponse(),
                   mojo::NullRemote(), title_future1.GetCallback());
  engine_->Process(GetFakeGroupRequest(), GetFakeClusteringResponse(),
                   mojo::NullRemote(), title_future2.GetCallback());
  std::vector<CoralResult<TitleGenerationResponse>> results;
  results.push_back(title_future1.Take());
  results.push_back(title_future2.Take());
  for (auto& result : results) {
    ASSERT_TRUE(result.has_value());
    TitleGenerationResponse response = std::move(*result);
    auto fake_response = GetFakeTitleGenerationResponse();
    ASSERT_EQ(response.groups.size(), fake_response.groups.size());
    for (size_t i = 0; i < response.groups.size(); i++) {
      // Fake model generates a certain style of output that is irrelevant to
      // the feature. Instead of matching the output, just ensure that it's not
      // empty.
      EXPECT_THAT(response.groups[i]->title, Optional(Not(IsEmpty())));
      EXPECT_EQ(response.groups[i]->entities,
                fake_response.groups[i]->entities);
    }
  }
}

TEST_F(TitleGenerationEngineTest, PromptTooLarge) {
  ExpectSendStatus(true, 1);
  ExpectSendLatency(1);
  ExpectSendLoadModelLatency(1);
  ExpectSendGenerateTitleMetrics(0);
  ExpectSendInputTokenSize(3);
  ExpectSendModelLoaded(false);
  EXPECT_CALL(shim_loader_, GetFunctionPointer("FormatInput"))
      .WillRepeatedly(Return(reinterpret_cast<void*>(FormatInputSignature(
          [](const std::string& uuid, Feature feature,
             const std::unordered_map<std::string, std::string>& fields)
              -> std::optional<std::string> {
            auto it = fields.find("prompt");
            if (it == fields.end()) {
              return std::nullopt;
            }
            return std::string(1000, '*');
          }))));

  TestFuture<CoralResult<TitleGenerationResponse>> title_future;
  engine_->Process(GetFakeGroupRequest(), GetFakeClusteringResponse(),
                   mojo::NullRemote(), title_future.GetCallback());
  CoralResult<TitleGenerationResponse> result = title_future.Take();
  ASSERT_TRUE(result.has_value());
  TitleGenerationResponse response = std::move(*result);
  auto fake_response = GetFakeTitleGenerationResponse();
  ASSERT_EQ(response.groups.size(), fake_response.groups.size());
  for (size_t i = 0; i < response.groups.size(); i++) {
    EXPECT_THAT(response.groups[i]->title, Optional(IsEmpty()));
    EXPECT_EQ(response.groups[i]->entities, fake_response.groups[i]->entities);
  }
}

TEST_F(TitleGenerationEngineTest, FailThenSuccess) {
  ExpectSendStatus(false);
  ExpectSendStatus(true);
  ExpectSendLatency(1);
  // Override DLC path to a non-existent path.
  auto dlc_path = base::FilePath("not_exist");
  cros::DlcClient::SetDlcPathForTest(&dlc_path);
  TestFuture<CoralResult<TitleGenerationResponse>> title_future1, title_future2;
  engine_->Process(GetFakeGroupRequest(), GetFakeClusteringResponse(),
                   mojo::NullRemote(), title_future1.GetCallback());
  CoralResult<TitleGenerationResponse> result1 = title_future1.Take();
  ASSERT_FALSE(result1.has_value());
  EXPECT_EQ(result1.error(), mojom::CoralError::kLoadModelFailed);

  // Set DlcClient to return paths from /build.
  dlc_path = base::FilePath("testdata").Append(kFakeModelName);
  cros::DlcClient::SetDlcPathForTest(&dlc_path);
  engine_->Process(GetFakeGroupRequest(), GetFakeClusteringResponse(),
                   mojo::NullRemote(), title_future2.GetCallback());
  CoralResult<TitleGenerationResponse> result2 = title_future2.Take();
  ASSERT_TRUE(result2.has_value());
  TitleGenerationResponse response = std::move(*result2);
  auto fake_response = GetFakeTitleGenerationResponse();
  ASSERT_EQ(response.groups.size(), fake_response.groups.size());
  for (size_t i = 0; i < response.groups.size(); i++) {
    // Fake model generates a certain style of output that is irrelevant to
    // the feature. Instead of matching the output, just ensure that it's not
    // null or empty.
    EXPECT_THAT(response.groups[i]->title, Optional(Not(IsEmpty())));
    EXPECT_EQ(response.groups[i]->entities, fake_response.groups[i]->entities);
  }
}

TEST_F(TitleGenerationEngineTest, TitleCaching) {
  ExpectSendStatus(true, 4);
  ExpectSendLatency(4);
  // 1 request out of 3 hits the cache.
  ExpectSendGenerateTitleMetrics(2);
  ExpectSendInputTokenSize(2);
  ExpectSendCacheHit(true, 2);
  ExpectSendCacheHit(false, 2);
  const odml::SessionStateManagerInterface::User user{"fake_user_1",
                                                      "fake_user_hash_1"};
  engine_->OnUserLoggedIn(user);

  // Wait a while, make sure there are no cache flushes.
  task_environment_->FastForwardBy(base::Days(100));
  TitleCacheStorage test_title_cache_storage =
      TitleCacheStorage(temp_dir_->GetPath());
  base::HashingLRUCache<std::string, TitleCacheEntry> read_title_cache(4);
  EXPECT_TRUE(test_title_cache_storage.Load(user, read_title_cache));
  EXPECT_EQ(0, read_title_cache.size());

  // Set up request with 8 items in 1 cluster.
  std::vector<mojom::EntityPtr> entities;
  entities.push_back(mojom::Entity::NewTab(
      mojom::Tab::New("ABC 1", url::mojom::Url::New("abc1.com"))));
  entities.push_back(
      mojom::Entity::NewApp(mojom::App::New("ABC app 1", "abc1")));
  entities.push_back(
      mojom::Entity::NewApp(mojom::App::New("ABC app 2", "abc2")));
  entities.push_back(mojom::Entity::NewTab(
      mojom::Tab::New("DEF", url::mojom::Url::New("def.com"))));
  entities.push_back(mojom::Entity::NewApp(mojom::App::New("DEF app", "def")));
  entities.push_back(mojom::Entity::NewTab(
      mojom::Tab::New("GHI", url::mojom::Url::New("ghi.com"))));
  entities.push_back(mojom::Entity::NewTab(
      mojom::Tab::New("YYY", url::mojom::Url::New("yyy.com"))));
  entities.push_back(mojom::Entity::NewTab(
      mojom::Tab::New("ZZZ", url::mojom::Url::New("zzz.com"))));

  auto get_request = [&entities]() {
    auto request = mojom::GroupRequest::New();
    request->embedding_options = mojom::EmbeddingOptions::New();
    request->clustering_options = mojom::ClusteringOptions::New();
    request->title_generation_options = mojom::TitleGenerationOptions::New();
    request->entities = CloneEntities(entities);
    return request;
  };
  auto get_clustering_response = [&entities]() {
    ClusteringResponse clustering_response;
    clustering_response.clusters.push_back(
        Cluster{.entities = CloneEntities(entities)});
    return clustering_response;
  };

  TestFuture<CoralResult<TitleGenerationResponse>> title_future1;
  engine_->Process(get_request(), get_clustering_response(), mojo::NullRemote(),
                   title_future1.GetCallback());
  CoralResult<TitleGenerationResponse> result1 = title_future1.Take();
  ASSERT_TRUE(result1.has_value());
  TitleGenerationResponse response1 = std::move(*result1);
  ASSERT_EQ(response1.groups.size(), 1);
  ASSERT_TRUE(response1.groups[0]->title.has_value());
  std::string title1 = *response1.groups[0]->title;

  // Wait a while, make sure the cache has been flushed.
  task_environment_->FastForwardBy(base::Days(100));
  EXPECT_TRUE(test_title_cache_storage.Load(user, read_title_cache));
  EXPECT_EQ(1, read_title_cache.size());

  // Clear out the on-disk cache, then wait and make sure we don't flush the
  // existing cache needlessly.
  read_title_cache.Clear();
  EXPECT_TRUE(test_title_cache_storage.Save(user, read_title_cache));
  task_environment_->FastForwardBy(base::Days(100));
  EXPECT_TRUE(test_title_cache_storage.Load(user, read_title_cache));
  EXPECT_EQ(0, read_title_cache.size());

  // Remove 1 item and add 1 item to the cluster. This makes the similarity
  // ratio 0.25, which is lower than the acceptable threshold, so the title
  // should be reused.
  entities.pop_back();
  entities.push_back(mojom::Entity::NewTab(
      mojom::Tab::New("AAA", url::mojom::Url::New("aaa.com"))));
  TestFuture<CoralResult<TitleGenerationResponse>> title_future2;
  engine_->Process(get_request(), get_clustering_response(), mojo::NullRemote(),
                   title_future2.GetCallback());
  CoralResult<TitleGenerationResponse> result2 = title_future2.Take();
  ASSERT_TRUE(result2.has_value());
  TitleGenerationResponse response2 = std::move(*result2);
  ASSERT_EQ(response2.groups.size(), 1);
  ASSERT_TRUE(response2.groups[0]->title.has_value());
  std::string title2 = *response2.groups[0]->title;
  ASSERT_EQ(title1, title2);

  // Logout and log back in, should not affect the result.
  engine_->OnUserLoggedOut();
  engine_->OnUserLoggedIn(user);

  // On disk cache should be overwritten with the correct values.
  EXPECT_TRUE(test_title_cache_storage.Load(user, read_title_cache));
  EXPECT_EQ(1, read_title_cache.size());

  // Remove 2 items and add 2 more items to the cluster. This makes the
  // similarity threshold 50%, so the title won't be reused.
  entities.pop_back();
  entities.pop_back();
  entities.push_back(mojom::Entity::NewTab(
      mojom::Tab::New("BBB", url::mojom::Url::New("bbb.com"))));
  entities.push_back(mojom::Entity::NewTab(
      mojom::Tab::New("CCC", url::mojom::Url::New("ccc.com"))));
  TestFuture<CoralResult<TitleGenerationResponse>> title_future3;
  engine_->Process(get_request(), get_clustering_response(), mojo::NullRemote(),
                   title_future3.GetCallback());
  CoralResult<TitleGenerationResponse> result3 = title_future3.Take();
  ASSERT_TRUE(result3.has_value());
  TitleGenerationResponse response3 = std::move(*result3);
  ASSERT_EQ(response3.groups.size(), 1);
  ASSERT_TRUE(response3.groups[0]->title.has_value());
  std::string title3 = *response3.groups[0]->title;
  EXPECT_NE(title2, title3);

  // Restore it back to the previous state and trigger a cache hit.
  entities.pop_back();
  entities.pop_back();
  entities.push_back(mojom::Entity::NewTab(
      mojom::Tab::New("YYY", url::mojom::Url::New("yyy.com"))));
  entities.push_back(mojom::Entity::NewTab(
      mojom::Tab::New("AAA", url::mojom::Url::New("aaa.com"))));
  TestFuture<CoralResult<TitleGenerationResponse>> title_future4;
  engine_->Process(get_request(), get_clustering_response(), mojo::NullRemote(),
                   title_future4.GetCallback());
  CoralResult<TitleGenerationResponse> result4 = title_future4.Take();
  ASSERT_TRUE(result4.has_value());
  TitleGenerationResponse response4 = std::move(*result4);
  ASSERT_EQ(response4.groups.size(), 1);
  ASSERT_TRUE(response4.groups[0]->title.has_value());
  std::string title4 = *response4.groups[0]->title;
  ASSERT_EQ(title1, title4);
  // Check that the title is at the front of the LRU cache.
  std::optional<std::string> cache_entry =
      engine_->GetNthTitleCacheKeyForTesting(0);
  ASSERT_TRUE(cache_entry.has_value());
  EXPECT_EQ(cache_entry.value(), title1);
}

TEST_F(TitleGenerationEngineTest, TitleCachingDifferentUser) {
  ExpectSendStatus(true, 2);
  ExpectSendLatency(2);
  ExpectSendGenerateTitleMetrics(2);
  ExpectSendInputTokenSize(2);
  ExpectSendCacheHit(false, 2);
  const odml::SessionStateManagerInterface::User user1{"fake_user_1",
                                                       "fake_user_hash_1"};
  engine_->OnUserLoggedIn(user1);
  // Set up request with 8 items in 1 cluster.
  std::vector<mojom::EntityPtr> entities;
  entities.push_back(mojom::Entity::NewTab(
      mojom::Tab::New("ABC 1", url::mojom::Url::New("abc1.com"))));
  entities.push_back(
      mojom::Entity::NewApp(mojom::App::New("ABC app 1", "abc1")));
  entities.push_back(
      mojom::Entity::NewApp(mojom::App::New("ABC app 2", "abc2")));
  entities.push_back(mojom::Entity::NewTab(
      mojom::Tab::New("DEF", url::mojom::Url::New("def.com"))));
  entities.push_back(mojom::Entity::NewApp(mojom::App::New("DEF app", "def")));
  entities.push_back(mojom::Entity::NewTab(
      mojom::Tab::New("GHI", url::mojom::Url::New("ghi.com"))));
  entities.push_back(mojom::Entity::NewTab(
      mojom::Tab::New("YYY", url::mojom::Url::New("yyy.com"))));
  entities.push_back(mojom::Entity::NewTab(
      mojom::Tab::New("ZZZ", url::mojom::Url::New("zzz.com"))));

  auto get_request = [&entities]() {
    auto request = mojom::GroupRequest::New();
    request->embedding_options = mojom::EmbeddingOptions::New();
    request->clustering_options = mojom::ClusteringOptions::New();
    request->title_generation_options = mojom::TitleGenerationOptions::New();
    request->entities = CloneEntities(entities);
    return request;
  };
  auto get_clustering_response = [&entities]() {
    ClusteringResponse clustering_response;
    clustering_response.clusters.push_back(
        Cluster{.entities = CloneEntities(entities)});
    return clustering_response;
  };

  TestFuture<CoralResult<TitleGenerationResponse>> title_future1;
  engine_->Process(get_request(), get_clustering_response(), mojo::NullRemote(),
                   title_future1.GetCallback());
  CoralResult<TitleGenerationResponse> result1 = title_future1.Take();
  ASSERT_TRUE(result1.has_value());
  TitleGenerationResponse response1 = std::move(*result1);
  ASSERT_EQ(response1.groups.size(), 1);
  ASSERT_TRUE(response1.groups[0]->title.has_value());
  std::string title1 = *response1.groups[0]->title;
  engine_->OnUserLoggedOut();

  const odml::SessionStateManagerInterface::User user2{"fake_user_2",
                                                       "fake_user_hash_2"};
  engine_->OnUserLoggedIn(user2);
  // Remove 1 item and add 1 item to the cluster. This makes the similarity
  // lower than the acceptable threshold, but because the user has changed, the
  // cache won't be used.
  entities.pop_back();
  entities.push_back(mojom::Entity::NewTab(
      mojom::Tab::New("AAA", url::mojom::Url::New("aaa.com"))));
  TestFuture<CoralResult<TitleGenerationResponse>> title_future2;
  engine_->Process(get_request(), get_clustering_response(), mojo::NullRemote(),
                   title_future2.GetCallback());
  CoralResult<TitleGenerationResponse> result2 = title_future2.Take();
  ASSERT_TRUE(result2.has_value());
  TitleGenerationResponse response2 = std::move(*result2);
  ASSERT_EQ(response2.groups.size(), 1);
  ASSERT_TRUE(response2.groups[0]->title.has_value());
  std::string title2 = *response2.groups[0]->title;
  EXPECT_NE(title1, title2);
}

TEST_F(TitleGenerationEngineTest, NoGroups) {
  ExpectSendStatus(true);
  ExpectSendLatency(1);
  auto request = GetFakeGroupRequest();

  TestFuture<CoralResult<TitleGenerationResponse>> title_future;
  engine_->Process(std::move(request), ClusteringResponse(), mojo::NullRemote(),
                   title_future.GetCallback());
  CoralResult<TitleGenerationResponse> result = title_future.Take();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->groups.size(), 0);
}

TEST_F(TitleGenerationEngineTest, ObserverSuccess) {
  ExpectSendStatus(true);
  ExpectSendLatency(1);
  base::RunLoop run_loop;
  FakeObserver observer(3, &run_loop);
  auto request = GetFakeGroupRequest();
  auto clustering_response = GetFakeClusteringResponse();

  TestFuture<CoralResult<TitleGenerationResponse>> title_future;
  engine_->Process(std::move(request), std::move(clustering_response),
                   observer.BindRemote(), title_future.GetCallback());
  CoralResult<TitleGenerationResponse> result = title_future.Take();
  ASSERT_TRUE(result.has_value());
  TitleGenerationResponse response = std::move(*result);
  auto fake_response = GetFakeTitleGenerationResponse();
  ASSERT_EQ(response.groups.size(), fake_response.groups.size());
  for (size_t i = 0; i < response.groups.size(); i++) {
    // When the response is returned, the titles shouldn't have been generated
    // yet.
    EXPECT_EQ(response.groups[i]->title, std::nullopt);
    EXPECT_EQ(observer.GetTitle(response.groups[i]->id), std::nullopt);
    EXPECT_EQ(response.groups[i]->entities, fake_response.groups[i]->entities);
  }
  run_loop.Run();
  for (const mojom::GroupPtr& group : response.groups) {
    // Now the titles should have been updated to the observer.
    std::optional<std::string> title = observer.GetTitle(group->id);
    EXPECT_THAT(title, Optional(Not(IsEmpty())));
  }
}

TEST_F(TitleGenerationEngineTest, ObserverFailed) {
  ExpectSendStatus(false);
  ExpectSendLatency(0);
  base::RunLoop run_loop;
  FakeObserver observer(3, &run_loop);
  auto request = GetFakeGroupRequest();
  auto clustering_response = GetFakeClusteringResponse();

  // Override DLC path to a non-existent path.
  auto dlc_path = base::FilePath("not_exist");
  cros::DlcClient::SetDlcPathForTest(&dlc_path);

  TestFuture<CoralResult<TitleGenerationResponse>> title_future;
  engine_->Process(std::move(request), std::move(clustering_response),
                   observer.BindRemote(), title_future.GetCallback());
  CoralResult<TitleGenerationResponse> result = title_future.Take();
  ASSERT_TRUE(result.has_value());
  TitleGenerationResponse response = std::move(*result);
  auto fake_response = GetFakeTitleGenerationResponse();
  ASSERT_EQ(response.groups.size(), fake_response.groups.size());
  for (size_t i = 0; i < response.groups.size(); i++) {
    // When the response is returned, the titles shouldn't have been generated
    // yet.
    EXPECT_EQ(response.groups[i]->title, std::nullopt);
    EXPECT_EQ(observer.GetTitle(response.groups[i]->id), std::nullopt);
    EXPECT_EQ(response.groups[i]->entities, fake_response.groups[i]->entities);
  }
  run_loop.Run();
  for (const mojom::GroupPtr& group : response.groups) {
    // Now the titles should have been updated to the observer, but because
    // model load failed, all the titles will be empty.
    std::optional<std::string> title = observer.GetTitle(group->id);
    ASSERT_TRUE(title.has_value());
    EXPECT_TRUE(title->empty());
  }
}

}  // namespace coral
