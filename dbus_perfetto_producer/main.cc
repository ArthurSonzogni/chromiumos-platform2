// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/logging.h>
#include <base/threading/platform_thread.h>
#include <base/threading/thread.h>
#include <base/time/time.h>

#include "dbus_perfetto_producer/dbus_tracing_categories.h"

int main(int argc, char** argv) {
  perfetto::TracingInitArgs args;

  // The system backend writes events into a system Perfetto daemon. Requires
  // the Perfetto `traced` daemon to be running (e.g., on Android Pie and
  // newer).
  args.backends |= perfetto::kSystemBackend;

  perfetto::Tracing::Initialize(args);
  perfetto::TrackEvent::Register();

  return 0;
}
