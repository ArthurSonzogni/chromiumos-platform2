// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/shared_dbus_connection.h"

#include <dbus-c++/glib-integration.h>
#include <dbus-c++/util.h>

namespace shill {

namespace {
base::LazyInstance<SharedDBusConnection> g_shared_dbus_connection =
    LAZY_INSTANCE_INITIALIZER;
}  // namespace

SharedDBusConnection *SharedDBusConnection::GetInstance() {
  return g_shared_dbus_connection.Pointer();
}

void SharedDBusConnection::Init() {
  dispatcher_.reset(new(std::nothrow) DBus::Glib::BusDispatcher());
  CHECK(dispatcher_.get()) << "Failed to create a dbus-dispatcher";
  DBus::default_dispatcher = dispatcher_.get();
  dispatcher_->attach(nullptr);
  connection_.reset(new DBus::Connection(DBus::Connection::SystemBus()));
}

DBus::Connection *SharedDBusConnection::GetConnection() {
  CHECK(connection_.get());
  return connection_.get();
}

}  // namespace shill
