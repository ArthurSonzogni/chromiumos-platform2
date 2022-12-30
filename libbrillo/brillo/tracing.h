// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBBRILLO_BRILLO_TRACING_H_
#define LIBBRILLO_BRILLO_TRACING_H_

#include <brillo/brillo_export.h>

namespace brillo {

// Enables Perfetto tracing by connecting to the system tracing service
// (traced). After this, track events (`TRACE_EVENT`) in the calling process
// will be recorded into future tracing sessions that have the track event data
// source enabled. Use https://ui.perfetto.dev to start tracing sessions and
// view recorded traces. Traces can also be recorded using the `perfetto`
// command line tool.
//
// By default, just built-in events from libchrome will be recorded. To add your
// own events:
//
// 1. Define tracing categories (in a header file):
//
//      #include <perfetto/perfetto.h>
//
//      PERFETTO_DEFINE_CATEGORIES_IN_NAMESPACE(
//        my_namespace,
//        perfetto::Category("foo")
//            .SetDescription("Events from the Foo subsystem"));
//
// 2. Initialize tracing and register your categories (in a .cc file):
//
//      PERFETTO_TRACK_EVENT_STATIC_STORAGE_IN_NAMESPACE(my_namespace);
//
//      void Init() {
//        brillo::InitPerfettoTracing();
//        my_namespace::TrackEvent::Register();
//      }
//
// 3. Instrument your code with events:
//
//      namespace my_namespace {
//
//      void FooOperation(int x, int y) {
//          TRACE_EVENT("foo", "FooOperation", "x", x, "y", y);
//          ...
//      }
//
//      }  // namespace my_namespace
//
// See https://perfetto.dev/docs/instrumentation/track-events for more.
//
BRILLO_EXPORT void InitPerfettoTracing();

}  // namespace brillo

#endif  // LIBBRILLO_BRILLO_TRACING_H_
