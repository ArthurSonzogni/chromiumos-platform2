// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <optional>
#include <utility>

#include <base/test/task_environment.h>
#include <chromeos/dbus/service_constants.h>
#include <biod/mock_biod_metrics.h>
#include <biod/session_state_manager.h>
#include <dbus/error.h>
#include <dbus/login_manager/dbus-constants.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_object_proxy.h>
#include <dbus/object_path.h>
#include <power_manager/proto_bindings/suspend.pb.h>

namespace biod {
namespace {

using testing::_;
using testing::A;
using testing::ByMove;
using testing::DoAll;
using testing::Invoke;
using testing::Return;
using testing::SaveArg;

constexpr char kUsername[] = "user@user.com";
constexpr char kSanitizedUsername[] =
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
static_assert(sizeof(kSanitizedUsername) == 41);
constexpr char kExampleConnectionName[] = ":1.33";

MATCHER_P(IsMember, name, "") {
  if (arg->GetMember() != name) {
    *result_listener << "has member " << arg->GetMember();
    return false;
  }
  return true;
}

class MockSessionStateObserver : public SessionStateManagerInterface::Observer {
 public:
  MOCK_METHOD(void,
              OnUserLoggedIn,
              (const std::string& sanitized_username, bool is_new_login),
              (override));
  MOCK_METHOD(void, OnUserLoggedOut, (), (override));
  MOCK_METHOD(void, OnSessionResumedFromHibernate, (), (override));
};

class SessionStateManagerTest : public ::testing::Test {
 public:
  SessionStateManagerTest() {
    dbus::Bus::Options options;
    options.bus_type = dbus::Bus::SYSTEM;
    bus_ = new dbus::MockBus(options);

    proxy_ = new dbus::MockObjectProxy(
        bus_.get(), login_manager::kSessionManagerServiceName,
        dbus::ObjectPath(login_manager::kSessionManagerServicePath));

    EXPECT_CALL(*bus_,
                GetObjectProxy(login_manager::kSessionManagerServiceName, _))
        .WillRepeatedly(Return(proxy_.get()));

    EXPECT_CALL(*bus_,
                GetObjectProxy(power_manager::kPowerManagerServiceName, _))
        .WillRepeatedly(Return(proxy_.get()));

    EXPECT_CALL(*proxy_, DoConnectToSignal(
                             login_manager::kSessionManagerInterface, _, _, _))
        .WillRepeatedly(
            Invoke(this, &SessionStateManagerTest::ConnectToSignal));

    EXPECT_CALL(*proxy_, DoConnectToSignal(
                             power_manager::kPowerManagerInterface, _, _, _))
        .WillRepeatedly(
            Invoke(this, &SessionStateManagerTest::ConnectToSignal));

    // Save NameOwnerChanged callback
    EXPECT_CALL(*proxy_, SetNameOwnerChangedCallback)
        .WillRepeatedly(SaveArg<0>(&on_name_owner_changed_));

    mock_metrics_ = std::make_unique<metrics::MockBiodMetrics>();

    manager_.emplace(bus_.get(), mock_metrics_.get());
  }

 protected:
  void EmitStateChangedSignal(const std::string& state);
  void EmitSuspendDone(const std::vector<uint8_t>& msg);

  std::unique_ptr<dbus::Response> RetrievePrimarySessionResponse(
      const char* username, const char* sanitized_username);

  base::test::SingleThreadTaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  scoped_refptr<dbus::MockBus> bus_;
  scoped_refptr<dbus::MockObjectProxy> proxy_;
  dbus::MockObjectProxy::NameOwnerChangedCallback on_name_owner_changed_;
  MockSessionStateObserver observer_;
  std::unique_ptr<metrics::MockBiodMetrics> mock_metrics_;
  std::optional<SessionStateManager> manager_;

 private:
  void ConnectToSignal(
      const std::string& interface_name,
      const std::string& signal_name,
      dbus::ObjectProxy::SignalCallback signal_callback,
      dbus::ObjectProxy::OnConnectedCallback* on_connected_callback);

