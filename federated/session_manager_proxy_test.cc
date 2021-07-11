// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "federated/session_manager_proxy.h"

#include <memory>
#include <stdlib.h>
#include <string>
#include <time.h>
#include <utility>
#include <vector>

#include <base/callback.h>
#include <base/check.h>
#include <base/memory/ptr_util.h>
#include <base/memory/ref_counted.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <session_manager/dbus-proxy-mocks.h>

#include "federated/utils.h"

using ::testing::_;
using ::testing::Invoke;
using ::testing::Mock;
using ::testing::SaveArg;
using ::testing::StrictMock;

namespace federated {
namespace {

class MockSessionManagerObserver : public SessionManagerObserverInterface {
 public:
  ~MockSessionManagerObserver() = default;

  MOCK_METHOD(void, OnSessionStarted, (), (override));
  MOCK_METHOD(void, OnSessionStopped, (), (override));
};

}  // namespace

class SessionManagerProxyTest : public ::testing::Test {
 public:
  SessionManagerProxyTest()
      : mock_session_manager_interface_proxy_(
            new StrictMock<org::chromium::SessionManagerInterfaceProxyMock>()) {
  }

  SessionManagerProxyTest(const SessionManagerProxyTest&) = delete;
  SessionManagerProxyTest& operator=(const SessionManagerProxyTest&) = delete;

  void SetUp() override {
    EXPECT_CALL(*mock_session_manager_interface_proxy_,
                DoRegisterSessionStateChangedSignalHandler(_, _))
        .WillOnce(SaveArg<0>(&session_state_changed_callback_));

    session_manager_proxy_ = std::make_unique<SessionManagerProxy>(
        base::WrapUnique<org::chromium::SessionManagerInterfaceProxyInterface>(
            mock_session_manager_interface_proxy_));
  }

  void TearDown() override {
    session_manager_proxy_.reset();
    Mock::VerifyAndClearExpectations(mock_session_manager_interface_proxy_);
  }

  // Mocks the session state change signals
  void InvokeSessionStateChange(const std::string& session_state) {
    session_state_changed_callback_.Run(session_state);
  }

  SessionManagerProxy* session_manager_proxy() const {
    DCHECK(session_manager_proxy_);
    return session_manager_proxy_.get();
  }

  // Sets the primary session and the EXPECT_CALL of RetrievePrimarySession.
  void SetPrimarySession(const std::string& username,
                         const std::string& sanitized_username) {
    primary_session_ = {username, sanitized_username};

    EXPECT_CALL(*mock_session_manager_interface_proxy_,
                RetrievePrimarySession(_, _, _, _))
        .Times(1)
        .WillOnce(
            Invoke(this, &SessionManagerProxyTest::RetrievePrimarySessionImpl));
  }

  // Sets the session state and the EXPECT_CALL of RetrieveSessionState.
  void SetSessionState(const std::string& state) {
    session_state_ = state;

    EXPECT_CALL(*mock_session_manager_interface_proxy_,
                RetrieveSessionState(_, _, _))
        .Times(1)
        .WillOnce(
            Invoke(this, &SessionManagerProxyTest::RetrieveSessionStateImpl));
  }

 private:
  // Invoked when SessionManagerInterfaceProxyMock::RetrievePrimarySession() is
  // called.
  bool RetrievePrimarySessionImpl(
      std::string* username,
      std::string* sanitized_username,
      brillo::ErrorPtr* error,
      int timeout_ms = dbus::ObjectProxy::TIMEOUT_USE_DEFAULT) {
    *username = primary_session_.first;  // Set in SetPrimarySession().
    *sanitized_username = primary_session_.second;

    return true;
  }

  // Invoked when SessionManagerInterfaceProxyMock::RetrieveSessionState() is
  // called.
  bool RetrieveSessionStateImpl(
      std::string* state,
      brillo::ErrorPtr* error,
      int timeout_ms = dbus::ObjectProxy::TIMEOUT_USE_DEFAULT) {
    *state = session_state_;  // Set in SetSessionState().
    return true;
  }

  // Primary session consists of username and sanitized_username.
  std::pair<std::string, std::string> primary_session_;
  std::string session_state_;

  org::chromium::SessionManagerInterfaceProxyMock*
      mock_session_manager_interface_proxy_;

  std::unique_ptr<SessionManagerProxy> session_manager_proxy_;

  base::RepeatingCallback<void(const std::string& state)>
      session_state_changed_callback_;
};

// Tests that GetSanitizedUsername can get the user_hash of current primary
// session.
TEST_F(SessionManagerProxyTest, GetSanitizedUsername) {
  SetPrimarySession("user1", "hash1");
  EXPECT_EQ(session_manager_proxy()->GetSanitizedUsername(), "hash1");

  SetPrimarySession("user2", "hash2");
  EXPECT_EQ(session_manager_proxy()->GetSanitizedUsername(), "hash2");
}

// Tests that RetrieveSessionState works. RetrieveSessionState can get whatever
// session state, although only kSessionStartedState and kSessionStoppedState
// are concerned.
TEST_F(SessionManagerProxyTest, RetrieveSessionState) {
  SetSessionState(kSessionStartedState);
  EXPECT_EQ(session_manager_proxy()->RetrieveSessionState(),
            kSessionStartedState);

  SetSessionState(kSessionStoppedState);
  EXPECT_EQ(session_manager_proxy()->RetrieveSessionState(),
            kSessionStoppedState);

  SetSessionState("unknown_state");
  EXPECT_EQ(session_manager_proxy()->RetrieveSessionState(), "unknown_state");
}

// Tests that session_manager_proxy can invoke observers when session state
// changes.
TEST_F(SessionManagerProxyTest, OnSessionStateChanged) {
  StrictMock<MockSessionManagerObserver> mock_observer;
  session_manager_proxy()->AddObserver(&mock_observer);

  // Generates a random state array, records the counts of started and stopped
  // state.
  std::vector<std::string> state_vector;
  int started_state_count = 0;
  int stopped_state_count = 0;

  const std::vector<std::string> available_states = {
      kSessionStartedState, kSessionStoppedState, "unknown_state"};
  static unsigned int seed = time(NULL) + getpid();
  for (size_t i = 0; i < 100; i++) {
    int index = rand_r(&seed) % 3;
    if (index == 0)
      started_state_count++;
    else if (index == 1)
      stopped_state_count++;

    state_vector.push_back(available_states[index]);
  }

  // Each time OnSessionStateChanged with state = kSessionStartedState,
  // observer's OnSessionStarted is invoked.
  EXPECT_CALL(mock_observer, OnSessionStarted()).Times(started_state_count);
  // Each time OnSessionStateChanged with state = kSessionStoppedState,
  // observer's OnSessionStopped is invoked.
  EXPECT_CALL(mock_observer, OnSessionStopped()).Times(stopped_state_count);

  for (const auto& state : state_vector) {
    InvokeSessionStateChange(state);
  }
}

}  // namespace federated
