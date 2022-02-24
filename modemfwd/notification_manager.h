// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MODEMFWD_NOTIFICATION_MANAGER_H_
#define MODEMFWD_NOTIFICATION_MANAGER_H_

#include <string>

#include <brillo/daemons/dbus_daemon.h>

#include "modemfwd/dbus_adaptors/org.chromium.Modemfwd.h"

namespace modemfwd {

class NotificationManager {
 public:
  explicit NotificationManager(org::chromium::ModemfwdAdaptor* dbus_adaptor);
  virtual ~NotificationManager() = default;

  virtual void NotifyUpdateFirmwareCompletedSuccess();
  virtual void NotifyUpdateFirmwareCompletedFailure(const std::string& error);

 protected:
  NotificationManager() = default;

 private:
  // Owned by Daemon
  org::chromium::ModemfwdAdaptor* dbus_adaptor_;
};

}  // namespace modemfwd

#endif  // MODEMFWD_NOTIFICATION_MANAGER_H_