  std::map<std::string, dbus::ObjectProxy::SignalCallback> signal_callbacks_;
};

void SessionStateManagerTest::ConnectToSignal(
    const std::string& interface_name,
    const std::string& signal_name,
    dbus::ObjectProxy::SignalCallback signal_callback,
    dbus::ObjectProxy::OnConnectedCallback* on_connected_callback) {
  EXPECT_TRUE(interface_name == login_manager::kSessionManagerInterface ||
              interface_name == power_manager::kPowerManagerInterface);
  signal_callbacks_[signal_name] = std::move(signal_callback);
  task_environment_.GetMainThreadTaskRunner()->PostTask(
      FROM_HERE,
      base::BindOnce(std::move(*on_connected_callback), interface_name,
                     signal_name, true /* success */));
}

void SessionStateManagerTest::EmitStateChangedSignal(const std::string& state) {
  const auto it =
      signal_callbacks_.find(login_manager::kSessionStateChangedSignal);
  ASSERT_TRUE(it != signal_callbacks_.end())
      << "Client didn't register for SessionStateChanged signal";

  dbus::Signal signal(login_manager::kSessionManagerInterface,
                      login_manager::kSessionStateChangedSignal);
  dbus::MessageWriter writer(&signal);
  writer.AppendString(state);

  it->second.Run(&signal);
}

std::unique_ptr<dbus::Response>
SessionStateManagerTest::RetrievePrimarySessionResponse(
    const char* username, const char* sanitized_username) {
  std::unique_ptr<dbus::Response> response(dbus::Response::CreateEmpty());
  dbus::MessageWriter writer(response.get());
  // Add username.
  writer.AppendString(username);
  // Add sanitized username.
  writer.AppendString(sanitized_username);

  return response;
}

void SessionStateManagerTest::EmitSuspendDone(const std::vector<uint8_t>& msg) {
  const auto it = signal_callbacks_.find(power_manager::kSuspendDoneSignal);
  ASSERT_TRUE(it != signal_callbacks_.end())
      << "Client didn't register for OnSuspendDone signal";

  dbus::Signal signal(power_manager::kPowerManagerInterface,
                      power_manager::kSuspendDoneSignal);
  dbus::MessageWriter writer(&signal);
  writer.AppendArrayOfBytes(msg.data(), msg.size());
  it->second.Run(&signal);
}

// Tests that check biod behavior on SessionManager communication errors.
TEST_F(SessionStateManagerTest, TestPrimaryUserErrorNoReply) {
  EXPECT_CALL(
      *proxy_,
      CallMethodAndBlock(
          IsMember(login_manager::kSessionManagerRetrievePrimarySession),
          A<int>()))
      .WillOnce(
          [](dbus::MethodCall* method_call, int timeout_ms)
              -> base::expected<std::unique_ptr<dbus::Response>, dbus::Error> {
            return base::unexpected(
                dbus::Error(dbus_constants::kDBusErrorNoReply, "Timeout"));
          });

  EXPECT_CALL(*mock_metrics_,
              SendSessionRetrievePrimarySessionResult(
                  BiodMetrics::RetrievePrimarySessionResult::kErrorDBusNoReply))
      .Times(1);
  EXPECT_FALSE(manager_->RefreshPrimaryUser());
  EXPECT_TRUE(manager_->GetPrimaryUser().empty());
}

TEST_F(SessionStateManagerTest, TestPrimaryUserErrorServiceUnknown) {
  EXPECT_CALL(
      *proxy_,
      CallMethodAndBlock(
          IsMember(login_manager::kSessionManagerRetrievePrimarySession),
          A<int>()))
      .WillOnce(
          [](dbus::MethodCall* method_call, int timeout_ms)
              -> base::expected<std::unique_ptr<dbus::Response>, dbus::Error> {
            return base::unexpected(dbus::Error(
                dbus_constants::kDBusErrorServiceUnknown, "Service unknown"));
          });

  EXPECT_CALL(
      *mock_metrics_,
      SendSessionRetrievePrimarySessionResult(
          BiodMetrics::RetrievePrimarySessionResult::kErrorDBusServiceUnknown))
      .Times(1);
  EXPECT_FALSE(manager_->RefreshPrimaryUser());
  EXPECT_TRUE(manager_->GetPrimaryUser().empty());
}

TEST_F(SessionStateManagerTest, TestPrimaryUserErrorOther) {
  EXPECT_CALL(
      *proxy_,
      CallMethodAndBlock(
          IsMember(login_manager::kSessionManagerRetrievePrimarySession),
          A<int>()))
      .WillOnce(
          [](dbus::MethodCall* method_call, int timeout_ms)
              -> base::expected<std::unique_ptr<dbus::Response>, dbus::Error> {
            return base::unexpected(
                dbus::Error("TestError", "This is a test error"));
          });

  EXPECT_CALL(*mock_metrics_,
              SendSessionRetrievePrimarySessionResult(
                  BiodMetrics::RetrievePrimarySessionResult::kErrorUnknown))
      .Times(1);
  EXPECT_FALSE(manager_->RefreshPrimaryUser());
  EXPECT_TRUE(manager_->GetPrimaryUser().empty());
}

// Tests that check invalid response from SessionManager.
TEST_F(SessionStateManagerTest, TestPrimaryUserNullResponse) {
  EXPECT_CALL(
      *proxy_,
      CallMethodAndBlock(
          IsMember(login_manager::kSessionManagerRetrievePrimarySession),
          A<int>()))
      .WillOnce(Return(ByMove(base::ok(nullptr))));

  EXPECT_CALL(
      *mock_metrics_,
      SendSessionRetrievePrimarySessionResult(
          BiodMetrics::RetrievePrimarySessionResult::kErrorResponseMissing))
      .Times(1);
  EXPECT_FALSE(manager_->RefreshPrimaryUser());
  EXPECT_TRUE(manager_->GetPrimaryUser().empty());
}

TEST_F(SessionStateManagerTest, TestPrimaryUserNoDataInResponse) {
  // Prepare empty response.
  std::unique_ptr<dbus::Response> response(dbus::Response::CreateEmpty());

  EXPECT_CALL(
      *proxy_,
      CallMethodAndBlock(
          IsMember(login_manager::kSessionManagerRetrievePrimarySession),
          A<int>()))
      .WillOnce(Return(ByMove(base::ok(std::move(response)))));

  EXPECT_CALL(*mock_metrics_,
              SendSessionRetrievePrimarySessionResult(
                  BiodMetrics::RetrievePrimarySessionResult::kErrorParsing))
      .Times(1);
  EXPECT_FALSE(manager_->RefreshPrimaryUser());
  EXPECT_TRUE(manager_->GetPrimaryUser().empty());
}

TEST_F(SessionStateManagerTest, TestPrimaryUserNoSanitizedUsername) {
  // Prepare response with username only.
  std::unique_ptr<dbus::Response> response(dbus::Response::CreateEmpty());
  dbus::MessageWriter writer(response.get());
  // Add username.
  writer.AppendString("");

  EXPECT_CALL(
      *proxy_,
      CallMethodAndBlock(
          IsMember(login_manager::kSessionManagerRetrievePrimarySession),
          A<int>()))
      .WillOnce(Return(ByMove(base::ok(std::move(response)))));

  EXPECT_CALL(*mock_metrics_,
              SendSessionRetrievePrimarySessionResult(
                  BiodMetrics::RetrievePrimarySessionResult::kErrorParsing))
      .Times(1);
  EXPECT_FALSE(manager_->RefreshPrimaryUser());
  EXPECT_TRUE(manager_->GetPrimaryUser().empty());
}

TEST_F(SessionStateManagerTest, TestPrimaryUserUsernameNotString) {
  // Prepare response with an integer instead of username.
  std::unique_ptr<dbus::Response> response(dbus::Response::CreateEmpty());
  dbus::MessageWriter writer(response.get());
  // Add username, integer in this case.
  writer.AppendInt32(0);
  // Add sanitized username.
  writer.AppendString(kSanitizedUsername);

  EXPECT_CALL(
      *proxy_,
      CallMethodAndBlock(
          IsMember(login_manager::kSessionManagerRetrievePrimarySession),
          A<int>()))
      .WillOnce(Return(ByMove(base::ok(std::move(response)))));

  EXPECT_CALL(*mock_metrics_,
              SendSessionRetrievePrimarySessionResult(
                  BiodMetrics::RetrievePrimarySessionResult::kErrorParsing))
      .Times(1);
  EXPECT_FALSE(manager_->RefreshPrimaryUser());
  EXPECT_TRUE(manager_->GetPrimaryUser().empty());
}

TEST_F(SessionStateManagerTest, TestPrimaryUserSanitizedUsernameNotString) {
  // Prepare response with an integer instead of sanitized username..
  std::unique_ptr<dbus::Response> response(dbus::Response::CreateEmpty());
  dbus::MessageWriter writer(response.get());
  // Add username.
  writer.AppendString(kUsername);
  // Add sanitized username, integer in this case.
  writer.AppendInt32(0);

  EXPECT_CALL(
      *proxy_,
      CallMethodAndBlock(
          IsMember(login_manager::kSessionManagerRetrievePrimarySession),
          A<int>()))
      .WillOnce(Return(ByMove(base::ok(std::move(response)))));

  EXPECT_CALL(*mock_metrics_,
              SendSessionRetrievePrimarySessionResult(
                  BiodMetrics::RetrievePrimarySessionResult::kErrorParsing))
      .Times(1);
  EXPECT_FALSE(manager_->RefreshPrimaryUser());
  EXPECT_TRUE(manager_->GetPrimaryUser().empty());
}

TEST_F(SessionStateManagerTest, TestPrimaryUserNoSessionAvailable) {
  // Prepare response with no primary user.
  std::unique_ptr<dbus::Response> response =
      RetrievePrimarySessionResponse("", "");

  EXPECT_CALL(
      *proxy_,
      CallMethodAndBlock(
          IsMember(login_manager::kSessionManagerRetrievePrimarySession),
          A<int>()))
      .WillOnce(Return(ByMove(base::ok(std::move(response)))));

  EXPECT_CALL(*mock_metrics_,
              SendSessionRetrievePrimarySessionResult(
                  BiodMetrics::RetrievePrimarySessionResult::kSuccess))
      .Times(1);
  EXPECT_FALSE(manager_->RefreshPrimaryUser());
  EXPECT_TRUE(manager_->GetPrimaryUser().empty());
}

TEST_F(SessionStateManagerTest, TestPrimaryUserSuccess) {
  // Prepare response with information about primary user.
  std::unique_ptr<dbus::Response> response =
      RetrievePrimarySessionResponse(kUsername, kSanitizedUsername);

  // During first call to RefreshPrimaryUser() we expect to call
  // RetrievePrimarySession DBus method.
  EXPECT_CALL(
      *proxy_,
      CallMethodAndBlock(
          IsMember(login_manager::kSessionManagerRetrievePrimarySession),
          A<int>()))
      .WillOnce(Return(ByMove(base::ok(std::move(response)))));

  EXPECT_CALL(*mock_metrics_,
              SendSessionRetrievePrimarySessionResult(
                  BiodMetrics::RetrievePrimarySessionResult::kSuccess))
      .Times(1);
  EXPECT_TRUE(manager_->RefreshPrimaryUser());
  EXPECT_EQ(manager_->GetPrimaryUser(), kSanitizedUsername);
}

TEST_F(SessionStateManagerTest, TestRetrievePrimarySessionCallDuration) {
  ON_CALL(*proxy_,
          CallMethodAndBlock(
              IsMember(login_manager::kSessionManagerRetrievePrimarySession),
              A<int>()))
      .WillByDefault([this](dbus::MethodCall* method, int delay) {
        // Prepare response with information about primary user.
        std::unique_ptr<dbus::Response> response =
            RetrievePrimarySessionResponse(kUsername, kSanitizedUsername);

        task_environment_.FastForwardBy(base::Milliseconds(101));
        return base::ok(std::move(response));
      });

  // Check that duration is greater or equal to 0.
  EXPECT_CALL(*mock_metrics_, SendSessionRetrievePrimarySessionDuration(101))
      .Times(1);
  EXPECT_TRUE(manager_->RefreshPrimaryUser());
}

TEST_F(SessionStateManagerTest, TestRefreshPrimarySessionNotifies) {
  manager_->AddObserver(&observer_);

  // Prepare response with information about primary user.
  std::unique_ptr<dbus::Response> response =
      RetrievePrimarySessionResponse(kUsername, kSanitizedUsername);

  // Prepare response with no primary user.
  std::unique_ptr<dbus::Response> response_no_user =
      RetrievePrimarySessionResponse("", "");

  // First call to RefreshPrimaryUser() will return information about logged
  // user, second call will return information that no one is logged in.
  EXPECT_CALL(
      *proxy_,
      CallMethodAndBlock(
          IsMember(login_manager::kSessionManagerRetrievePrimarySession),
          A<int>()))
      .WillOnce(Return(ByMove(base::ok(std::move(response)))))
      .WillOnce(Return(ByMove(base::ok(std::move(response_no_user)))));

  EXPECT_CALL(observer_, OnUserLoggedIn(kSanitizedUsername, false)).Times(1);
  EXPECT_TRUE(manager_->RefreshPrimaryUser());

  EXPECT_CALL(observer_, OnUserLoggedOut).Times(1);
  EXPECT_FALSE(manager_->RefreshPrimaryUser());
}

TEST_F(SessionStateManagerTest, TestRefreshPrimarySessionNoChangeLogin) {
  manager_->AddObserver(&observer_);

  // Prepare response with information about primary user.
  std::unique_ptr<dbus::Response> response1 =
      RetrievePrimarySessionResponse(kUsername, kSanitizedUsername);

  // Prepare response with information about primary user.
  std::unique_ptr<dbus::Response> response2 =
      RetrievePrimarySessionResponse(kUsername, kSanitizedUsername);

  // Both calls to RefreshPrimaryUser() will return information about logged
  // user.
  EXPECT_CALL(
      *proxy_,
      CallMethodAndBlock(
          IsMember(login_manager::kSessionManagerRetrievePrimarySession),
          A<int>()))
      .WillOnce(Return(ByMove(base::ok(std::move(response1)))))
      .WillOnce(Return(ByMove(base::ok(std::move(response2)))));

  EXPECT_CALL(observer_, OnUserLoggedIn(kSanitizedUsername, false)).Times(1);
  EXPECT_TRUE(manager_->RefreshPrimaryUser());

  // Expect we don't get notification.
  EXPECT_CALL(observer_, OnUserLoggedIn).Times(0);
  EXPECT_TRUE(manager_->RefreshPrimaryUser());
}

TEST_F(SessionStateManagerTest, TestRefreshPrimarySessionNoChangeLogout) {
  manager_->AddObserver(&observer_);

  // Prepare response with no primary user.
  std::unique_ptr<dbus::Response> response_no_user1 =
      RetrievePrimarySessionResponse("", "");

  // Prepare response with no primary user.
  std::unique_ptr<dbus::Response> response_no_user2 =
      RetrievePrimarySessionResponse("", "");

  // Both calls to RefreshPrimaryUser() will return information that no one
  // is logged in.
  EXPECT_CALL(
      *proxy_,
      CallMethodAndBlock(
          IsMember(login_manager::kSessionManagerRetrievePrimarySession),
          A<int>()))
      .WillOnce(Return(ByMove(base::ok(std::move(response_no_user1)))))
      .WillOnce(Return(ByMove(base::ok(std::move(response_no_user2)))));

  EXPECT_CALL(observer_, OnUserLoggedOut).Times(0);
  EXPECT_FALSE(manager_->RefreshPrimaryUser());
  EXPECT_FALSE(manager_->RefreshPrimaryUser());
}

TEST_F(SessionStateManagerTest, TestStateChangeStarted) {
  // Prepare response with information about primary user.
  std::unique_ptr<dbus::Response> response =
      RetrievePrimarySessionResponse(kUsername, kSanitizedUsername);

  // After first SessionStateChange signal we expect to call
  // RetrievePrimarySession DBus method.
  EXPECT_CALL(
      *proxy_,
      CallMethodAndBlock(
          IsMember(login_manager::kSessionManagerRetrievePrimarySession),
          A<int>()))
      .WillOnce(Return(ByMove(base::ok(std::move(response)))));

  manager_->AddObserver(&observer_);
  EXPECT_CALL(observer_, OnUserLoggedIn(kSanitizedUsername, true)).Times(1);

  EmitStateChangedSignal(dbus_constants::kSessionStateStarted);
  EXPECT_EQ(manager_->GetPrimaryUser(), kSanitizedUsername);

  // After second SessionStateChange signal we don't expect to call
  // RetrievePrimarySession DBus method.
  EXPECT_CALL(
      *proxy_,
      CallMethodAndBlock(
          IsMember(login_manager::kSessionManagerRetrievePrimarySession),
          A<int>()))
      .Times(0);
  EXPECT_CALL(observer_, OnUserLoggedIn).Times(0);

  EmitStateChangedSignal(dbus_constants::kSessionStateStarted);
  EXPECT_EQ(manager_->GetPrimaryUser(), kSanitizedUsername);
}

TEST_F(SessionStateManagerTest, TestStateChangeStopped) {
  manager_->AddObserver(&observer_);

  // Change state to stopped.`
  EXPECT_CALL(observer_, OnUserLoggedOut).Times(1);
  EmitStateChangedSignal(dbus_constants::kSessionStateStopped);
}

TEST_F(SessionStateManagerTest, TestStateChangeStartedStoppedStarted) {
  // Prepare response with information about primary user.
  std::unique_ptr<dbus::Response> response =
      RetrievePrimarySessionResponse(kUsername, kSanitizedUsername);

  manager_->AddObserver(&observer_);

  // After first SessionStateChange signal we expect to call
  // RetrievePrimarySession DBus method.
  EXPECT_CALL(
      *proxy_,
      CallMethodAndBlock(
          IsMember(login_manager::kSessionManagerRetrievePrimarySession),
          A<int>()))
      .WillOnce(Return(ByMove(base::ok(std::move(response)))));
  EXPECT_CALL(observer_, OnUserLoggedIn(kSanitizedUsername, true)).Times(1);

  EmitStateChangedSignal(dbus_constants::kSessionStateStarted);
  EXPECT_EQ(manager_->GetPrimaryUser(), kSanitizedUsername);

  // Change state to stopped.`
  EXPECT_CALL(observer_, OnUserLoggedOut).Times(1);
  EmitStateChangedSignal(dbus_constants::kSessionStateStopped);
  EXPECT_TRUE(manager_->GetPrimaryUser().empty());

  // Prepare response with information about primary user.
  std::unique_ptr<dbus::Response> response2 =
      RetrievePrimarySessionResponse(kUsername, kSanitizedUsername);

  // After third SessionStateChange signal we expect to call
  // RetrievePrimarySession DBus method.
  EXPECT_CALL(
      *proxy_,
      CallMethodAndBlock(
          IsMember(login_manager::kSessionManagerRetrievePrimarySession),
          A<int>()))
      .WillOnce(Return(ByMove(base::ok(std::move(response2)))));
  EXPECT_CALL(observer_, OnUserLoggedIn(kSanitizedUsername, true)).Times(1);

  EmitStateChangedSignal(dbus_constants::kSessionStateStarted);
  EXPECT_EQ(manager_->GetPrimaryUser(), kSanitizedUsername);
}

TEST_F(SessionStateManagerTest, TestStateChangeStartedNoUser) {
  // Prepare response with empty primary user.
  std::unique_ptr<dbus::Response> response =
      RetrievePrimarySessionResponse("", "");

  manager_->AddObserver(&observer_);

  // After first SessionStateChange signal we expect to call
  // RetrievePrimarySession DBus method, but OnUserLoggedIn method
  // shouldn't be called because there is no primary user.
  EXPECT_CALL(
      *proxy_,
      CallMethodAndBlock(
          IsMember(login_manager::kSessionManagerRetrievePrimarySession),
          A<int>()))
      .WillOnce(Return(ByMove(base::ok(std::move(response)))));
  EXPECT_CALL(observer_, OnUserLoggedIn).Times(0);

  EmitStateChangedSignal(dbus_constants::kSessionStateStarted);

  // Prepare response with information about primary user.
  std::unique_ptr<dbus::Response> response2 =
      RetrievePrimarySessionResponse(kUsername, kSanitizedUsername);

  // After second SessionStateChange signal we expect to call
  // RetrievePrimarySession DBus method.
  EXPECT_CALL(
      *proxy_,
      CallMethodAndBlock(
          IsMember(login_manager::kSessionManagerRetrievePrimarySession),
          A<int>()))
      .WillOnce(Return(ByMove(base::ok(std::move(response2)))));
  EXPECT_CALL(observer_, OnUserLoggedIn(kSanitizedUsername, true)).Times(1);

  EmitStateChangedSignal(dbus_constants::kSessionStateStarted);
}

TEST_F(SessionStateManagerTest, TestAddRemoveObserver) {
  manager_->AddObserver(&observer_);

  // Prepare response with information about primary user.
  std::unique_ptr<dbus::Response> response =
      RetrievePrimarySessionResponse(kUsername, kSanitizedUsername);
  std::unique_ptr<dbus::Response> response2 =
      RetrievePrimarySessionResponse(kUsername, kSanitizedUsername);

  // After SessionStateChange signal we expect OnUserLoggedIn to be called.
  EXPECT_CALL(
      *proxy_,
      CallMethodAndBlock(
          IsMember(login_manager::kSessionManagerRetrievePrimarySession),
          A<int>()))
      .WillOnce(Return(ByMove(base::ok(std::move(response)))))
      .WillOnce(Return(ByMove(base::ok(std::move(response2)))));
  EXPECT_CALL(observer_, OnUserLoggedIn).Times(1);
  manager_->RefreshPrimaryUser();

  // Remove observer.
  manager_->RemoveObserver(&observer_);
  // After SessionStateChange signal we expect that OnUserLoggedIn observer
  // method won't be called..
  EXPECT_CALL(observer_, OnUserLoggedIn).Times(0);
  manager_->RefreshPrimaryUser();
}

TEST_F(SessionStateManagerTest, TestOnNameOwnerChangedNewOwnerEmpty) {
  manager_->AddObserver(&observer_);

  // Prepare response with information about primary user.
  std::unique_ptr<dbus::Response> response =
      RetrievePrimarySessionResponse(kUsername, kSanitizedUsername);

  // Load primary user.
  EXPECT_CALL(
      *proxy_,
      CallMethodAndBlock(
          IsMember(login_manager::kSessionManagerRetrievePrimarySession),
          A<int>()))
      .WillOnce(Return(ByMove(base::ok(std::move(response)))));
  manager_->RefreshPrimaryUser();
  EXPECT_EQ(manager_->GetPrimaryUser(), kSanitizedUsername);

  // Expect that OnUserLoggedOut will be called when new name owner is empty.
  EXPECT_CALL(observer_, OnUserLoggedOut).Times(1);

  // Inform session manager that new owner is empty.
  const auto& old_owner = kExampleConnectionName;
  const auto& new_owner = "";
  on_name_owner_changed_.Run(old_owner, new_owner);
  EXPECT_TRUE(manager_->GetPrimaryUser().empty());
}

TEST_F(SessionStateManagerTest, TestOnNameOwnerChangedNewOwnerEmptyNoUser) {
  manager_->AddObserver(&observer_);

  // Expect that neither OnUserLoggedOut nor OnUserLoggedIn will be called when
  // new name owner is empty but user is not logged in.
  EXPECT_CALL(observer_, OnUserLoggedOut).Times(0);
  EXPECT_CALL(observer_, OnUserLoggedIn).Times(0);

  // Inform session manager that new owner is empty.
  const auto& old_owner = kExampleConnectionName;
  const auto& new_owner = "";
  on_name_owner_changed_.Run(old_owner, new_owner);
}

TEST_F(SessionStateManagerTest, TestOnNameOwnerChangedNewOwnerNotEmpty) {
  manager_->AddObserver(&observer_);

  // Expect that neither OnUserLoggedOut nor OnUserLoggedIn will be called when
  // new name owner is not empty.
  EXPECT_CALL(observer_, OnUserLoggedOut).Times(0);
  EXPECT_CALL(observer_, OnUserLoggedIn).Times(0);

  // Inform session manager that name has new owner.
  const auto& old_owner = "";
  const auto& new_owner = kExampleConnectionName;
  on_name_owner_changed_.Run(old_owner, new_owner);
}

// This test validates that the PowerManager SuspendDone signal will trigger
// a OnSessionResumedFromHibernate when it's deepest state was "ToDisk".
TEST_F(SessionStateManagerTest, TestPowerManagerSuspendDoneToDisk) {
  manager_->AddObserver(&observer_);

  EXPECT_CALL(observer_, OnSessionResumedFromHibernate).Times(1);

  power_manager::SuspendDone signal;
  signal.set_deepest_state(power_manager::SuspendDone_SuspendState_TO_DISK);

  std::vector<uint8_t> msg(signal.ByteSizeLong());
  signal.SerializeToArray(&msg[0], msg.size());

  EmitSuspendDone(msg);
}

// This test validates that a SuspendDone where the deepest state is to RAM does
// not trigger OnSessionResumedFromHibernate.
TEST_F(SessionStateManagerTest, TestPowerManagerSuspendDoneToRam) {
  manager_->AddObserver(&observer_);

  // We suspend to ram only so we will not call OnSessionResumedFromHibernate.
  EXPECT_CALL(observer_, OnSessionResumedFromHibernate).Times(0);

  power_manager::SuspendDone signal;
  signal.set_deepest_state(power_manager::SuspendDone_SuspendState_TO_RAM);

  std::vector<uint8_t> msg(signal.ByteSizeLong());
  signal.SerializeToArray(&msg[0], msg.size());

  EmitSuspendDone(msg);
}

}  // namespace
}  // namespace biod
