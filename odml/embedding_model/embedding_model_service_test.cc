// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/embedding_model/embedding_model_service.h"

#include <memory>
#include <string>
#include <vector>

#include <base/test/bind.h>
#include <base/test/task_environment.h>
#include <metrics/metrics_library_mock.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/public/cpp/bindings/pending_remote.h>
#include <mojo/public/cpp/bindings/remote.h>
#include <testing/gmock/include/gmock/gmock.h>
#include <testing/gtest/include/gtest/gtest.h>

#include "odml/embedding_model/model_factory_mock.h"
#include "odml/embedding_model/model_runner_mock.h"
#include "odml/mojom/embedding_model.mojom.h"

namespace embedding_model {
namespace {

constexpr char kFakeModelUuid1[] = "961e0724-020b-4e97-aa83-735cc894da6e";
constexpr char kFakeModelVersion1[] = "FakeModelVersion1";
constexpr char kTestContent1[] = "Some content 1";
constexpr char kTestContent2[] = "Some content 2";
constexpr float kFakeEmbedding1[] = {0.1, 0.2, 0.3, 0.4};
constexpr float kFakeEmbedding2[] = {0.7, 0.8, 0.9, 0.1};

using ::on_device_model::mojom::LoadModelResult;
using ::testing::AtLeast;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;

class EmbeddingModelServiceTest : public testing::Test {
 public:
  EmbeddingModelServiceTest()
      : service_impl_(raw_ref(metrics_), raw_ref(model_factory_)) {}

  void SetUp() override {
    mojo::core::Init();
    service_impl_.AddReceiver(remote_.BindNewPipeAndPassReceiver());

    ON_CALL(model_factory_, BuildRunnerFromUuid)
        .WillByDefault(
            Invoke([&](const base::Uuid& uuid,
                       ModelFactory::BuildRunnerFromUuidCallback callback) {
              for (int i = 0; i < pending_runner_build_.size(); i++) {
                if (pending_runner_build_[i].first == uuid) {
                  std::move(callback).Run(
                      std::move(pending_runner_build_[i].second));
                  pending_runner_build_.erase(pending_runner_build_.begin() +
                                              i);
                  return;
                }
              }
              // See if there's a match in the defer_runner_build_
              for (int i = 0; i < defer_runner_build_.size(); i++) {
                if (defer_runner_build_[i] == uuid) {
                  deferred_runner_build_.push_back(
                      std::make_pair(uuid, std::move(callback)));
                  defer_runner_build_.erase(defer_runner_build_.begin() + i);
                  return;
                }
              }
              ASSERT_TRUE(false);
            }));
  }

  mojo::Remote<mojom::OnDeviceEmbeddingModel> LoadModel(
      const std::string& model_uuid,
      std::unique_ptr<NiceMock<ModelRunnerMock>> runner_mock) {
    base::Uuid uuid = base::Uuid::ParseLowercase(model_uuid);
    base::RunLoop run_loop;
    mojo::Remote<mojom::OnDeviceEmbeddingModel> remote;

    pending_runner_build_.push_back(
        std::make_pair(uuid, std::move(runner_mock)));
    service_impl_.LoadEmbeddingModel(
        uuid, remote.BindNewPipeAndPassReceiver(), mojo::NullRemote(),
        base::BindLambdaForTesting([&](LoadModelResult result) {
          EXPECT_EQ(LoadModelResult::kSuccess, result);
          run_loop.Quit();
        }));
    run_loop.Run();
    return remote;
  }

 protected:
  // A task environment so we can create run loops during test.
  base::test::TaskEnvironment task_environment_;

  // A client to the service for testing.
  mojo::Remote<mojom::OnDeviceEmbeddingModelService> remote_;

  // Metrics library mock for injection.
  NiceMock<MetricsLibraryMock> metrics_;

  // The service implementation under test.
  EmbeddingModelService service_impl_;

  // Put UUID and corresponding ModelRunner that the factory should return here.
  std::vector<std::pair<base::Uuid, std::unique_ptr<ModelRunner>>>
      pending_runner_build_;

  // For any UUID that we should put the callback into deferred_runner_build_
  // and defer the runner build.
  std::vector<base::Uuid> defer_runner_build_;

