// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <utility>

#include <base/run_loop.h>
#include <base/test/bind.h>
#include <gtest/gtest.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "mojo_service_manager/daemon/mojo_test_environment.h"
#include "mojo_service_manager/daemon/service_manager.h"
#include "mojo_service_manager/daemon/service_policy_test_util.h"

namespace chromeos {
namespace mojo_service_manager {
namespace {

class ServiceManagerTestBase : public ::testing::Test {
 public:
  explicit ServiceManagerTestBase(Configuration config)
      : service_manager_(std::move(config),
                         CreateServicePolicyMapForTest({
                             {"FooService", {"owner", {"requester"}}},
                         })) {}

 protected:
  mojo::Remote<mojom::ServiceManager> ConnectServiceManagerAs(
      const std::string& security_context) {
    mojo::Remote<mojom::ServiceManager> remote;
    service_manager_.AddReceiver(
        mojom::ProcessIdentity::New(security_context, 0, 0, 0),
        remote.BindNewPipeAndPassReceiver());
    return remote;
  }

  MojoTaskEnvironment env_;

  ServiceManager service_manager_;
};

class ServiceManagerTest : public ServiceManagerTestBase {
 public:
  ServiceManagerTest() : ServiceManagerTestBase(Configuration{}) {}
};

class PermissiveServiceManagerTest : public ServiceManagerTestBase {
 public:
  PermissiveServiceManagerTest()
      : ServiceManagerTestBase(Configuration{.is_permissive = true}) {}
};

mojom::ErrorOrServiceStatePtr Query(
    const mojo::Remote<mojom::ServiceManager>& service_manager,
    const std::string& service_name) {
  mojom::ErrorOrServiceStatePtr result;
  base::RunLoop run_loop;
  service_manager->Query(service_name,
                         base::BindLambdaForTesting(
                             [&](mojom::ErrorOrServiceStatePtr result_inner) {
                               result = std::move(result_inner);
                               run_loop.Quit();
                             }));
  run_loop.Run();
  return result;
}

TEST_F(ServiceManagerTest, Query) {
  EXPECT_EQ(Query(ConnectServiceManagerAs("requester"), "FooService"),
            mojom::ErrorOrServiceState::NewState(mojom::ServiceState::New(
                /*is_registered=*/false, /*owner=*/nullptr)));
}

TEST_F(ServiceManagerTest, QueryError) {
  // Test service not found.
  EXPECT_EQ(Query(ConnectServiceManagerAs("requester"), "NotFoundService")
                ->get_error()
                ->code,
            mojom::ErrorCode::kServiceNotFound);

  // Test permission denied.
  EXPECT_EQ(Query(ConnectServiceManagerAs("not_requester"), "FooService")
                ->get_error()
                ->code,
            mojom::ErrorCode::kPermissionDenied);
}

TEST_F(PermissiveServiceManagerTest, QueryPermissive) {
  // Test service not found.
  EXPECT_EQ(Query(ConnectServiceManagerAs("requester"), "NotFoundService")
                ->get_error()
                ->code,
            mojom::ErrorCode::kServiceNotFound);

  // Test permission denied is not raised for not_requester.
  EXPECT_FALSE(Query(ConnectServiceManagerAs("not_requester"), "FooService")
                   ->is_error());

  // Test normal requester.
  EXPECT_FALSE(
      Query(ConnectServiceManagerAs("requester"), "FooService")->is_error());
}

}  // namespace
}  // namespace mojo_service_manager
}  // namespace chromeos
