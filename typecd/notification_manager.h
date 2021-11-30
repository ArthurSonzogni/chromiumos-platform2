// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TYPECD_NOTIFICATION_MANAGER_H_
#define TYPECD_NOTIFICATION_MANAGER_H_

#include <brillo/daemons/dbus_daemon.h>
#include <dbus/typecd/dbus-constants.h>

#include "typecd/dbus_adaptors/org.chromium.typecd.h"

namespace typecd {

class NotificationManager : public org::chromium::typecdAdaptor,
                            public org::chromium::typecdInterface {
 public:
  explicit NotificationManager(brillo::dbus_utils::DBusObject* dbus_object);

  void NotifyConnected(DeviceConnectedType type);
  void NotifyCableWarning(CableWarningType type);
};

}  // namespace typecd

#endif  // TYPECD_NOTIFICATION_MANAGER_H_
