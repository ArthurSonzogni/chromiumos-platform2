// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>

#include <base/run_loop.h>
#include <google/protobuf/util/message_differencer.h>
#include <gtest/gtest.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "federated/federated_metadata.h"
#include "federated/federated_service_impl.h"
#include "federated/mock_storage_manager.h"
#include "federated/mojom/federated_service.mojom.h"
#include "federated/protos/example.pb.h"
#include "federated/test_utils.h"
#include "federated/utils.h"

namespace federated {
namespace {

using chromeos::federated::mojom::FederatedService;
using testing::_;
using testing::Return;
using testing::StrictMock;

TEST(FederatedServiceImplTest, TestReportExample) {
  const std::unique_ptr<MockStorageManager> storage_manager(
      new StrictMock<MockStorageManager>());

  const std::string registered_client_name = *GetClientNames().begin();
  EXPECT_CALL(*storage_manager, OnExampleReceived(_, _))
      .Times(1)
      .WillOnce(Return(true));

  mojo::Remote<FederatedService> federated_service;
  const FederatedServiceImpl federated_service_impl(
      federated_service.BindNewPipeAndPassReceiver().PassPipe(),
      base::OnceClosure(), storage_manager.get());

  // Reports examples with a registered client_name then an unknown client_name,
  // will trigger storage_manager->OnExampleReceived only once.
  federated_service->ReportExample(registered_client_name, CreateExamplePtr());
  federated_service->ReportExample("unknown_client", CreateExamplePtr());

  base::RunLoop().RunUntilIdle();
}

TEST(FederatedServiceImplTest, TestClone) {
  const std::unique_ptr<MockStorageManager> storage_manager(
      new StrictMock<MockStorageManager>());

  const std::string registered_client_name = *GetClientNames().begin();
  EXPECT_CALL(*storage_manager, OnExampleReceived(registered_client_name, _))
      .Times(1)
      .WillOnce(Return(true));

  mojo::Remote<FederatedService> federated_service;
  const FederatedServiceImpl federated_service_impl(
      federated_service.BindNewPipeAndPassReceiver().PassPipe(),
      base::OnceClosure(), storage_manager.get());

  // Call Clone to bind another FederatedService.
  mojo::Remote<FederatedService> federated_service_2;
  federated_service->Clone(federated_service_2.BindNewPipeAndPassReceiver());

  federated_service_2->ReportExample(registered_client_name,
                                     CreateExamplePtr());

  base::RunLoop().RunUntilIdle();
}

}  // namespace
}  // namespace federated
