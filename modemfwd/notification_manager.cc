// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modemfwd/notification_manager.h"

#include <string>

#include <base/logging.h>

namespace modemfwd {

NotificationManager::NotificationManager(
    org::chromium::ModemfwdAdaptor* dbus_adaptor)
    : dbus_adaptor_(dbus_adaptor) {}

void NotificationManager::NotifyUpdateFirmwareCompletedSuccess() {
  dbus_adaptor_->SendUpdateFirmwareCompletedSignal(true, "");
}

void NotificationManager::NotifyUpdateFirmwareCompletedFailure(
    const std::string& error) {
  dbus_adaptor_->SendUpdateFirmwareCompletedSignal(false, error);
}

}  // namespace modemfwd
