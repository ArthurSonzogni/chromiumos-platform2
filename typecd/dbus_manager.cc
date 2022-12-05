// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "typecd/dbus_manager.h"

#include <string>

#include <base/logging.h>

namespace typecd {

DBusManager::DBusManager(brillo::dbus_utils::DBusObject* dbus_object)
    : org::chromium::typecdAdaptor(this) {
  RegisterWithDBusObject(dbus_object);
}

void DBusManager::NotifyConnected(DeviceConnectedType type) {
  SendDeviceConnectedSignal(static_cast<uint32_t>(type));
}

void DBusManager::NotifyCableWarning(CableWarningType type) {
  SendCableWarningSignal(static_cast<uint32_t>(type));
}

bool DBusManager::SetPeripheralDataAccess(brillo::ErrorPtr* err, bool enabled) {
  if (!features_client_) {
    LOG(ERROR) << "Unable to call SetPeripheralDataAccessEnabled";
    brillo::Error::AddTo(err, FROM_HERE, "Typecd", "no_features_client",
                         "Typecd DBusManager failed features_client_ check");
    return false;
  }

  features_client_->SetPeripheralDataAccessEnabled(enabled);
  return true;
}

}  // namespace typecd