  // After defer_runner_build_ matechs, put the callback in here.
  std::vector<std::pair<base::Uuid, ModelFactory::BuildRunnerFromUuidCallback>>
      deferred_runner_build_;

  NiceMock<ModelFactoryMock> model_factory_;
};

TEST_F(EmbeddingModelServiceTest, LoadModelAndGetVersion) {
  mojo::Remote<mojom::OnDeviceEmbeddingModel> remote;
  std::unique_ptr<NiceMock<ModelRunnerMock>> owned_runner_mock =
      std::make_unique<NiceMock<ModelRunnerMock>>();
  NiceMock<ModelRunnerMock>* runner_mock = owned_runner_mock.get();
  EXPECT_CALL(*runner_mock, Load)
      .WillOnce(Invoke([&](base::PassKey<ModelHolder> passkey,
                           ModelRunner::LoadCallback callback) {
        std::move(callback).Run(true);
      }));
  remote = LoadModel(kFakeModelUuid1, std::move(owned_runner_mock));
  EXPECT_CALL(*runner_mock, GetModelVersion)
      .WillOnce(Return(kFakeModelVersion1));

  base::RunLoop run_loop;
  remote->Version(
      base::BindLambdaForTesting([&](const std::string& model_version) {
        EXPECT_EQ(kFakeModelVersion1, model_version);
        run_loop.Quit();
      }));
  run_loop.Run();
}

TEST_F(EmbeddingModelServiceTest, OverlappingLoadModelAndGetVersion) {
  mojo::Remote<mojom::OnDeviceEmbeddingModel> remote1, remote2;
  base::Uuid uuid = base::Uuid::ParseLowercase(kFakeModelUuid1);
  defer_runner_build_.push_back(uuid);

  int loaded_count = 0;
  base::RunLoop run_loop;
  service_impl_.LoadEmbeddingModel(
      uuid, remote1.BindNewPipeAndPassReceiver(), mojo::NullRemote(),
      base::BindLambdaForTesting([&](LoadModelResult result) {
        EXPECT_EQ(LoadModelResult::kSuccess, result);
        loaded_count++;
      }));
  run_loop.RunUntilIdle();

  EXPECT_EQ(1, deferred_runner_build_.size());
  EXPECT_EQ(uuid, deferred_runner_build_[0].first);

  // Queue a second LoadModel request and it should work as well.
  service_impl_.LoadEmbeddingModel(
      uuid, remote2.BindNewPipeAndPassReceiver(), mojo::NullRemote(),
      base::BindLambdaForTesting([&](LoadModelResult result) {
        EXPECT_EQ(LoadModelResult::kSuccess, result);
        loaded_count++;
      }));
  run_loop.RunUntilIdle();

  // No duplicate calls to load the same model.
  EXPECT_EQ(1, deferred_runner_build_.size());

  // Fulfill the load request.
  NiceMock<ModelRunnerMock>* runner_mock;
  std::unique_ptr<NiceMock<ModelRunnerMock>> owned_runner_mock =
      std::make_unique<NiceMock<ModelRunnerMock>>();
  runner_mock = owned_runner_mock.get();
  EXPECT_CALL(*runner_mock, Load)
      .WillOnce(Invoke([&](base::PassKey<ModelHolder> passkey,
                           ModelRunner::LoadCallback callback) {
        std::move(callback).Run(true);
      }));

  std::move(deferred_runner_build_[0].second).Run(std::move(owned_runner_mock));
  run_loop.RunUntilIdle();

  // Everything should be loaded now.
  EXPECT_EQ(2, loaded_count);

  // Both remote should work.
  int get_version_count = 0;
  EXPECT_CALL(*runner_mock, GetModelVersion)
      .Times(AtLeast(1))
      .WillRepeatedly(Return(kFakeModelVersion1));
  remote1->Version(
      base::BindLambdaForTesting([&](const std::string& model_version) {
        EXPECT_EQ(kFakeModelVersion1, model_version);
        get_version_count++;
      }));
  remote2->Version(
      base::BindLambdaForTesting([&](const std::string& model_version) {
        EXPECT_EQ(kFakeModelVersion1, model_version);
        get_version_count++;
      }));
  run_loop.RunUntilIdle();
  EXPECT_EQ(2, get_version_count);
}

TEST_F(EmbeddingModelServiceTest, SerializedRun) {
  // Setup Load()
  std::unique_ptr<NiceMock<ModelRunnerMock>> owned_runner_mock =
      std::make_unique<NiceMock<ModelRunnerMock>>();
  NiceMock<ModelRunnerMock>* runner_mock = owned_runner_mock.get();
  bool runner_busy = false;
  EXPECT_CALL(*runner_mock, Load)
      .WillOnce(Invoke([&](base::PassKey<ModelHolder> passkey,
                           ModelRunner::LoadCallback callback) {
        ASSERT_FALSE(runner_busy);
        std::move(callback).Run(true);
      }));

  // Stall the Run() call as well.
  mojom::GenerateEmbeddingRequest mojo_request1;
  mojo_request1.content = kTestContent1;
  mojo_request1.task_type = mojom::TaskType::kClustering;
  mojo_request1.truncate_input = false;
  mojom::GenerateEmbeddingRequest mojo_request2;
  mojo_request2.content = kTestContent2;
  mojo_request2.task_type = mojom::TaskType::kClustering;
  mojo_request2.truncate_input = false;
  std::optional<ModelRunner::RunCallback> run_callback1, run_callback2;
  EXPECT_CALL(*runner_mock, Run)
      .WillRepeatedly(Invoke([&](base::PassKey<ModelHolder> passkey,
                                 mojom::GenerateEmbeddingRequestPtr request,
                                 ModelRunner::RunCallback callback) {
        ASSERT_FALSE(runner_busy);
        runner_busy = true;
        if (*request == mojo_request1) {
          run_callback1 = std::move(callback);
        } else if (*request == mojo_request2) {
          run_callback2 = std::move(callback);
        } else {
          // Invalid call, should not be here.
          FAIL();
        }
      }));

  // Load the model.
  mojo::Remote<mojom::OnDeviceEmbeddingModel> remote;
  remote = LoadModel(kFakeModelUuid1, std::move(owned_runner_mock));
  base::RunLoop run_loop;

  // Run 2 concurrent request to make sure they're properly serialized.
  int run_finished = 0;
  std::vector<float> fake_embedding1(std::begin(kFakeEmbedding1),
                                     std::end(kFakeEmbedding1));
  std::vector<float> fake_embedding2(std::begin(kFakeEmbedding2),
                                     std::end(kFakeEmbedding2));
  remote->GenerateEmbedding(
      mojo_request1.Clone(),
      base::BindLambdaForTesting(
          [&](mojom::OnDeviceEmbeddingModelInferenceError error,
              const std::vector<float>& embeddings) {
            EXPECT_EQ(mojom::OnDeviceEmbeddingModelInferenceError::kSuccess,
                      error);
            EXPECT_EQ(fake_embedding1, embeddings);
            run_finished++;
          }));
  remote->GenerateEmbedding(
      mojo_request2.Clone(),
      base::BindLambdaForTesting(
          [&](mojom::OnDeviceEmbeddingModelInferenceError error,
              const std::vector<float>& embeddings) {
            EXPECT_EQ(mojom::OnDeviceEmbeddingModelInferenceError::kSuccess,
                      error);
            EXPECT_EQ(fake_embedding2, embeddings);
            run_finished++;
          }));
  run_loop.RunUntilIdle();

  // After Load(), Run() should be called.
  ASSERT_TRUE(run_callback1.has_value());
  runner_busy = false;
  std::move(run_callback1.value())
      .Run(mojom::OnDeviceEmbeddingModelInferenceError::kSuccess,
           fake_embedding1);
  run_loop.RunUntilIdle();
  EXPECT_EQ(1, run_finished);

  // Another Run() should be called.
  ASSERT_TRUE(run_callback2.has_value());
  runner_busy = false;
  std::move(run_callback2.value())
      .Run(mojom::OnDeviceEmbeddingModelInferenceError::kSuccess,
           fake_embedding2);
  run_loop.RunUntilIdle();
  EXPECT_EQ(2, run_finished);
}

TEST_F(EmbeddingModelServiceTest, RequestWhileUnloading) {
  base::Uuid uuid = base::Uuid::ParseLowercase(kFakeModelUuid1);
  bool runner_busy = false;
  mojo::Remote<mojom::OnDeviceEmbeddingModel> remote1, remote2;
  std::unique_ptr<NiceMock<ModelRunnerMock>> owned_runner_mock =
      std::make_unique<NiceMock<ModelRunnerMock>>();
  NiceMock<ModelRunnerMock>* runner_mock = owned_runner_mock.get();
  EXPECT_CALL(*runner_mock, Load)
      .WillOnce(Invoke([&](base::PassKey<ModelHolder> passkey,
                           ModelRunner::LoadCallback callback) {
        ASSERT_FALSE(runner_busy);
        std::move(callback).Run(true);
      }));
  remote1 = LoadModel(kFakeModelUuid1, std::move(owned_runner_mock));
  ASSERT_TRUE(testing::Mock::VerifyAndClearExpectations(runner_mock));

  // Stall the Unload() call.
  std::optional<ModelRunner::UnloadCallback> unload_callback;
  EXPECT_CALL(*runner_mock, Unload)
      .WillOnce(Invoke([&](base::PassKey<ModelHolder> passkey,
                           ModelRunner::UnloadCallback callback) {
        ASSERT_FALSE(runner_busy);
        runner_busy = true;
        unload_callback = std::move(callback);
      }));

  // Clear the remote to cause an Unload().
  remote1.reset();
  base::RunLoop run_loop;
  run_loop.RunUntilIdle();

  ASSERT_TRUE(testing::Mock::VerifyAndClearExpectations(runner_mock));

  // Load again but block ModelRunner::Load().
  std::optional<ModelRunner::LoadCallback> load_callback;
  EXPECT_CALL(*runner_mock, Load)
      .WillOnce(Invoke([&](base::PassKey<ModelHolder> passkey,
                           ModelRunner::LoadCallback callback) {
        ASSERT_FALSE(runner_busy);
        runner_busy = true;
        load_callback = std::move(callback);
      }));

  int loaded_count = 0;
  service_impl_.LoadEmbeddingModel(
      uuid, remote2.BindNewPipeAndPassReceiver(), mojo::NullRemote(),
      base::BindLambdaForTesting([&](LoadModelResult result) {
        EXPECT_EQ(LoadModelResult::kSuccess, result);
        loaded_count++;
      }));

  // Try to run a request.
  int run_finished = 0;
  mojom::GenerateEmbeddingRequest mojo_request1;
  mojo_request1.content = kTestContent1;
  mojo_request1.task_type = mojom::TaskType::kClustering;
  mojo_request1.truncate_input = false;
  std::vector<float> fake_embedding1(std::begin(kFakeEmbedding1),
                                     std::end(kFakeEmbedding1));
  std::optional<ModelRunner::RunCallback> run_callback1;
  EXPECT_CALL(*runner_mock, Run)
      .WillRepeatedly(Invoke([&](base::PassKey<ModelHolder> passkey,
                                 mojom::GenerateEmbeddingRequestPtr request,
                                 ModelRunner::RunCallback callback) {
        ASSERT_FALSE(runner_busy);
        runner_busy = true;
        if (*request == mojo_request1) {
          run_callback1 = std::move(callback);
        } else {
          // Invalid call, should not be here.
          FAIL();
        }
      }));
  remote2->GenerateEmbedding(
      mojo_request1.Clone(),
      base::BindLambdaForTesting(
          [&](mojom::OnDeviceEmbeddingModelInferenceError error,
              const std::vector<float>& embeddings) {
            EXPECT_EQ(mojom::OnDeviceEmbeddingModelInferenceError::kSuccess,
                      error);
            EXPECT_EQ(fake_embedding1, embeddings);
            run_finished++;
          }));

  // Unblock Unload() so Load() runs next.
  ASSERT_TRUE(unload_callback.has_value());
  ASSERT_TRUE(runner_busy);
  runner_busy = false;
  std::move(unload_callback.value()).Run();
  run_loop.RunUntilIdle();

  // Unblock Load().
  ASSERT_TRUE(load_callback.has_value());
  ASSERT_TRUE(runner_busy);
  runner_busy = false;
  std::move(load_callback.value()).Run(true);
  run_loop.RunUntilIdle();
  EXPECT_EQ(1, loaded_count);

  // Everything else should run.
  ASSERT_TRUE(run_callback1.has_value());
  ASSERT_TRUE(runner_busy);
  runner_busy = false;
  std::move(run_callback1.value())
      .Run(mojom::OnDeviceEmbeddingModelInferenceError::kSuccess,
           fake_embedding1);
  run_loop.RunUntilIdle();
  EXPECT_EQ(1, run_finished);
}

TEST_F(EmbeddingModelServiceTest, ModelLoadFailed) {
  // Stall Load().
  bool runner_busy = false;
  base::Uuid uuid = base::Uuid::ParseLowercase(kFakeModelUuid1);
  mojo::Remote<mojom::OnDeviceEmbeddingModel> remote;
  std::unique_ptr<NiceMock<ModelRunnerMock>> owned_runner_mock =
      std::make_unique<NiceMock<ModelRunnerMock>>();
  NiceMock<ModelRunnerMock>* runner_mock = owned_runner_mock.get();
  std::optional<ModelRunner::LoadCallback> load_callback;
  EXPECT_CALL(*runner_mock, Load)
      .WillOnce(Invoke([&](base::PassKey<ModelHolder> passkey,
                           ModelRunner::LoadCallback callback) {
        ASSERT_FALSE(runner_busy);
        runner_busy = true;
        load_callback = std::move(callback);
      }));

  // Run LoadEmbeddingModel().
  base::RunLoop run_loop;
  pending_runner_build_.push_back(
      std::make_pair(uuid, std::move(owned_runner_mock)));
  int load_result_count = 0;
  service_impl_.LoadEmbeddingModel(
      uuid, remote.BindNewPipeAndPassReceiver(), mojo::NullRemote(),
      base::BindLambdaForTesting([&](LoadModelResult result) {
        ASSERT_NE(LoadModelResult::kSuccess, result);
        load_result_count++;
      }));
  run_loop.RunUntilIdle();

  // Try GenerateEmbedding(), it should fail.
  mojom::GenerateEmbeddingRequest mojo_request1;
  mojo_request1.content = kTestContent1;
  mojo_request1.task_type = mojom::TaskType::kClustering;
  mojo_request1.truncate_input = false;
  std::vector<float> fake_embedding1(std::begin(kFakeEmbedding1),
                                     std::end(kFakeEmbedding1));
  std::optional<ModelRunner::RunCallback> run_callback1;
  remote->GenerateEmbedding(
      mojo_request1.Clone(),
      base::BindLambdaForTesting(
          [&](mojom::OnDeviceEmbeddingModelInferenceError error,
              const std::vector<float>& embeddings) {
            ASSERT_NE(mojom::OnDeviceEmbeddingModelInferenceError::kSuccess,
                      error);
          }));
  EXPECT_CALL(*runner_mock, Run).Times(0);

  // Make Load() fail.
  ASSERT_TRUE(load_callback.has_value());
  ASSERT_TRUE(runner_busy);
  runner_busy = false;
  std::move(load_callback.value()).Run(false);
  run_loop.RunUntilIdle();
  EXPECT_EQ(1, load_result_count);

  // Run another GenerateEmbedding() and ModelRunner::Run() should still not be
  // called.
  remote->GenerateEmbedding(
      mojo_request1.Clone(),
      base::BindLambdaForTesting(
          [&](mojom::OnDeviceEmbeddingModelInferenceError error,
              const std::vector<float>& embeddings) {
            ASSERT_NE(mojom::OnDeviceEmbeddingModelInferenceError::kSuccess,
                      error);
          }));
  run_loop.RunUntilIdle();
}

TEST_F(EmbeddingModelServiceTest, ModelBuildFailed) {
  base::Uuid uuid = base::Uuid::ParseLowercase(kFakeModelUuid1);
  mojo::Remote<mojom::OnDeviceEmbeddingModel> remote;

  base::RunLoop run_loop;
  int load_result_count = 0;
  pending_runner_build_.push_back(std::make_pair(uuid, nullptr));
  service_impl_.LoadEmbeddingModel(
      uuid, remote.BindNewPipeAndPassReceiver(), mojo::NullRemote(),
      base::BindLambdaForTesting([&](LoadModelResult result) {
        ASSERT_NE(LoadModelResult::kSuccess, result);
        load_result_count++;
      }));
  run_loop.RunUntilIdle();

  EXPECT_EQ(1, load_result_count);
}

}  // namespace
}  // namespace embedding_model
