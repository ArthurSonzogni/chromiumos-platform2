// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/logging.h>
#include <dbus/dbus.h>
#include <perfetto/perfetto.h>

#include "dbus_perfetto_producer/dbus_tracer.h"

int main(int argc, char** argv) {
  perfetto::TracingInitArgs args;
  args.backends |= perfetto::kSystemBackend;
  perfetto::Tracing::Initialize(args);
  perfetto::TrackEvent::Register();

  if (!DbusTracer()) {
    exit(1);
  }
  return 0;
}
