// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "typecd/notification_manager.h"

#include <string>

#include <base/logging.h>

namespace typecd {

NotificationManager::NotificationManager(
    brillo::dbus_utils::DBusObject* dbus_object)
    : org::chromium::typecdAdaptor(this) {
  RegisterWithDBusObject(dbus_object);
}

void NotificationManager::NotifyConnected(DeviceConnectedType type) {
  SendDeviceConnectedSignal(static_cast<uint32_t>(type));
}

void NotificationManager::NotifyCableWarning(CableWarningType type) {
  SendCableWarningSignal(static_cast<uint32_t>(type));
}

}  // namespace typecd
