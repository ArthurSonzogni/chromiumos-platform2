// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DBUS_PERFETTO_PRODUCER_DBUS_TRACING_CATEGORIES_H_
#define DBUS_PERFETTO_PRODUCER_DBUS_TRACING_CATEGORIES_H_

#include <perfetto/perfetto.h>

PERFETTO_DEFINE_CATEGORIES(
    perfetto::Category("dbus_perfetto_producer")
        .SetDescription("Events from dbus_perfetto_producer"));

#endif  // DBUS_PERFETTO_PRODUCER_DBUS_TRACING_CATEGORIES_H_
