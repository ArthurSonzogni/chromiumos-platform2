// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/embedding_model/embedding_model_service.h"

#include <base/test/task_environment.h>
#include <metrics/metrics_library_mock.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/public/cpp/bindings/pending_remote.h>
#include <mojo/public/cpp/bindings/remote.h>
#include <testing/gmock/include/gmock/gmock.h>
#include <testing/gtest/include/gtest/gtest.h>

#include "odml/mojom/embedding_model.mojom.h"

namespace embedding_model {
namespace {

using ::testing::NiceMock;

class EmbeddingModelServiceTest : public testing::Test {
 public:
  EmbeddingModelServiceTest() : service_impl_(raw_ref(metrics_)) {}

  void SetUp() override {
    mojo::core::Init();
    service_impl_.AddReceiver(remote_.BindNewPipeAndPassReceiver());
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
};

TEST_F(EmbeddingModelServiceTest, LoadModel) {}

}  // namespace
}  // namespace embedding_model
