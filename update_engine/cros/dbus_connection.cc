// Copyright 2016 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/cros/dbus_connection.h"

#include <base/logging.h>
#include <base/time/time.h>

namespace chromeos_update_engine {

namespace {
constexpr base::TimeDelta kDBusSystemMaxWait = base::Minutes(2);

DBusConnection* dbus_connection_singleton = nullptr;
}  // namespace

DBusConnection::DBusConnection() {
  // We wait for the D-Bus connection for up two minutes to avoid re-spawning
  // the daemon too fast causing thrashing if dbus-daemon is not running.
  bus_ = dbus_connection_.ConnectWithTimeout(kDBusSystemMaxWait);

  if (!bus_) {
    // TODO(deymo): Make it possible to run update_engine even if dbus-daemon
    // is not running or constantly crashing.
    LOG(FATAL) << "Failed to initialize DBus, aborting.";
  }

  CHECK(bus_->SetUpAsyncOperations());
}

const scoped_refptr<dbus::Bus>& DBusConnection::GetDBus() {
  CHECK(bus_);
  return bus_;
}

DBusConnection* DBusConnection::Get() {
  if (!dbus_connection_singleton)
    dbus_connection_singleton = new DBusConnection();
  return dbus_connection_singleton;
}

}  // namespace chromeos_update_engine
