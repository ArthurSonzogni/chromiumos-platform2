// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BPF_MONS_INCLUDE_MEMMON_TRACING_H_
#define BPF_MONS_INCLUDE_MEMMON_TRACING_H_

#include <perfetto/perfetto.h>

constexpr char __memmon_kcat[] = "memmon";

PERFETTO_DEFINE_CATEGORIES(
    perfetto::Category(__memmon_kcat).SetDescription("Events from memmon"));

struct memmon_event;

#define MEMMON_EVENT(event, ...) \
  TRACE_EVENT(__memmon_kcat, event, ##__VA_ARGS__)
#define MEMMON_EVENT_BEGIN(event, track, ...) \
  TRACE_EVENT_BEGIN(__memmon_kcat, event, track, ##__VA_ARGS__)
#define MEMMON_EVENT_END(track, ...) \
  TRACE_EVENT_END(__memmon_kcat, track, ##__VA_ARGS__)

perfetto::StaticString memmon_event_name(struct memmon_event* event);
perfetto::Track memmon_event_track(struct memmon_event* event);
void memmon_tracing_init();

#endif  // BPF_MONS_INCLUDE_MEMMON_TRACING_H_
