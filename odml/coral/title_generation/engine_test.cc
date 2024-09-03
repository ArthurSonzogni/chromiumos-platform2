// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/coral/title_generation/engine.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

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
#include "odml/on_device_model/features.h"
#include "odml/on_device_model/on_device_model_fake.h"
#include "odml/on_device_model/on_device_model_service.h"
#include "odml/utils/odml_shim_loader_mock.h"

namespace coral {
namespace {
using base::test::TestFuture;
using ::testing::NiceMock;
using ::testing::Return;

constexpr char kFakeModelName[] = "5f30b8ca-2447-445e-9716-a6da073fae51";
}  // namespace

class TitleGenerationEngineTest : public testing::Test {
 public:
  TitleGenerationEngineTest()
      : model_service_(raw_ref(metrics_),
                       raw_ref(shim_loader_),
                       on_device_model::GetOnDeviceModelFakeImpl(
                           raw_ref(metrics_), raw_ref(shim_loader_))) {}
  void SetUp() override {
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

  TestFuture<mojom::GroupRequestPtr, CoralResult<TitleGenerationResponse>>
      title_future;
  engine_->Process(std::move(request), std::move(clustering_response),
                   title_future.GetCallback());
  auto [_, result] = title_future.Take();
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

  TestFuture<mojom::GroupRequestPtr, CoralResult<TitleGenerationResponse>>
      title_future;
  engine_->Process(std::move(request), ClusteringResponse(),
                   title_future.GetCallback());
  auto [_, result] = title_future.Take();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->groups.size(), 0);
}

TEST_F(TitleGenerationEngineTest, LoadModelFailed) {
  auto request = GetFakeGroupRequest();
  auto clustering_response = GetFakeClusteringResponse();

  // Override DLC path to a non-existent path.
  auto dlc_path = base::FilePath("not_exist");
  cros::DlcClient::SetDlcPathForTest(&dlc_path);

  TestFuture<mojom::GroupRequestPtr, CoralResult<TitleGenerationResponse>>
      title_future;
  engine_->Process(std::move(request), std::move(clustering_response),
                   title_future.GetCallback());
  auto [_, result] = title_future.Take();
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), mojom::CoralError::kLoadModelFailed);
}

}  // namespace coral
