// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "secagentd/plugins.h"

#include <memory>

#include "base/functional/callback_forward.h"
#include "base/memory/scoped_refptr.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "cryptohome/proto_bindings/auth_factor.pb.h"
#include "dbus/mock_bus.h"
#include "dbus/mock_object_proxy.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "missive/util/status.h"
#include "secagentd/device_user.h"
#include "secagentd/proto/security_xdr_events.pb.h"
#include "secagentd/test/mock_batch_sender.h"
#include "secagentd/test/mock_device_user.h"
#include "secagentd/test/mock_message_sender.h"
#include "secagentd/test/mock_policies_features_broker.h"
#include "secagentd/test/mock_process_cache.h"
#include "secagentd/test/test_utils.h"
#include "user_data_auth-client-test/user_data_auth/dbus-proxy-mocks.h"

namespace secagentd::testing {

namespace pb = cros_xdr::reporting;

using ::testing::_;
using ::testing::Eq;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::WithArg;
using ::testing::WithArgs;

using BatchSenderType =
    ::testing::StrictMock<MockBatchSender<std::monostate,
                                          pb::XdrUserEvent,
                                          pb::UserEventAtomicVariant>>;

constexpr char kDeviceUser[] = "deviceUser@email.com";

class AuthenticationPluginTestFixture : public ::testing::Test {
 protected:
  void SetUp() override {
    message_sender_ = base::MakeRefCounted<MockMessageSender>();
    device_user_ = base::MakeRefCounted<MockDeviceUser>();
    // Unused in authentication plugin.
    scoped_refptr<MockProcessCache> process_cache =
        base::MakeRefCounted<MockProcessCache>();
    policies_features_broker_ =
        base::MakeRefCounted<MockPoliciesFeaturesBroker>();
    plugin_factory_ = std::make_unique<PluginFactory>();

    plugin_ = plugin_factory_->Create(
        Types::Plugin::kAuthenticate, message_sender_, process_cache,
        policies_features_broker_, device_user_, 1);
    EXPECT_NE(nullptr, plugin_);

    auto batch_sender = std::make_unique<BatchSenderType>();
    batch_sender_ = batch_sender.get();
    auth_plugin_ = static_cast<AuthenticationPlugin*>(plugin_.get());
    auth_plugin_->SetBatchSenderForTesting(std::move(batch_sender));
  }

  void SaveRegisterLockingCbs() {
    EXPECT_CALL(*device_user_, RegisterScreenLockedHandler)
        .WillOnce(WithArg<0>(
            Invoke([this](const base::RepeatingCallback<void()>& cb) {
              locked_cb_ = cb;
            })));
    EXPECT_CALL(*device_user_, RegisterScreenUnlockedHandler)
        .WillOnce(WithArg<0>(
            Invoke([this](const base::RepeatingCallback<void()>& cb) {
              unlocked_cb_ = cb;
            })));
  }

  void SaveSessionStateChangeCb() {
    EXPECT_CALL(*device_user_, RegisterSessionChangeListener)
        .WillOnce(WithArg<0>(Invoke([this](const base::RepeatingCallback<void(
                                               const std::string& state)>& cb) {
          state_changed_cb_ = cb;
        })));
  }

  void SetupObjectProxies() {
    EXPECT_CALL(*batch_sender_, Start());

    cryptohome_proxy_ =
        std::make_unique<org::chromium::UserDataAuthInterfaceProxyMock>();
    cryptohome_object_proxy_ = new dbus::MockObjectProxy(
        bus_.get(), user_data_auth::kUserDataAuthServiceName,
        dbus::ObjectPath(user_data_auth::kUserDataAuthServicePath));

    EXPECT_CALL(*cryptohome_proxy_, GetObjectProxy)
        .WillRepeatedly(Return(cryptohome_object_proxy_.get()));
    EXPECT_CALL(*cryptohome_object_proxy_, DoWaitForServiceToBeAvailable(_))
        .WillRepeatedly(
            WithArg<0>(Invoke([](base::OnceCallback<void(bool)>* cb) mutable {
              std::move(*cb).Run(true);
            })));
    EXPECT_CALL(*cryptohome_proxy_,
                DoRegisterAuthenticateAuthFactorCompletedSignalHandler)
        .WillOnce(WithArg<0>(Invoke(
            [this](base::RepeatingCallback<void(
                       const user_data_auth::AuthenticateAuthFactorCompleted&)>
                       cb) { auth_factor_cb_ = cb; })));
    auth_plugin_->cryptohome_proxy_ = std::move(cryptohome_proxy_);
  }

