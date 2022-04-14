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

class FakeServcieProvider : public mojom::ServiceProvider {
 public:
  // Overrides mojom::ServiceProvider.
  void Request(mojom::ProcessIdentityPtr client_identity,
               mojo::ScopedMessagePipeHandle receiver) override {}

  mojo::Receiver<mojom::ServiceProvider> receiver_{this};
};

void ExpectServiceProviderDisconnectWithError(FakeServcieProvider* provider,
                                              mojom::ErrorCode expected_error) {
  base::RunLoop run_loop;
  provider->receiver_.set_disconnect_with_reason_handler(
      base::BindLambdaForTesting(
          [&](uint32_t error, const std::string& message) {
            EXPECT_EQ(error, static_cast<uint32_t>(expected_error));
            run_loop.Quit();
          }));
  run_loop.Run();
}

class FakeServcieObserver : public mojom::ServiceObserver {
 public:
  // Overrides mojom::ServiceObserver.
  void OnServiceEvent(mojom::ServiceEventPtr event) override {
    last_event_ = std::move(event);
    if (callback_) {
      std::move(callback_).Run();
      callback_.Reset();
    }
  }

  mojo::Receiver<mojom::ServiceObserver> receiver_{this};

  base::OnceClosure callback_;

  mojom::ServiceEventPtr last_event_;
};

void ExpectServiceEvent(FakeServcieObserver* observer) {
  base::RunLoop run_loop;
  observer->callback_ = run_loop.QuitClosure();
  run_loop.Run();
}

TEST_F(ServiceManagerTest, RegisterAndUnregister) {
  FakeServcieProvider povider;
  ConnectServiceManagerAs("owner")->Register(
      "FooService", povider.receiver_.BindNewPipeAndPassRemote());

  EXPECT_EQ(Query(ConnectServiceManagerAs("requester"), "FooService"),
            mojom::ErrorOrServiceState::NewState(mojom::ServiceState::New(
                /*is_registered=*/true,
                /*owner=*/mojom::ProcessIdentity::New("owner", 0, 0, 0))));

  // Reset the receiver to unregister from service manager.
  povider.receiver_.reset();
  EXPECT_EQ(Query(ConnectServiceManagerAs("requester"), "FooService"),
            mojom::ErrorOrServiceState::NewState(mojom::ServiceState::New(
                /*is_registered=*/false, /*owner=*/nullptr)));
}

TEST_F(ServiceManagerTest, RegisterError) {
  {
    FakeServcieProvider povider;
    ConnectServiceManagerAs("owner")->Register(
        "NotFoundService", povider.receiver_.BindNewPipeAndPassRemote());
    ExpectServiceProviderDisconnectWithError(
        &povider, mojom::ErrorCode::kServiceNotFound);
  }
  {
    FakeServcieProvider povider;
    ConnectServiceManagerAs("not_owner")
        ->Register("FooService", povider.receiver_.BindNewPipeAndPassRemote());
    ExpectServiceProviderDisconnectWithError(
        &povider, mojom::ErrorCode::kPermissionDenied);
  }
  {
    auto remote = ConnectServiceManagerAs("owner");
    FakeServcieProvider povider1;
    FakeServcieProvider povider2;
    remote->Register("FooService",
                     povider1.receiver_.BindNewPipeAndPassRemote());
    remote->Register("FooService",
                     povider2.receiver_.BindNewPipeAndPassRemote());
    ExpectServiceProviderDisconnectWithError(
        &povider2, mojom::ErrorCode::kServiceHasBeenRegistered);
  }
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

TEST_F(ServiceManagerTest, ServiceObserverGetEvent) {
  FakeServcieObserver observer;
  ConnectServiceManagerAs("requester")
      ->AddServiceObserver(observer.receiver_.BindNewPipeAndPassRemote());

  FakeServcieProvider povider;
  ConnectServiceManagerAs("owner")->Register(
      "FooService", povider.receiver_.BindNewPipeAndPassRemote());
  ExpectServiceEvent(&observer);
  EXPECT_EQ(observer.last_event_,
            mojom::ServiceEvent::New(
                mojom::ServiceEvent::Type::kRegistered, "FooService",
                mojom::ProcessIdentity::New("owner", 0, 0, 0)));

  // Reset the receiver to unregister from service manager.
  povider.receiver_.reset();
  ExpectServiceEvent(&observer);
  EXPECT_EQ(observer.last_event_,
            mojom::ServiceEvent::New(
                mojom::ServiceEvent::Type::kUnRegistered, "FooService",
                mojom::ProcessIdentity::New("owner", 0, 0, 0)));
}

TEST_F(ServiceManagerTest, ServiceObserverNotRequester) {
  FakeServcieObserver observer_not_a_requester;
  ConnectServiceManagerAs("not_requester")
      ->AddServiceObserver(
          observer_not_a_requester.receiver_.BindNewPipeAndPassRemote());

  // Register a service and the observer should not receiver the event.
  FakeServcieProvider provider;
  ConnectServiceManagerAs("owner")->Register(
      "FooService", provider.receiver_.BindNewPipeAndPassRemote());

  // Run until all the async mojo operations are fulfilled.
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(observer_not_a_requester.last_event_.is_null());
}

TEST_F(PermissiveServiceManagerTest, RegisterPermissive) {
  {
    // Test normal case.
    FakeServcieProvider povider;
    ConnectServiceManagerAs("owner")->Register(
        "FooService", povider.receiver_.BindNewPipeAndPassRemote());
    EXPECT_EQ(Query(ConnectServiceManagerAs("requester"), "FooService"),
              mojom::ErrorOrServiceState::NewState(mojom::ServiceState::New(
                  /*is_registered=*/true,
                  /*owner=*/mojom::ProcessIdentity::New("owner", 0, 0, 0))));
  }
  {
    // Test service can be owned by "not_owner".
    FakeServcieProvider povider;
    ConnectServiceManagerAs("not_owner")
        ->Register("FooService", povider.receiver_.BindNewPipeAndPassRemote());
    EXPECT_EQ(
        Query(ConnectServiceManagerAs("requester"), "FooService"),
        mojom::ErrorOrServiceState::NewState(mojom::ServiceState::New(
            /*is_registered=*/true,
            /*owner=*/mojom::ProcessIdentity::New("not_owner", 0, 0, 0))));
  }
  {
    // Test "NotInPolicyService" can be owned.
    FakeServcieProvider povider;
    ConnectServiceManagerAs("owner")->Register(
        "NotInPolicyService", povider.receiver_.BindNewPipeAndPassRemote());
    EXPECT_EQ(Query(ConnectServiceManagerAs("requester"), "NotInPolicyService"),
              mojom::ErrorOrServiceState::NewState(mojom::ServiceState::New(
                  /*is_registered=*/true,
                  /*owner=*/mojom::ProcessIdentity::New("owner", 0, 0, 0))));
  }
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

TEST_F(PermissiveServiceManagerTest, ServiceObserverPermissive) {
  // Test if observer can receive events from services which it is not a
  // requester.
  FakeServcieObserver observer;
  ConnectServiceManagerAs("not_requester")
      ->AddServiceObserver(observer.receiver_.BindNewPipeAndPassRemote());

  FakeServcieProvider povider;
  ConnectServiceManagerAs("owner")->Register(
      "FooService", povider.receiver_.BindNewPipeAndPassRemote());
  ExpectServiceEvent(&observer);

  // Reset the receiver to unregister from service manager.
  povider.receiver_.reset();
  ExpectServiceEvent(&observer);
}

}  // namespace
}  // namespace mojo_service_manager
}  // namespace chromeos
