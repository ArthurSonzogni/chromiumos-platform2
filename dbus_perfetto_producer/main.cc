// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/logging.h>
#include <dbus/dbus.h>
#include <perfetto/perfetto.h>

#include "dbus_perfetto_producer/dbus_request.h"
#include "dbus_perfetto_producer/dbus_tracer.h"

int main(int argc, char** argv) {
  DBusConnection* connection;
  DBusError error;
  DBusBusType type = DBUS_BUS_SYSTEM;

  // user data to be passed to a filter function
  Maps maps;

  dbus_error_init(&error);
  connection = dbus_bus_get(type, &error);
  if (!connection) {
    LOG(ERROR) << "Failed to open a connection";
    dbus_error_free(&error);
    exit(1);
  }

  if (!StoreProcessesNames(connection, &error, maps)) {
    exit(1);
  }

  perfetto::TracingInitArgs args;
  args.backends |= perfetto::kSystemBackend;
  perfetto::Tracing::Initialize(args);
  perfetto::TrackEvent::Register();

  if (!DbusTracer(connection, &error, maps)) {
    exit(1);
  }
  return 0;
}
