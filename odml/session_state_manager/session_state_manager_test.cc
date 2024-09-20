// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/session_state_manager/session_state_manager.h"

#include <base/memory/ptr_util.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <session_manager/dbus-proxies.h>
#include <session_manager/dbus-proxy-mocks.h>

using ::testing::_;
using ::testing::DoAll;
using ::testing::InSequence;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::SetArgPointee;

namespace odml {

class MockSessionStateObserver : public SessionStateManagerInterface::Observer {
 public:
  MOCK_METHOD(void,
              OnUserLoggedIn,
              (const SessionStateManagerInterface::User&),
              (override));
  MOCK_METHOD(void, OnUserLoggedOut, (), (override));
};

class SessionStateManagerTest : public ::testing::Test {
 public:
  void SetUp() override {
    auto proxy =
        std::make_unique<org::chromium::SessionManagerInterfaceProxyMock>();
    mock_session_manager_proxy_ = proxy.get();
    EXPECT_CALL(*mock_session_manager_proxy_,
                DoRegisterSessionStateChangedSignalHandler(_, _))
        .WillOnce(SaveArg<0>(&session_state_changed_callback_));

    // The ownership of |proxy| is transferred to |session_state_manager_|.
    session_state_manager_ =
        std::make_unique<SessionStateManager>(std::move(proxy));
    session_state_manager_->AddObserver(&observer_);
  }

  void SimulateSessionStateChanged(const std::string& state) {
    session_state_changed_callback_.Run(state);
  }

