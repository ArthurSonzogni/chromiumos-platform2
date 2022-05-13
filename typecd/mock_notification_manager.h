// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TYPECD_MOCK_NOTIFICATION_MANAGER_H_
#define TYPECD_MOCK_NOTIFICATION_MANAGER_H_

#include <string>

#include <gmock/gmock.h>

#include "typecd/notification_manager.h"

namespace typecd {

// A mock implementation of the NotificationManager class used in PortManager
// testing.
class MockNotificationManager : public NotificationManager {
 public:
  explicit MockNotificationManager(brillo::dbus_utils::DBusObject* dbus_object)
      : NotificationManager(dbus_object) {}

  MOCK_METHOD(void, NotifyConnected, (DeviceConnectedType), ());
  MOCK_METHOD(void, NotifyCableWarning, (CableWarningType), ());
};

}  // namespace typecd

#endif  // TYPECD_MOCK_NOTIFICATION_MANAGER_H_
