// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SECAGENTD_TEST_MOCK_DEVICE_USER_H_
#define SECAGENTD_TEST_MOCK_DEVICE_USER_H_

#include <list>
#include <string>

#include "gmock/gmock.h"
#include "secagentd/device_user.h"

namespace secagentd::testing {

class MockDeviceUser : public DeviceUserInterface {
 public:
  MOCK_METHOD(void, RegisterSessionChangeHandler, (), (override));
  MOCK_METHOD(void,
              RegisterScreenLockedHandler,
              (base::RepeatingClosure signal_callback,
               dbus::ObjectProxy::OnConnectedCallback on_connected_callback),
              (override));
  MOCK_METHOD(void,
              RegisterScreenUnlockedHandler,
              (base::RepeatingClosure signal_callback,
               dbus::ObjectProxy::OnConnectedCallback on_connected_callback),
              (override));
  MOCK_METHOD(void,
              RegisterSessionChangeListener,
              (base::RepeatingCallback<void(const std::string&)> cb),
              (override));
  MOCK_METHOD(std::string, GetDeviceUser, (), (override));
  MOCK_METHOD((std::list<std::string>),
              GetUsernamesForRedaction,
              (),
              (override));
};
}  // namespace secagentd::testing

#endif  // SECAGENTD_TEST_MOCK_DEVICE_USER_H_
