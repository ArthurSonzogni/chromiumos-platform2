// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "brillo/tracing.h"

#include <base/trace_event/trace_log.h>
#include <perfetto/perfetto.h>

namespace brillo {

void InitPerfettoTracing() {
  if (!perfetto::Tracing::IsInitialized()) {
    perfetto::TracingInitArgs init_args;
    init_args.backends = perfetto::BackendType::kSystemBackend;
    perfetto::Tracing::Initialize(init_args);
  }
  base::TrackEvent::Register();
}

}  // namespace brillo
