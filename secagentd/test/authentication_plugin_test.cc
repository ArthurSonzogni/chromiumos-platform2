// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <cstring>
#include <iterator>
#include <memory>
#include <optional>

#include "base/functional/callback_forward.h"
#include "base/memory/scoped_refptr.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "missive/util/status.h"
#include "secagentd/device_user.h"
#include "secagentd/plugins.h"
#include "secagentd/proto/security_xdr_events.pb.h"
#include "secagentd/test/mock_device_user.h"
#include "secagentd/test/mock_message_sender.h"
#include "secagentd/test/mock_policies_features_broker.h"
#include "secagentd/test/mock_process_cache.h"
#include "secagentd/test/test_utils.h"

namespace secagentd::testing {

namespace pb = cros_xdr::reporting;

using ::testing::_;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::WithArg;
using ::testing::WithArgs;

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
        policies_features_broker_, device_user_, -1);
    EXPECT_NE(nullptr, plugin_);
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

  scoped_refptr<MockMessageSender> message_sender_;
  scoped_refptr<MockPoliciesFeaturesBroker> policies_features_broker_;
  scoped_refptr<MockDeviceUser> device_user_;
  std::unique_ptr<PluginFactory> plugin_factory_;
  std::unique_ptr<PluginInterface> plugin_;
  base::RepeatingCallback<void()> locked_cb_;
  base::RepeatingCallback<void()> unlocked_cb_;
  base::RepeatingCallback<void(const std::string& state)> state_changed_cb_;
};

TEST_F(AuthenticationPluginTestFixture, TestGetName) {
  ASSERT_EQ("Authentication", plugin_->GetName());
}

TEST_F(AuthenticationPluginTestFixture, TestScreenLockToUnlock) {
  SaveRegisterLockingCbs();
  EXPECT_CALL(*device_user_, GetDeviceUser)
      .Times(2)
      .WillRepeatedly(Return(kDeviceUser));

  // message_sender_ will be called twice. Once for lock, once for unlock.
  auto lock_event = std::make_unique<pb::XdrAuthenticateEvent>();
  auto unlock_event = std::make_unique<pb::XdrAuthenticateEvent>();
  EXPECT_CALL(*message_sender_,
              SendMessage(reporting::Destination::CROS_SECURITY_USER, _, _, _))
      .Times(2)
      .WillOnce(WithArg<2>(
          Invoke([&lock_event](
                     std::unique_ptr<google::protobuf::MessageLite> message) {
            lock_event->ParseFromString(
                std::move(message->SerializeAsString()));
          })))
      .WillOnce(WithArg<2>(
          Invoke([&unlock_event](
                     std::unique_ptr<google::protobuf::MessageLite> message) {
            unlock_event->ParseFromString(
                std::move(message->SerializeAsString()));
          })));

  EXPECT_OK(plugin_->Activate());
  locked_cb_.Run();
  auto expected_event = std::make_unique<pb::XdrAuthenticateEvent>();
  expected_event->mutable_common();
  auto expected_batched = expected_event->add_batched_events();
  expected_batched->mutable_lock();
  expected_batched->mutable_common()->set_device_user(kDeviceUser);
  expected_batched->mutable_common()->set_create_timestamp_us(
      lock_event->batched_events()[0].common().create_timestamp_us());
  EXPECT_THAT(*expected_event, EqualsProto(*lock_event));

  unlocked_cb_.Run();
  expected_batched->mutable_unlock()->mutable_authentication()->add_auth_factor(
      pb::Authentication_AuthenticationType_AUTH_TYPE_UNKNOWN);
  expected_batched->mutable_common()->set_create_timestamp_us(
      unlock_event->batched_events()[0].common().create_timestamp_us());
  EXPECT_THAT(*expected_event, EqualsProto(*unlock_event));
}

TEST_F(AuthenticationPluginTestFixture, TestScreenLoginToLogout) {
  SaveSessionStateChangeCb();
  EXPECT_CALL(*device_user_, GetDeviceUser)
      .Times(2)
      .WillRepeatedly(Return(kDeviceUser));

  // message_sender_ will be called twice. Once for login, once for logout.
  auto logout_event = std::make_unique<pb::XdrAuthenticateEvent>();
  auto login_event = std::make_unique<pb::XdrAuthenticateEvent>();
  EXPECT_CALL(*message_sender_,
              SendMessage(reporting::Destination::CROS_SECURITY_USER, _, _, _))
      .Times(2)
      .WillOnce(WithArg<2>(
          Invoke([&login_event](
                     std::unique_ptr<google::protobuf::MessageLite> message) {
            login_event->ParseFromString(
                std::move(message->SerializeAsString()));
          })))
      .WillOnce(WithArg<2>(
          Invoke([&logout_event](
                     std::unique_ptr<google::protobuf::MessageLite> message) {
            logout_event->ParseFromString(
                std::move(message->SerializeAsString()));
          })));

  EXPECT_OK(plugin_->Activate());

  state_changed_cb_.Run(kStarted);
  auto expected_event = std::make_unique<pb::XdrAuthenticateEvent>();
  expected_event->mutable_common();
  auto expected_batched = expected_event->add_batched_events();
  expected_batched->mutable_logon()->mutable_authentication()->add_auth_factor(
      pb::Authentication_AuthenticationType_AUTH_TYPE_UNKNOWN);
  expected_batched->mutable_common()->set_device_user(kDeviceUser);
  expected_batched->mutable_common()->set_create_timestamp_us(
      login_event->batched_events()[0].common().create_timestamp_us());
  EXPECT_THAT(*expected_event, EqualsProto(*login_event));

  state_changed_cb_.Run(kStopped);
  expected_batched->mutable_logoff();
  expected_batched->mutable_common()->set_create_timestamp_us(
      logout_event->batched_events()[0].common().create_timestamp_us());
  EXPECT_THAT(*expected_event, EqualsProto(*logout_event));
}

}  // namespace secagentd::testing
