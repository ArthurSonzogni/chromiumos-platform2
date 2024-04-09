// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <utility>

#include <base/run_loop.h>
#include <base/test/bind.h>
#include <gtest/gtest.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "mojo_service_manager/daemon/service_manager.h"
#include "mojo_service_manager/daemon/service_policy_test_util.h"
#include "mojo_service_manager/testing/mojo_test_environment.h"
#include "mojo_service_manager/testing/test.mojom.h"

namespace chromeos::mojo_service_manager {
namespace {

const uint32_t kOwnerUid = 1;
const uint32_t kNotOwnerUid = 2;
const uint32_t kRequesterUid = 3;
const uint32_t kNotRequesterUid = 4;

class ServiceManagerTestBase : public ::testing::Test {
 public:
  explicit ServiceManagerTestBase(Configuration config)
      : service_manager_(
            std::move(config),
            CreateServicePolicyMapForTest(
                {
                    {"FooUidService", {kOwnerUid, {kRequesterUid}}},
                },
                {
                    {"FooService", {"owner", {"requester"}}},
                    {"FooUidService", {"", {"requester"}}},
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

  mojo::Remote<mojom::ServiceManager> ConnectServiceManagerAs(uint32_t uid) {
    mojo::Remote<mojom::ServiceManager> remote;
    // TODO(b/333323875): Change security_context to empty string after remove
    // the SELinux logic. Before that, security_context can't be empty.
    service_manager_.AddReceiver(
        mojom::ProcessIdentity::New("any_security_context", 0, uid, 0),
        remote.BindNewPipeAndPassReceiver());
    return remote;
  }

  MojoTaskEnvironment env_{base::test::TaskEnvironment::TimeSource::MOCK_TIME};

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

class FakeServcieProvider : public mojom::ServiceProvider, public mojom::Foo {
 public:
  // Overrides mojom::ServiceProvider.
  void Request(mojom::ProcessIdentityPtr client_identity,
               mojo::ScopedMessagePipeHandle receiver) override {
    CHECK(receiver.is_valid()) << "Receiver pipe is not valid.";
    last_client_identity_ = std::move(client_identity);
    foo_receiver_set_.Add(
        this, mojo::PendingReceiver<mojom::Foo>(std::move(receiver)));
  }

  // Overrides mojom::Foo.
  void Ping(PingCallback callback) override { std::move(callback).Run(); }

  mojo::Receiver<mojom::ServiceProvider> receiver_{this};

  mojo::ReceiverSet<mojom::Foo> foo_receiver_set_;

  mojom::ProcessIdentityPtr last_client_identity_;
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

void ExpectFooServiceConnected(mojo::Remote<mojom::Foo>* service) {
  service->set_disconnect_with_reason_handler(base::BindLambdaForTesting(
      [&](uint32_t error, const std::string& message) {
        CHECK(false) << "Reset with error: " << error
                     << ",message: " << message;
      }));
  service->FlushForTesting();
  CHECK(service->is_connected()) << "Foo service is disconnected.";
  service->set_disconnect_with_reason_handler(base::DoNothing());
  base::RunLoop run_loop;
  service->get()->Ping(run_loop.QuitClosure());
  run_loop.Run();
}

void ExpectFooServiceDisconnectWithError(mojo::Remote<mojom::Foo>* service,
                                         mojom::ErrorCode expected_error) {
  base::RunLoop run_loop;
  service->set_disconnect_with_reason_handler(base::BindLambdaForTesting(
      [&](uint32_t error, const std::string& message) {
        EXPECT_EQ(error, static_cast<uint32_t>(expected_error));
        run_loop.Quit();
      }));
  run_loop.Run();
}

TEST_F(ServiceManagerTest, RegisterAndUnregisterUid) {
  FakeServcieProvider povider;
  ConnectServiceManagerAs(kOwnerUid)->Register(
      "FooUidService", povider.receiver_.BindNewPipeAndPassRemote());

  EXPECT_EQ(Query(ConnectServiceManagerAs(kRequesterUid), "FooUidService"),
            mojom::ErrorOrServiceState::NewState(
                mojom::ServiceState::NewRegisteredState(
                    mojom::RegisteredServiceState::New(
                        /*owner=*/mojom::ProcessIdentity::New(
                            "any_security_context", 0, kOwnerUid, 0)))));

  // Reset the receiver to unregister from service manager.
  povider.receiver_.reset();
  EXPECT_EQ(Query(ConnectServiceManagerAs(kRequesterUid), "FooUidService"),
            mojom::ErrorOrServiceState::NewState(
                mojom::ServiceState::NewUnregisteredState(
                    mojom::UnregisteredServiceState::New())));
}

TEST_F(ServiceManagerTest, RegisterAndUnregisterSELinux) {
  FakeServcieProvider povider;
  ConnectServiceManagerAs("owner")->Register(
      "FooService", povider.receiver_.BindNewPipeAndPassRemote());

  EXPECT_EQ(
      Query(ConnectServiceManagerAs("requester"), "FooService"),
      mojom::ErrorOrServiceState::NewState(
          mojom::ServiceState::NewRegisteredState(
              mojom::RegisteredServiceState::New(
                  /*owner=*/mojom::ProcessIdentity::New("owner", 0, 0, 0)))));

  // Reset the receiver to unregister from service manager.
  povider.receiver_.reset();
  EXPECT_EQ(Query(ConnectServiceManagerAs("requester"), "FooService"),
            mojom::ErrorOrServiceState::NewState(
                mojom::ServiceState::NewUnregisteredState(
                    mojom::UnregisteredServiceState::New())));
}

TEST_F(ServiceManagerTest, RegisterErrorUid) {
  {
    FakeServcieProvider povider;
    ConnectServiceManagerAs(kOwnerUid)->Register(
        "NotFoundService", povider.receiver_.BindNewPipeAndPassRemote());
    ExpectServiceProviderDisconnectWithError(
        &povider, mojom::ErrorCode::kServiceNotFound);
  }
  {
    FakeServcieProvider povider;
    ConnectServiceManagerAs(kNotOwnerUid)
        ->Register("FooUidService",
                   povider.receiver_.BindNewPipeAndPassRemote());
    ExpectServiceProviderDisconnectWithError(
        &povider, mojom::ErrorCode::kPermissionDenied);
  }
  {
    auto remote = ConnectServiceManagerAs(kOwnerUid);
    FakeServcieProvider povider1;
    FakeServcieProvider povider2;
    remote->Register("FooUidService",
                     povider1.receiver_.BindNewPipeAndPassRemote());
    remote->Register("FooUidService",
                     povider2.receiver_.BindNewPipeAndPassRemote());
    ExpectServiceProviderDisconnectWithError(
        &povider2, mojom::ErrorCode::kServiceAlreadyRegistered);
  }
}

TEST_F(ServiceManagerTest, RegisterErrorSELinux) {
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
        &povider2, mojom::ErrorCode::kServiceAlreadyRegistered);
  }
}

TEST_F(ServiceManagerTest, RequestUid) {
  FakeServcieProvider provider;
  ConnectServiceManagerAs(kOwnerUid)->Register(
      "FooUidService", provider.receiver_.BindNewPipeAndPassRemote());

  mojo::Remote<mojom::Foo> foo;
  ConnectServiceManagerAs(kRequesterUid)
      ->Request("FooUidService", std::nullopt,
                foo.BindNewPipeAndPassReceiver().PassPipe());
  ExpectFooServiceConnected(&foo);
  EXPECT_EQ(provider.last_client_identity_->uid, kRequesterUid);

  // Also check legacy security context still work.
  foo.reset();
  ConnectServiceManagerAs("requester")
      ->Request("FooUidService", std::nullopt,
                foo.BindNewPipeAndPassReceiver().PassPipe());
  ExpectFooServiceConnected(&foo);
  EXPECT_EQ(provider.last_client_identity_->security_context, "requester");
}

TEST_F(ServiceManagerTest, RequestSELinux) {
  FakeServcieProvider provider;
  ConnectServiceManagerAs("owner")->Register(
      "FooService", provider.receiver_.BindNewPipeAndPassRemote());

  mojo::Remote<mojom::Foo> foo;
  ConnectServiceManagerAs("requester")
      ->Request("FooService", std::nullopt,
                foo.BindNewPipeAndPassReceiver().PassPipe());
  ExpectFooServiceConnected(&foo);
  EXPECT_EQ(provider.last_client_identity_->security_context, "requester");
}

TEST_F(ServiceManagerTest, RequestBeforeRegister) {
  // Request without a timeout (set timeout to std::nullopt) so it will wait
  // until the service is registered.
  mojo::Remote<mojom::Foo> foo;
  ConnectServiceManagerAs(kRequesterUid)
      ->Request("FooUidService", std::nullopt,
                foo.BindNewPipeAndPassReceiver().PassPipe());

  FakeServcieProvider provider;
  ConnectServiceManagerAs(kOwnerUid)->Register(
      "FooUidService", provider.receiver_.BindNewPipeAndPassRemote());
  ExpectFooServiceConnected(&foo);
  EXPECT_EQ(provider.last_client_identity_->uid, kRequesterUid);
}

TEST_F(ServiceManagerTest, RequestErrorUid) {
  {
    // Test service not found.
    mojo::Remote<mojom::Foo> foo;
    ConnectServiceManagerAs(kRequesterUid)
        ->Request("NotFoundService", std::nullopt,
                  foo.BindNewPipeAndPassReceiver().PassPipe());
    ExpectFooServiceDisconnectWithError(&foo,
                                        mojom::ErrorCode::kServiceNotFound);
  }
  {
    // Test permission denied.
    mojo::Remote<mojom::Foo> foo;
    ConnectServiceManagerAs(kNotRequesterUid)
        ->Request("FooUidService", std::nullopt,
                  foo.BindNewPipeAndPassReceiver().PassPipe());
    ExpectFooServiceDisconnectWithError(&foo,
                                        mojom::ErrorCode::kPermissionDenied);
  }
}

TEST_F(ServiceManagerTest, RequestErrorSELinux) {
  {
    // Test service not found.
    mojo::Remote<mojom::Foo> foo;
    ConnectServiceManagerAs("requester")
        ->Request("NotFoundService", std::nullopt,
                  foo.BindNewPipeAndPassReceiver().PassPipe());
    ExpectFooServiceDisconnectWithError(&foo,
                                        mojom::ErrorCode::kServiceNotFound);
  }
  {
    // Test permission denied.
    mojo::Remote<mojom::Foo> foo;
    ConnectServiceManagerAs("not_a_requester")
        ->Request("FooService", std::nullopt,
                  foo.BindNewPipeAndPassReceiver().PassPipe());
    ExpectFooServiceDisconnectWithError(&foo,
                                        mojom::ErrorCode::kPermissionDenied);
  }
}

TEST_F(ServiceManagerTest, RequestTimeout) {
  auto remote = ConnectServiceManagerAs(kRequesterUid);
  mojo::Remote<mojom::Foo> foo1;
  remote->Request("FooUidService", base::Seconds(0),
                  foo1.BindNewPipeAndPassReceiver().PassPipe());
  mojo::Remote<mojom::Foo> foo2;
  remote->Request("FooUidService", base::Seconds(5),
                  foo2.BindNewPipeAndPassReceiver().PassPipe());
  mojo::Remote<mojom::Foo> foo3;
  remote->Request("FooUidService", base::Seconds(10),
                  foo3.BindNewPipeAndPassReceiver().PassPipe());
  // No timeout.
  mojo::Remote<mojom::Foo> foo4;
  remote->Request("FooUidService", std::nullopt,
                  foo4.BindNewPipeAndPassReceiver().PassPipe());

  // Wait for the first two timeout.
  ExpectFooServiceDisconnectWithError(&foo1, mojom::ErrorCode::kTimeout);
  ExpectFooServiceDisconnectWithError(&foo2, mojom::ErrorCode::kTimeout);

  // Now it is at 5 seconds. Register the service so the rest of them can
  // connected successfully.
  FakeServcieProvider provider;
  ConnectServiceManagerAs(kOwnerUid)->Register(
      "FooUidService", provider.receiver_.BindNewPipeAndPassRemote());
  ExpectFooServiceConnected(&foo3);
  ExpectFooServiceConnected(&foo4);
}

TEST_F(ServiceManagerTest, QueryUid) {
  EXPECT_EQ(Query(ConnectServiceManagerAs(kRequesterUid), "FooUidService"),
            mojom::ErrorOrServiceState::NewState(
                mojom::ServiceState::NewUnregisteredState(
                    mojom::UnregisteredServiceState::New())));
  // Also check legacy security context still work.
  EXPECT_EQ(Query(ConnectServiceManagerAs("requester"), "FooUidService"),
            mojom::ErrorOrServiceState::NewState(
                mojom::ServiceState::NewUnregisteredState(
                    mojom::UnregisteredServiceState::New())));
}

TEST_F(ServiceManagerTest, QuerySELinux) {
  EXPECT_EQ(Query(ConnectServiceManagerAs("requester"), "FooService"),
            mojom::ErrorOrServiceState::NewState(
                mojom::ServiceState::NewUnregisteredState(
                    mojom::UnregisteredServiceState::New())));
}

TEST_F(ServiceManagerTest, QueryErrorUid) {
  // Test service not found.
  EXPECT_EQ(Query(ConnectServiceManagerAs(kRequesterUid), "NotFoundService")
                ->get_error()
                ->code,
            mojom::ErrorCode::kServiceNotFound);

  // Test permission denied.
  EXPECT_EQ(Query(ConnectServiceManagerAs(kNotRequesterUid), "FooUidService")
                ->get_error()
                ->code,
            mojom::ErrorCode::kPermissionDenied);
}

TEST_F(ServiceManagerTest, QueryErrorSELinux) {
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
    ConnectServiceManagerAs(kOwnerUid)->Register(
        "FooUidService", povider.receiver_.BindNewPipeAndPassRemote());
    EXPECT_EQ(Query(ConnectServiceManagerAs("requester"), "FooUidService"),
              mojom::ErrorOrServiceState::NewState(
                  mojom::ServiceState::NewRegisteredState(
                      mojom::RegisteredServiceState::New(
                          /*owner=*/mojom::ProcessIdentity::New(
                              "any_security_context", 0, kOwnerUid, 0)))));
  }
  {
    // Test service can be owned by kNotOwnerUid.
    FakeServcieProvider povider;
    ConnectServiceManagerAs(kNotOwnerUid)
        ->Register("FooUidService",
                   povider.receiver_.BindNewPipeAndPassRemote());
    EXPECT_EQ(Query(ConnectServiceManagerAs("requester"), "FooUidService"),
              mojom::ErrorOrServiceState::NewState(
                  mojom::ServiceState::NewRegisteredState(
                      mojom::RegisteredServiceState::New(
                          /*owner=*/mojom::ProcessIdentity::New(
                              "any_security_context", 0, kNotOwnerUid, 0)))));
  }
  {
    // Test "NotInPolicyService" can be owned.
    FakeServcieProvider povider;
    ConnectServiceManagerAs(kOwnerUid)->Register(
        "NotInPolicyService", povider.receiver_.BindNewPipeAndPassRemote());
    EXPECT_EQ(Query(ConnectServiceManagerAs("requester"), "NotInPolicyService"),
              mojom::ErrorOrServiceState::NewState(
                  mojom::ServiceState::NewRegisteredState(
                      mojom::RegisteredServiceState::New(
                          /*owner=*/mojom::ProcessIdentity::New(
                              "any_security_context", 0, kOwnerUid, 0)))));
  }
}

TEST_F(PermissiveServiceManagerTest, RequestPermissive) {
  {
    // Test normal case.
    FakeServcieProvider provider;
    ConnectServiceManagerAs(kOwnerUid)->Register(
        "FooUidService", provider.receiver_.BindNewPipeAndPassRemote());

    mojo::Remote<mojom::Foo> foo;
    ConnectServiceManagerAs(kRequesterUid)
        ->Request("FooUidService", std::nullopt,
                  foo.BindNewPipeAndPassReceiver().PassPipe());
    ExpectFooServiceConnected(&foo);
    EXPECT_EQ(provider.last_client_identity_->uid, kRequesterUid);
  }
  {
    // Test request by not_requester.
    FakeServcieProvider provider;
    ConnectServiceManagerAs(kOwnerUid)->Register(
        "FooUidService", provider.receiver_.BindNewPipeAndPassRemote());

    mojo::Remote<mojom::Foo> foo;
    ConnectServiceManagerAs(kNotRequesterUid)
        ->Request("FooUidService", std::nullopt,
                  foo.BindNewPipeAndPassReceiver().PassPipe());
    ExpectFooServiceConnected(&foo);
    EXPECT_EQ(provider.last_client_identity_->uid, kNotRequesterUid);
  }
  {
    // Test request NotInPolicyService.
    FakeServcieProvider provider;
    ConnectServiceManagerAs(kOwnerUid)->Register(
        "NotInPolicyService", provider.receiver_.BindNewPipeAndPassRemote());

    mojo::Remote<mojom::Foo> foo;
    ConnectServiceManagerAs(kRequesterUid)
        ->Request("NotInPolicyService", std::nullopt,
                  foo.BindNewPipeAndPassReceiver().PassPipe());
    ExpectFooServiceConnected(&foo);
    EXPECT_EQ(provider.last_client_identity_->uid, kRequesterUid);
  }
}

TEST_F(PermissiveServiceManagerTest, RequestTimeoutPermissive) {
  {
    // Test normal case.
    mojo::Remote<mojom::Foo> foo;
    ConnectServiceManagerAs(kRequesterUid)
        ->Request("FooUidService", base::Seconds(5),
                  foo.BindNewPipeAndPassReceiver().PassPipe());
    ExpectFooServiceDisconnectWithError(&foo, mojom::ErrorCode::kTimeout);
  }
  {
    // Test request by not_requester.
    mojo::Remote<mojom::Foo> foo;
    ConnectServiceManagerAs(kNotRequesterUid)
        ->Request("FooUidService", base::Seconds(5),
                  foo.BindNewPipeAndPassReceiver().PassPipe());
    ExpectFooServiceDisconnectWithError(&foo, mojom::ErrorCode::kTimeout);
  }
  {
    // Test request NotInPolicyService.
    mojo::Remote<mojom::Foo> foo;
    ConnectServiceManagerAs(kRequesterUid)
        ->Request("NotInPolicyService", base::Seconds(5),
                  foo.BindNewPipeAndPassReceiver().PassPipe());
    ExpectFooServiceDisconnectWithError(&foo, mojom::ErrorCode::kTimeout);
  }
}

TEST_F(PermissiveServiceManagerTest, QueryPermissive) {
  // Test service not found.
  EXPECT_EQ(Query(ConnectServiceManagerAs(kRequesterUid), "NotFoundService")
                ->get_error()
                ->code,
            mojom::ErrorCode::kServiceNotFound);

  // Test permission denied is not raised for not_requester.
  EXPECT_FALSE(Query(ConnectServiceManagerAs(kNotRequesterUid), "FooUidService")
                   ->is_error());

  // Test normal requester.
  EXPECT_FALSE(Query(ConnectServiceManagerAs(kRequesterUid), "FooUidService")
                   ->is_error());
}

TEST_F(PermissiveServiceManagerTest, ServiceObserverPermissive) {
  // Test if observer can receive events from services which it is not a
  // requester.
  FakeServcieObserver observer;
  ConnectServiceManagerAs("not_requester")
      ->AddServiceObserver(observer.receiver_.BindNewPipeAndPassRemote());

  FakeServcieProvider povider;
  ConnectServiceManagerAs(kOwnerUid)->Register(
      "FooUidService", povider.receiver_.BindNewPipeAndPassRemote());
  ExpectServiceEvent(&observer);

  // Reset the receiver to unregister from service manager.
  povider.receiver_.reset();
  ExpectServiceEvent(&observer);
}

}  // namespace
}  // namespace chromeos::mojo_service_manager
