// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/coral/title_generation/engine.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

#include <base/run_loop.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <metrics/metrics_library_mock.h>
#include <ml_core/dlc/dlc_client.h>
#include <mojo/core/embedder/embedder.h>

#include "odml/coral/test_util.h"
#include "odml/mojom/coral_service.mojom-shared.h"
#include "odml/mojom/coral_service.mojom.h"
#include "odml/on_device_model/fake/on_device_model_fake.h"
#include "odml/on_device_model/features.h"
#include "odml/on_device_model/on_device_model_service.h"
#include "odml/utils/odml_shim_loader_mock.h"

namespace coral {
namespace {
using base::test::TestFuture;
using ::testing::NiceMock;
using ::testing::Return;

constexpr char kFakeModelName[] = "5f30b8ca-2447-445e-9716-a6da073fae51";
}  // namespace

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
      : model_service_(raw_ref(metrics_), raw_ref(shim_loader_)) {}
  void SetUp() override {
    fake_ml::SetupFakeChromeML(raw_ref(metrics_), raw_ref(shim_loader_));
    mojo::core::Init();
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
    engine_ = std::make_unique<TitleGenerationEngine>(raw_ref(model_service_));
  }

 protected:
  base::test::TaskEnvironment task_environment_;
  NiceMock<MetricsLibraryMock> metrics_;
  NiceMock<odml::OdmlShimLoaderMock> shim_loader_;
  on_device_model::OnDeviceModelService model_service_;

  std::unique_ptr<TitleGenerationEngine> engine_;
};

TEST_F(TitleGenerationEngineTest, Success) {
  auto request = GetFakeGroupRequest();
  auto clustering_response = GetFakeClusteringResponse();

  TestFuture<CoralResult<TitleGenerationResponse>> title_future;
  engine_->Process(std::move(request), std::move(clustering_response),
                   mojo::NullRemote(), title_future.GetCallback());
  CoralResult<TitleGenerationResponse> result = title_future.Take();
  ASSERT_TRUE(result.has_value());
  TitleGenerationResponse response = std::move(*result);
  auto fake_response = GetFakeTitleGenerationResponse();
  ASSERT_EQ(response.groups.size(), fake_response.groups.size());
  for (size_t i = 0; i < response.groups.size(); i++) {
    // Fake model generates a certain style of output that is irrelevant to the
    // feature. Instead of matching the output, just ensure that it's not empty.
    EXPECT_FALSE(response.groups[i]->title.empty());
    EXPECT_EQ(response.groups[i]->entities, fake_response.groups[i]->entities);
  }
}

TEST_F(TitleGenerationEngineTest, NoGroups) {
  auto request = GetFakeGroupRequest();

  TestFuture<CoralResult<TitleGenerationResponse>> title_future;
  engine_->Process(std::move(request), ClusteringResponse(), mojo::NullRemote(),
                   title_future.GetCallback());
  CoralResult<TitleGenerationResponse> result = title_future.Take();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->groups.size(), 0);
}

TEST_F(TitleGenerationEngineTest, LoadModelFailed) {
  auto request = GetFakeGroupRequest();
  auto clustering_response = GetFakeClusteringResponse();

  // Override DLC path to a non-existent path.
  auto dlc_path = base::FilePath("not_exist");
  cros::DlcClient::SetDlcPathForTest(&dlc_path);

  TestFuture<CoralResult<TitleGenerationResponse>> title_future;
  engine_->Process(std::move(request), std::move(clustering_response),
                   mojo::NullRemote(), title_future.GetCallback());
  CoralResult<TitleGenerationResponse> result = title_future.Take();
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), mojom::CoralError::kLoadModelFailed);
}

// Test that multiple Process at the same time, without the previous call
// returning, will still have only loaded the model once, and both calls will
// have received the correct model load result.
TEST_F(TitleGenerationEngineTest, ConcurrentModelLoadFailed) {
  auto request = GetFakeGroupRequest();

  // Override DLC path to a non-existent path.
  auto dlc_path = base::FilePath("not_exist");
  cros::DlcClient::SetDlcPathForTest(&dlc_path);

  TestFuture<CoralResult<TitleGenerationResponse>> title_future1, title_future2;
  engine_->Process(request.Clone(), GetFakeClusteringResponse(),
                   mojo::NullRemote(), title_future1.GetCallback());
  engine_->Process(std::move(request), GetFakeClusteringResponse(),
                   mojo::NullRemote(), title_future2.GetCallback());

  auto fake_response = GetFakeTitleGenerationResponse();
  CoralResult<TitleGenerationResponse> result = title_future1.Take();
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), mojom::CoralError::kLoadModelFailed);

  result = title_future2.Take();
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), mojom::CoralError::kLoadModelFailed);
}

TEST_F(TitleGenerationEngineTest, ConcurrentModelLoadSuccess) {
  auto request = GetFakeGroupRequest();

  TestFuture<CoralResult<TitleGenerationResponse>> title_future1, title_future2;
  engine_->Process(request.Clone(), GetFakeClusteringResponse(),
                   mojo::NullRemote(), title_future1.GetCallback());
  engine_->Process(std::move(request), GetFakeClusteringResponse(),
                   mojo::NullRemote(), title_future2.GetCallback());

  auto fake_response = GetFakeTitleGenerationResponse();
  CoralResult<TitleGenerationResponse> result = title_future1.Take();
  ASSERT_TRUE(result.has_value());
  TitleGenerationResponse response = std::move(*result);
  ASSERT_EQ(response.groups.size(), fake_response.groups.size());

  result = title_future2.Take();
  response = std::move(*result);
  ASSERT_EQ(response.groups.size(), fake_response.groups.size());
}

TEST_F(TitleGenerationEngineTest, ObserverSuccess) {
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
    EXPECT_TRUE(response.groups[i]->title.empty());
    EXPECT_EQ(observer.GetTitle(response.groups[i]->id), std::nullopt);
    EXPECT_EQ(response.groups[i]->entities, fake_response.groups[i]->entities);
  }
  run_loop.Run();
  for (const mojom::GroupPtr& group : response.groups) {
    // Now the titles should have been updated to the observer.
    std::optional<std::string> title = observer.GetTitle(group->id);
    ASSERT_TRUE(title.has_value());
    EXPECT_FALSE(title->empty());
  }
}

TEST_F(TitleGenerationEngineTest, ObserverFailed) {
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
    EXPECT_TRUE(response.groups[i]->title.empty());
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
