// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bootsplash/session_manager_client.h"

#include <chromeos/dbus/service_constants.h>
#include <dbus/bus.h>
#include <dbus/message.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_object_proxy.h>
#include <dbus/object_path.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "bootsplash/session_event_observer.h"

namespace bootsplash {

class SessionManagerClientTest : public testing::Test,
                                 public SessionEventObserver {
 public:
  SessionManagerClientTest() {
    dbus::Bus::Options options;
    options.bus_type = dbus::Bus::SYSTEM;
    mock_bus_ = base::MakeRefCounted<dbus::MockBus>(options);

    session_manager_proxy_ = base::MakeRefCounted<dbus::MockObjectProxy>(
        mock_bus_.get(), login_manager::kSessionManagerServiceName,
        dbus::ObjectPath(login_manager::kSessionManagerServicePath));

    // Set an expectation so that the MockBus will return our mock session
    // manager proxy.
    EXPECT_CALL(*mock_bus_.get(),
                GetObjectProxy(login_manager::kSessionManagerServiceName,
                               dbus::ObjectPath(
                                   login_manager::kSessionManagerServicePath)))
        .WillOnce(testing::Return(session_manager_proxy_.get()));

    EXPECT_CALL(*session_manager_proxy_,
                DoConnectToSignal(login_manager::kSessionManagerInterface,
                                  login_manager::kLoginPromptVisibleSignal,
                                  testing::_, testing::_))
        .WillOnce(testing::SaveArg<2>(&login_prompt_visible_callback_));
  }

  // SessionEventObserver implementation
  void SessionManagerLoginPromptVisibleEventReceived() override {
    login_prompt_visible_received_ = true;
  }

 protected:
  void SendLoginPromptVisibleSignal() {
    dbus::Signal login_prompt_visible_signal(
        login_manager::kSessionManagerInterface,
        login_manager::kLoginPromptVisibleSignal);
    login_prompt_visible_callback_.Run(&login_prompt_visible_signal);
  }

  scoped_refptr<dbus::MockBus> mock_bus_;
  scoped_refptr<dbus::MockObjectProxy> session_manager_proxy_;
  dbus::ObjectProxy::SignalCallback login_prompt_visible_callback_;
  bool login_prompt_visible_received_ = false;
};

// Ensure that SessionEventObservers are notified on login prompt visible event.
TEST_F(SessionManagerClientTest, LoginPromptVisibleEvent) {
  std::unique_ptr<SessionManagerClientInterface> client =
      SessionManagerClient::Create(mock_bus_);

  client->AddObserver(this);
  SendLoginPromptVisibleSignal();
  ASSERT_TRUE(login_prompt_visible_received_);
}

}  // namespace bootsplash