 protected:
  std::unique_ptr<SessionStateManager> session_state_manager_;
  org::chromium::SessionManagerInterfaceProxyMock* mock_session_manager_proxy_;
  base::RepeatingCallback<void(const std::string&)>
      session_state_changed_callback_;
  MockSessionStateObserver observer_;
};

TEST_F(SessionStateManagerTest, Success) {
  SessionStateManagerInterface::User user{
      .name = "user_name",
      .hash = "sanitized_user_name",
  };
  EXPECT_CALL(*mock_session_manager_proxy_, RetrievePrimarySession(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<0>(user.name), SetArgPointee<1>(user.hash),
                      Return(true)));

  {
    InSequence s;
    EXPECT_CALL(observer_, OnUserLoggedIn(user));
    EXPECT_CALL(observer_, OnUserLoggedOut());
  }

  SimulateSessionStateChanged("started");
  SimulateSessionStateChanged("stopped");
}

TEST_F(SessionStateManagerTest, MultipleLogins) {
  SessionStateManagerInterface::User user_1{
      .name = "user_name_1",
      .hash = "sanitized_user_name_1",
  };
  SessionStateManagerInterface::User user_2{
      .name = "user_name_2",
      .hash = "sanitized_user_name_2",
  };
  EXPECT_CALL(*mock_session_manager_proxy_, RetrievePrimarySession(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<0>(user_1.name),
                      SetArgPointee<1>(user_1.hash), Return(true)))
      .WillOnce(DoAll(SetArgPointee<0>(user_2.name),
                      SetArgPointee<1>(user_2.hash), Return(true)));

  {
    InSequence s;
    EXPECT_CALL(observer_, OnUserLoggedIn(user_1)).Times(1);
    EXPECT_CALL(observer_, OnUserLoggedOut()).Times(1);
    EXPECT_CALL(observer_, OnUserLoggedIn(user_2)).Times(1);
    EXPECT_CALL(observer_, OnUserLoggedOut()).Times(1);
  }

  SimulateSessionStateChanged("started");
  SimulateSessionStateChanged("stopped");
  SimulateSessionStateChanged("started");
  SimulateSessionStateChanged("stopped");
}

TEST_F(SessionStateManagerTest, PrimaryUserNotChanged) {
  SessionStateManagerInterface::User user{
      .name = "user_name",
      .hash = "sanitized_user_name",
  };
  EXPECT_CALL(*mock_session_manager_proxy_, RetrievePrimarySession(_, _, _, _))
      .Times(3)
      .WillRepeatedly(DoAll(SetArgPointee<0>(user.name),
                            SetArgPointee<1>(user.hash), Return(true)));

  // Only called once.
  EXPECT_CALL(observer_, OnUserLoggedIn(user)).Times(1);

  SimulateSessionStateChanged("started");
  SimulateSessionStateChanged("started");
  SimulateSessionStateChanged("started");
}

TEST_F(SessionStateManagerTest, FailedToRetrievePrimaryUser) {
  SessionStateManagerInterface::User user{
      .name = "user_name",
      .hash = "sanitized_user_name",
  };
  EXPECT_CALL(*mock_session_manager_proxy_, RetrievePrimarySession(_, _, _, _))
      .Times(2)
      .WillRepeatedly(Return(false));

  // Not called.
  EXPECT_CALL(observer_, OnUserLoggedIn(_)).Times(0);

  SimulateSessionStateChanged("started");
  SimulateSessionStateChanged("started");
}

TEST_F(SessionStateManagerTest, NoLoggedInUserWhenLoggedOut) {
  EXPECT_CALL(*mock_session_manager_proxy_, RetrievePrimarySession(_, _, _, _))
      .Times(0);

  // Not called.
  EXPECT_CALL(observer_, OnUserLoggedOut()).Times(0);

  SimulateSessionStateChanged("stopped");
}

TEST_F(SessionStateManagerTest, RefreshPrimaryUser) {
  SessionStateManagerInterface::User user{
      .name = "user_name",
      .hash = "sanitized_user_name",
  };
  EXPECT_CALL(*mock_session_manager_proxy_, RetrievePrimarySession(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<0>(user.name), SetArgPointee<1>(user.hash),
                      Return(true)));

  EXPECT_CALL(observer_, OnUserLoggedIn(user)).Times(1);

  session_state_manager_->RefreshPrimaryUser();
}

TEST_F(SessionStateManagerTest, RefreshPrimaryUserPrimaryUserAlreadyExists) {
  SessionStateManagerInterface::User user{
      .name = "user_name",
      .hash = "sanitized_user_name",
  };
  EXPECT_CALL(*mock_session_manager_proxy_, RetrievePrimarySession(_, _, _, _))
      .Times(2)
      .WillRepeatedly(DoAll(SetArgPointee<0>(user.name),
                            SetArgPointee<1>(user.hash), Return(true)));

  // Called once only. When RefreshPrimaryUser() is called this is not called
  // the second time since primary user already exists.
  EXPECT_CALL(observer_, OnUserLoggedIn(user)).Times(1);

  SimulateSessionStateChanged("started");
  session_state_manager_->RefreshPrimaryUser();
}

TEST_F(SessionStateManagerTest, RefreshPrimaryUserLoggedOut) {
  SessionStateManagerInterface::User user{
      .name = "user_name",
      .hash = "sanitized_user_name",
  };
  EXPECT_CALL(*mock_session_manager_proxy_, RetrievePrimarySession(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<0>(user.name), SetArgPointee<1>(user.hash),
                      Return(true)))
      .WillOnce(
          DoAll(SetArgPointee<0>(""), SetArgPointee<1>(""), Return(true)));

  EXPECT_CALL(observer_, OnUserLoggedOut()).Times(1);

  SimulateSessionStateChanged("started");
  session_state_manager_->RefreshPrimaryUser();
}

TEST_F(SessionStateManagerTest, RefreshPrimaryNotLoggedInToLoggedIn) {
  SessionStateManagerInterface::User user{
      .name = "user_name",
      .hash = "sanitized_user_name",
  };

  EXPECT_CALL(*mock_session_manager_proxy_, RetrievePrimarySession(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<0>(""), SetArgPointee<1>(""), Return(true)))
      .WillOnce(DoAll(SetArgPointee<0>(user.name), SetArgPointee<1>(user.hash),
                      Return(true)));

  EXPECT_CALL(observer_, OnUserLoggedIn(user)).Times(1);

  session_state_manager_->RefreshPrimaryUser();
  session_state_manager_->RefreshPrimaryUser();
}

}  // namespace odml