  AuthFactorType GetAuthFactor() { return auth_plugin_->auth_factor_type_; }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  scoped_refptr<MockMessageSender> message_sender_;
  scoped_refptr<MockPoliciesFeaturesBroker> policies_features_broker_;
  scoped_refptr<MockDeviceUser> device_user_;
  scoped_refptr<dbus::MockBus> bus_;
  scoped_refptr<dbus::MockObjectProxy> cryptohome_object_proxy_;
  std::unique_ptr<org::chromium::UserDataAuthInterfaceProxyMock>
      cryptohome_proxy_;
  std::unique_ptr<PluginFactory> plugin_factory_;
  std::unique_ptr<PluginInterface> plugin_;
  AuthenticationPlugin* auth_plugin_;
  BatchSenderType* batch_sender_;
  base::RepeatingCallback<void(
      const user_data_auth::AuthenticateAuthFactorCompleted&)>
      auth_factor_cb_;
  base::RepeatingCallback<void()> locked_cb_;
  base::RepeatingCallback<void()> unlocked_cb_;
  base::RepeatingCallback<void(const std::string& state)> state_changed_cb_;
};

TEST_F(AuthenticationPluginTestFixture, TestGetName) {
  ASSERT_EQ("Authentication", plugin_->GetName());
}

TEST_F(AuthenticationPluginTestFixture, TestScreenLockToUnlock) {
  SetupObjectProxies();
  SaveRegisterLockingCbs();
  SaveSessionStateChangeCb();
  EXPECT_CALL(*device_user_, GetDeviceUserAsync)
      .Times(2)
      .WillRepeatedly(WithArg<0>(Invoke(
          [](base::OnceCallback<void(const std::string& device_user)> cb) {
            std::move(cb).Run(kDeviceUser);
          })));

  user_data_auth::AuthenticateAuthFactorCompleted completed;
  completed.set_auth_factor_type(user_data_auth::AUTH_FACTOR_TYPE_PIN);

  // batch_sender_ will be called twice. Once for lock, once for unlock.
  auto lock_event = std::make_unique<pb::UserEventAtomicVariant>();
  auto unlock_event = std::make_unique<pb::UserEventAtomicVariant>();
  EXPECT_CALL(*batch_sender_, Enqueue(_))
      .Times(2)
      .WillOnce(WithArg<0>(
          Invoke([&lock_event](
                     std::unique_ptr<google::protobuf::MessageLite> message) {
            lock_event->ParseFromString(
                std::move(message->SerializeAsString()));
          })))
      .WillOnce(WithArg<0>(
          Invoke([&unlock_event](
                     std::unique_ptr<google::protobuf::MessageLite> message) {
            unlock_event->ParseFromString(
                std::move(message->SerializeAsString()));
          })));

  EXPECT_OK(plugin_->Activate());
  auth_factor_cb_.Run(completed);
  locked_cb_.Run();

  auto expected_event = std::make_unique<pb::UserEventAtomicVariant>();
  expected_event->mutable_common();
  expected_event->mutable_lock();
  expected_event->mutable_common()->set_device_user(kDeviceUser);
  expected_event->mutable_common()->set_create_timestamp_us(
      lock_event->common().create_timestamp_us());
  EXPECT_THAT(*expected_event, EqualsProto(*lock_event));

  // Unlock.
  unlocked_cb_.Run();
  expected_event->mutable_unlock()->mutable_authentication()->add_auth_factor(
      AuthFactorType::Authentication_AuthenticationType_AUTH_PIN);
  expected_event->mutable_common()->set_create_timestamp_us(
      unlock_event->common().create_timestamp_us());
  EXPECT_THAT(*expected_event, EqualsProto(*unlock_event));
  EXPECT_EQ(AuthFactorType::Authentication_AuthenticationType_AUTH_TYPE_UNKNOWN,
            GetAuthFactor());
}

TEST_F(AuthenticationPluginTestFixture, TestScreenLoginToLogout) {
  SetupObjectProxies();
  SaveRegisterLockingCbs();
  SaveSessionStateChangeCb();
  EXPECT_CALL(*device_user_, GetDeviceUserAsync)
      .Times(2)
      .WillRepeatedly(WithArg<0>(Invoke(
          [](base::OnceCallback<void(const std::string& device_user)> cb) {
            std::move(cb).Run(kDeviceUser);
          })));

  user_data_auth::AuthenticateAuthFactorCompleted completed;
  completed.set_auth_factor_type(user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);

  // batch_sender_ will be called twice. Once for login, once for logout.
  auto logout_event = std::make_unique<pb::UserEventAtomicVariant>();
  auto login_event = std::make_unique<pb::UserEventAtomicVariant>();
  EXPECT_CALL(*batch_sender_, Enqueue(_))
      .Times(2)
      .WillOnce(WithArg<0>(
          Invoke([&login_event](
                     std::unique_ptr<google::protobuf::MessageLite> message) {
            login_event->ParseFromString(
                std::move(message->SerializeAsString()));
          })))
      .WillOnce(WithArg<0>(
          Invoke([&logout_event](
                     std::unique_ptr<google::protobuf::MessageLite> message) {
            logout_event->ParseFromString(
                std::move(message->SerializeAsString()));
          })));

  EXPECT_OK(plugin_->Activate());
  auth_factor_cb_.Run(completed);
  state_changed_cb_.Run(kStarted);

  auto expected_event = std::make_unique<pb::UserEventAtomicVariant>();
  expected_event->mutable_common();
  expected_event->mutable_logon()->mutable_authentication()->add_auth_factor(
      AuthFactorType::Authentication_AuthenticationType_AUTH_PASSWORD);
  expected_event->mutable_common()->set_device_user(kDeviceUser);
  expected_event->mutable_common()->set_create_timestamp_us(
      login_event->common().create_timestamp_us());
  EXPECT_THAT(*expected_event, EqualsProto(*login_event));
  EXPECT_EQ(AuthFactorType::Authentication_AuthenticationType_AUTH_TYPE_UNKNOWN,
            GetAuthFactor());

  // Logoff.
  state_changed_cb_.Run(kStopped);
  expected_event->mutable_logoff();
  expected_event->mutable_common()->set_create_timestamp_us(
      logout_event->common().create_timestamp_us());
  EXPECT_THAT(*expected_event, EqualsProto(*logout_event));
}

TEST_F(AuthenticationPluginTestFixture, LateAuthFactor) {
  SetupObjectProxies();
  SaveRegisterLockingCbs();
  SaveSessionStateChangeCb();
  EXPECT_CALL(*device_user_, GetDeviceUserAsync)
      .Times(2)
      .WillRepeatedly(WithArg<0>(Invoke(
          [](base::OnceCallback<void(const std::string& device_user)> cb) {
            std::move(cb).Run(kDeviceUser);
          })));

  user_data_auth::AuthenticateAuthFactorCompleted completed;
  completed.set_auth_factor_type(user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);

  auto login_event = std::make_unique<pb::UserEventAtomicVariant>();
  EXPECT_CALL(*batch_sender_, Enqueue(_))
      .Times(1)
      .WillOnce(WithArg<0>(
          Invoke([&login_event](
                     std::unique_ptr<google::protobuf::MessageLite> message) {
            login_event->ParseFromString(
                std::move(message->SerializeAsString()));
          })));

  EXPECT_OK(plugin_->Activate());
  // Have the state change cb run first to simulate late signal.
  state_changed_cb_.Run(kStarted);
  auth_factor_cb_.Run(completed);
  task_environment_.FastForwardBy(kWaitForAuthFactorS);

  ASSERT_EQ(1, login_event->logon().authentication().auth_factor_size());
  EXPECT_EQ(AuthFactorType::Authentication_AuthenticationType_AUTH_PASSWORD,
            login_event->logon().authentication().auth_factor()[0]);
  EXPECT_EQ(AuthFactorType::Authentication_AuthenticationType_AUTH_TYPE_UNKNOWN,
            GetAuthFactor());

  // Unlock.
  auto unlock_event = std::make_unique<pb::UserEventAtomicVariant>();
  EXPECT_CALL(*batch_sender_, Enqueue(_))
      .Times(1)
      .WillOnce(WithArg<0>(
          Invoke([&unlock_event](
                     std::unique_ptr<google::protobuf::MessageLite> message) {
            unlock_event->ParseFromString(
                std::move(message->SerializeAsString()));
          })));

  // Have the state change cb run first to simulate late signal.
  unlocked_cb_.Run();
  auth_factor_cb_.Run(completed);
  task_environment_.FastForwardBy(kWaitForAuthFactorS);

  ASSERT_EQ(1, unlock_event->unlock().authentication().auth_factor_size());
  EXPECT_EQ(AuthFactorType::Authentication_AuthenticationType_AUTH_PASSWORD,
            unlock_event->unlock().authentication().auth_factor()[0]);
  EXPECT_EQ(AuthFactorType::Authentication_AuthenticationType_AUTH_TYPE_UNKNOWN,
            GetAuthFactor());
}

TEST_F(AuthenticationPluginTestFixture, FailedLoginThenSuccess) {
  SetupObjectProxies();
  SaveRegisterLockingCbs();
  SaveSessionStateChangeCb();
  EXPECT_CALL(*device_user_, GetDeviceUserAsync)
      .Times(2)
      .WillRepeatedly(WithArg<0>(Invoke(
          [](base::OnceCallback<void(const std::string& device_user)> cb) {
            std::move(cb).Run(kDeviceUser);
          })));

  user_data_auth::AuthenticateAuthFactorCompleted failure;
  failure.mutable_error_info();
  failure.set_auth_factor_type(user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);

  auto failure_event = std::make_unique<pb::UserEventAtomicVariant>();
  auto login_event = std::make_unique<pb::UserEventAtomicVariant>();
  EXPECT_CALL(*batch_sender_, Enqueue(_))
      .Times(2)
      .WillOnce(WithArg<0>(
          Invoke([&failure_event](
                     std::unique_ptr<google::protobuf::MessageLite> message) {
            failure_event->ParseFromString(
                std::move(message->SerializeAsString()));
          })))
      .WillOnce(WithArg<0>(
          Invoke([&login_event](
                     std::unique_ptr<google::protobuf::MessageLite> message) {
            login_event->ParseFromString(
                std::move(message->SerializeAsString()));
          })));
  EXPECT_CALL(*batch_sender_,
              Visit(Eq(pb::UserEventAtomicVariant::kFailure), _, _))
      .WillOnce(Return(false))
      .WillOnce([&failure_event](auto unused_type, const auto& unused_key,
                                 BatchSenderType::VisitCallback cb) {
        std::move(cb).Run(failure_event.get());
        return true;
      });

  EXPECT_OK(plugin_->Activate());
  // 2 Failures.
  auth_factor_cb_.Run(failure);
  auth_factor_cb_.Run(failure);
  // Success.
  user_data_auth::AuthenticateAuthFactorCompleted completed;
  completed.set_auth_factor_type(user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  auth_factor_cb_.Run(completed);

  state_changed_cb_.Run(kStarted);

  // Failure.
  ASSERT_EQ(1, failure_event->failure().authentication().auth_factor_size());
  EXPECT_EQ(AuthFactorType::Authentication_AuthenticationType_AUTH_PASSWORD,
            failure_event->failure().authentication().auth_factor()[0]);
  EXPECT_EQ(2, failure_event->failure().authentication().num_failed_attempts());

  // Success.
  ASSERT_EQ(1, login_event->logon().authentication().auth_factor_size());
  EXPECT_EQ(AuthFactorType::Authentication_AuthenticationType_AUTH_PASSWORD,
            login_event->logon().authentication().auth_factor()[0]);
  EXPECT_EQ(AuthFactorType::Authentication_AuthenticationType_AUTH_TYPE_UNKNOWN,
            GetAuthFactor());
}

TEST_F(AuthenticationPluginTestFixture, FailedLoginAfterTimeout) {
  SetupObjectProxies();
  SaveRegisterLockingCbs();
  SaveSessionStateChangeCb();
  EXPECT_CALL(*device_user_, GetDeviceUserAsync)
      .Times(1)
      .WillOnce(WithArg<0>(Invoke(
          [](base::OnceCallback<void(const std::string& device_user)> cb) {
            std::move(cb).Run(kDeviceUser);
          })));

  user_data_auth::AuthenticateAuthFactorCompleted failure;
  failure.mutable_error_info();
  failure.set_auth_factor_type(user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);

  auto failure_event = std::make_unique<pb::UserEventAtomicVariant>();
  failure_event->mutable_common()->set_create_timestamp_us(5);
  EXPECT_CALL(*batch_sender_, Enqueue(_))
      .Times(1)
      .WillOnce(WithArg<0>(
          Invoke([&failure_event](
                     std::unique_ptr<google::protobuf::MessageLite> message) {
            failure_event->ParseFromString(
                std::move(message->SerializeAsString()));
          })));
  EXPECT_CALL(*batch_sender_,
              Visit(Eq(pb::UserEventAtomicVariant::kFailure), _, _))
      .WillOnce(Return(false))
      .WillRepeatedly([&failure_event](auto unused_type, const auto& unused_key,
                                       BatchSenderType::VisitCallback cb) {
        std::move(cb).Run(failure_event.get());
        return true;
      });

  EXPECT_OK(plugin_->Activate());
  // 3 Failures.
  auth_factor_cb_.Run(failure);
  auth_factor_cb_.Run(failure);
  auth_factor_cb_.Run(failure);

  // Failure.
  ASSERT_EQ(1, failure_event->failure().authentication().auth_factor_size());
  EXPECT_EQ(AuthFactorType::Authentication_AuthenticationType_AUTH_PASSWORD,
            failure_event->failure().authentication().auth_factor()[0]);
  EXPECT_EQ(3, failure_event->failure().authentication().num_failed_attempts());
}

TEST_F(AuthenticationPluginTestFixture, FailedLoginCreateTimestampSquashing) {
  SetupObjectProxies();
  SaveRegisterLockingCbs();
  SaveSessionStateChangeCb();
  EXPECT_CALL(*device_user_, GetDeviceUserAsync)
      .Times(3)
      .WillRepeatedly(WithArg<0>(Invoke(
          [](base::OnceCallback<void(const std::string& device_user)> cb) {
            std::move(cb).Run(kDeviceUser);
          })));

  user_data_auth::AuthenticateAuthFactorCompleted failure;
  failure.mutable_error_info();
  failure.set_auth_factor_type(user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);

  auto failure_event_1 = std::make_unique<pb::UserEventAtomicVariant>();
  auto failure_event_2 = std::make_unique<pb::UserEventAtomicVariant>();
  auto login_event = std::make_unique<pb::UserEventAtomicVariant>();
  EXPECT_CALL(*batch_sender_, Enqueue(_))
      .Times(3)
      .WillOnce(WithArg<0>(
          Invoke([&failure_event_1](
                     std::unique_ptr<google::protobuf::MessageLite> message) {
            failure_event_1->ParseFromString(
                std::move(message->SerializeAsString()));
          })))
      .WillOnce(WithArg<0>(
          Invoke([&login_event](
                     std::unique_ptr<google::protobuf::MessageLite> message) {
            login_event->ParseFromString(
                std::move(message->SerializeAsString()));
          })))
      .WillOnce(WithArg<0>(
          Invoke([&failure_event_2](
                     std::unique_ptr<google::protobuf::MessageLite> message) {
            failure_event_2->ParseFromString(
                std::move(message->SerializeAsString()));
          })));

  EXPECT_CALL(*batch_sender_,
              Visit(Eq(pb::UserEventAtomicVariant::kFailure), _, _))
      .Times(3)
      .WillOnce(Return(false))
      .WillOnce([&failure_event_1](auto unused_type, const auto& unused_key,
                                   BatchSenderType::VisitCallback cb) {
        return std::move(cb).Run(failure_event_1.get());
      })
      .WillOnce([&failure_event_2](auto unused_type, const auto& unused_key,
                                   BatchSenderType::VisitCallback cb) {
        return std::move(cb).Run(failure_event_2.get());
      });

  EXPECT_OK(plugin_->Activate());

  // 2 Failures.
  auth_factor_cb_.Run(failure);
  auth_factor_cb_.Run(failure);
  // Successful login.
  user_data_auth::AuthenticateAuthFactorCompleted completed;
  completed.set_auth_factor_type(user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  auth_factor_cb_.Run(completed);
  state_changed_cb_.Run(kStarted);

  // Another failure but timestamp is after successful login.
  auth_factor_cb_.Run(failure);

  // Failure 1.
  ASSERT_EQ(1, failure_event_1->failure().authentication().auth_factor_size());
  EXPECT_EQ(AuthFactorType::Authentication_AuthenticationType_AUTH_PASSWORD,
            failure_event_1->failure().authentication().auth_factor()[0]);
  EXPECT_EQ(2,
            failure_event_1->failure().authentication().num_failed_attempts());

  // Success.
  ASSERT_EQ(1, login_event->logon().authentication().auth_factor_size());
  EXPECT_EQ(AuthFactorType::Authentication_AuthenticationType_AUTH_PASSWORD,
            login_event->logon().authentication().auth_factor()[0]);

  // Failure 2.
  ASSERT_EQ(1, failure_event_2->failure().authentication().auth_factor_size());
  EXPECT_EQ(AuthFactorType::Authentication_AuthenticationType_AUTH_PASSWORD,
            failure_event_2->failure().authentication().auth_factor()[0]);
  EXPECT_EQ(1,
            failure_event_2->failure().authentication().num_failed_attempts());
}

TEST_F(AuthenticationPluginTestFixture, FailedLoginAuthFactorSquashing) {
  SetupObjectProxies();
  SaveRegisterLockingCbs();
  SaveSessionStateChangeCb();
  EXPECT_CALL(*device_user_, GetDeviceUserAsync)
      .Times(2)
      .WillRepeatedly(WithArg<0>(Invoke(
          [](base::OnceCallback<void(const std::string& device_user)> cb) {
            std::move(cb).Run(kDeviceUser);
          })));

  user_data_auth::AuthenticateAuthFactorCompleted failure;
  failure.mutable_error_info();
  failure.set_auth_factor_type(user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);

  auto failure_event_1 = std::make_unique<pb::UserEventAtomicVariant>();
  auto failure_event_2 = std::make_unique<pb::UserEventAtomicVariant>();
  EXPECT_CALL(*batch_sender_, Enqueue(_))
      .Times(2)
      .WillOnce(WithArg<0>(
          Invoke([&failure_event_1](
                     std::unique_ptr<google::protobuf::MessageLite> message) {
            failure_event_1->ParseFromString(
                std::move(message->SerializeAsString()));
          })))
      .WillOnce(WithArg<0>(
          Invoke([&failure_event_2](
                     std::unique_ptr<google::protobuf::MessageLite> message) {
            failure_event_2->ParseFromString(
                std::move(message->SerializeAsString()));
          })));

  EXPECT_CALL(*batch_sender_,
              Visit(Eq(pb::UserEventAtomicVariant::kFailure), _, _))
      .Times(3)
      .WillOnce(Return(false))
      .WillRepeatedly([&failure_event_1](auto unused_type,
                                         const auto& unused_key,
                                         BatchSenderType::VisitCallback cb) {
        return std::move(cb).Run(failure_event_1.get());
      });

  EXPECT_OK(plugin_->Activate());

  // 2 Failures of same type.
  auth_factor_cb_.Run(failure);
  auth_factor_cb_.Run(failure);

  // 1 failure with different auth type.
  failure.set_auth_factor_type(user_data_auth::AUTH_FACTOR_TYPE_PIN);
  auth_factor_cb_.Run(failure);

  // Failure 1.
  ASSERT_EQ(1, failure_event_1->failure().authentication().auth_factor_size());
  EXPECT_EQ(AuthFactorType::Authentication_AuthenticationType_AUTH_PASSWORD,
            failure_event_1->failure().authentication().auth_factor()[0]);
  EXPECT_EQ(2,
            failure_event_1->failure().authentication().num_failed_attempts());

  // Failure 2.
  ASSERT_EQ(1, failure_event_2->failure().authentication().auth_factor_size());
  EXPECT_EQ(AuthFactorType::Authentication_AuthenticationType_AUTH_PIN,
            failure_event_2->failure().authentication().auth_factor()[0]);
  EXPECT_EQ(1,
            failure_event_2->failure().authentication().num_failed_attempts());
}

}  // namespace secagentd::testing
