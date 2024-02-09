// Copyright 2016 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_CROS_DBUS_CONNECTION_H_
#define UPDATE_ENGINE_CROS_DBUS_CONNECTION_H_

#include <base/memory/ref_counted.h>
#include <brillo/dbus/dbus_connection.h>
#include <dbus/bus.h>

namespace chromeos_update_engine {

class DBusConnection {
 public:
  DBusConnection();
  DBusConnection(const DBusConnection&) = delete;
  DBusConnection& operator=(const DBusConnection&) = delete;

  const scoped_refptr<dbus::Bus>& GetDBus();

  static DBusConnection* Get();

 private:
  scoped_refptr<dbus::Bus> bus_;

  brillo::DBusConnection dbus_connection_;
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_CROS_DBUS_CONNECTION_H_
