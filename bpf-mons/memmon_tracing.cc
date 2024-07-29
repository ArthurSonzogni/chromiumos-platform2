// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/memmon_tracing.h"

#include <stdint.h>
// This include should follow stdint.h include
#include "include/memmon.h"

PERFETTO_TRACK_EVENT_STATIC_STORAGE();

perfetto::StaticString memmon_event_name(struct memmon_event* event) {
  switch (event->type) {
    case MEMMON_EVENT_MALLOC:
      return "malloc";
    case MEMMON_EVENT_FREE:
      return "free";
    case MEMMON_EVENT_MMAP:
      return "mmap";
    case MEMMON_EVENT_MUNMAP:
      return "munmap";
    case MEMMON_EVENT_CALLOC:
      return "calloc";
    case MEMMON_EVENT_MEMALIGN:
      return "memalign";
    case MEMMON_EVENT_PF:
      return "mm_fault";
    case MEMMON_EVENT_INVALID:
      return "INVALID";
    default:
      return "ERROR";
  }
}

perfetto::Track memmon_event_track(struct memmon_event* event) {
  return perfetto::Track(event->ptr);
}

void memmon_tracing_init() {
  if (!perfetto::Tracing::IsInitialized()) {
    perfetto::TracingInitArgs init_args;
    init_args.backends = perfetto::BackendType::kInProcessBackend;
    init_args.backends |= perfetto::BackendType::kSystemBackend;
    perfetto::Tracing::Initialize(init_args);
  }

  perfetto::TrackEvent::Register();
}
