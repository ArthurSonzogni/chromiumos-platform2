/*
 * Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_INCLUDE_CROS_CAMERA_TRACING_H_
#define CAMERA_INCLUDE_CROS_CAMERA_TRACING_H_

// Perfetto tracing support for cros-camera. See doc/tracing.md for
// documentation about tracing in the camera service.

#include "cros-camera/export.h"

// To export the Perfetto symbols (e.g. kCategoryRegistry).
#define PERFETTO_COMPONENT_EXPORT CROS_CAMERA_EXPORT

#define PERFETTO_TRACK_EVENT_NAMESPACE cros_camera

#include <perfetto/perfetto.h>

namespace cros {

// The camera trace categories.
constexpr char kHalAdapterTraceCategory[] = "hal_adapter";
constexpr char kHdrnetTraceCategory[] = "hdrnet";

// Common keys used to annotate camera trace events.
constexpr char kCameraTraceKeyFrameNumber[] = "frame_number";
constexpr char kCameraTraceKeyBufferId[] = "buffer_id";
constexpr char kCameraTraceKeyCameraId[] = "camera_id";
constexpr char kCameraTraceKeyStreamId[] = "stream_id";
constexpr char kCameraTraceKeyWidth[] = "width";
constexpr char kCameraTraceKeyHeight[] = "height";
constexpr char kCameraTraceKeyFormat[] = "format";

}  // namespace cros

PERFETTO_DEFINE_CATEGORIES(
    perfetto::Category(cros::kHalAdapterTraceCategory)
        .SetDescription("Events from CrOS Camera HAL adapter"));

#endif  // CAMERA_INCLUDE_CROS_CAMERA_TRACING_H_
