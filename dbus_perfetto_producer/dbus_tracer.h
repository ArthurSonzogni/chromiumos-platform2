// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DBUS_PERFETTO_PRODUCER_DBUS_TRACER_H_
#define DBUS_PERFETTO_PRODUCER_DBUS_TRACER_H_

#include <perfetto/perfetto.h>

#define DBUS_PERFETTO_PRODUCER_PERFETTO_CATEGORY "dbus_perfetto_producer"

PERFETTO_DEFINE_CATEGORIES(
    perfetto::Category(DBUS_PERFETTO_PRODUCER_PERFETTO_CATEGORY)
        .SetDescription("D-Bus Event"));

bool DbusTracer();

#endif  // DBUS_PERFETTO_PRODUCER_DBUS_TRACER_H_
