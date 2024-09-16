// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/embedding_model/embedding_model_service.h"

#include <memory>
#include <string>
#include <vector>

#include <base/test/task_environment.h>
#include <metrics/metrics_library_mock.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/public/cpp/bindings/pending_remote.h>
#include <mojo/public/cpp/bindings/remote.h>
#include <testing/gmock/include/gmock/gmock.h>
#include <testing/gtest/include/gtest/gtest.h>

#include "odml/embedding_model/model_factory_mock.h"
#include "odml/mojom/embedding_model.mojom.h"

namespace embedding_model {
namespace {

using ::testing::Invoke;
using ::testing::NiceMock;

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

TEST_F(EmbeddingModelServiceTest, LoadModel) {}

}  // namespace
}  // namespace embedding_model
