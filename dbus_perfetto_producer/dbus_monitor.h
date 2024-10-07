// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DBUS_PERFETTO_PRODUCER_DBUS_MONITOR_H_
#define DBUS_PERFETTO_PRODUCER_DBUS_MONITOR_H_

#include <dbus/dbus.h>

namespace dbus_perfetto_producer {

bool SetupConnection(DBusConnection*, DBusError*, int);

}  // namespace dbus_perfetto_producer

#endif  // DBUS_PERFETTO_PRODUCER_DBUS_MONITOR_H_
