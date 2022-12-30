/*
 * Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros-camera/tracing.h"

#include <brillo/tracing.h>

PERFETTO_TRACK_EVENT_STATIC_STORAGE_IN_NAMESPACE_WITH_ATTRS(cros,
                                                            CROS_CAMERA_EXPORT);

namespace cros {

void InitializeCameraTrace() {
  brillo::InitPerfettoTracing();
  TrackEvent::Register();
}

}  // namespace cros
