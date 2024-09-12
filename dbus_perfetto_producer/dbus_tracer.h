// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DBUS_PERFETTO_PRODUCER_DBUS_TRACER_H_
#define DBUS_PERFETTO_PRODUCER_DBUS_TRACER_H_

#include <dbus/dbus.h>
#include <perfetto/perfetto.h>

#include "dbus_perfetto_producer/dbus_request.h"

#define DBUS_PERFETTO_PRODUCER_PERFETTO_CATEGORY "dbus_perfetto_producer"

PERFETTO_DEFINE_CATEGORIES(
    perfetto::Category(DBUS_PERFETTO_PRODUCER_PERFETTO_CATEGORY)
        .SetDescription("D-Bus Event"));

bool DbusTracer(DBusConnection*, DBusError*, Maps&);

#endif  // DBUS_PERFETTO_PRODUCER_DBUS_TRACER_H_
