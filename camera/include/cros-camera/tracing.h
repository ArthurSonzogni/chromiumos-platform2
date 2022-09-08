/*
 * Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

// Perfetto tracing support for cros-camera. See doc/tracing.md for
// documentation about tracing in the camera service.

#ifndef CAMERA_INCLUDE_CROS_CAMERA_TRACING_H_
#define CAMERA_INCLUDE_CROS_CAMERA_TRACING_H_

#include <string>

#include "cros-camera/export.h"

// To export the Perfetto symbols (e.g. kCategoryRegistry).
#define PERFETTO_COMPONENT_EXPORT CROS_CAMERA_EXPORT

#define PERFETTO_TRACK_EVENT_NAMESPACE cros_camera

#include <perfetto/perfetto.h>

namespace cros {

// The camera trace categories.
constexpr char kCameraTraceCategoryCommon[] = "camera.common";
constexpr char kCameraTraceCategoryGcamAe[] = "camera.gcam_ae";
constexpr char kCameraTraceCategoryGpu[] = "camera.gpu";
constexpr char kCameraTraceCategoryHalAdapter[] = "camera.hal_adapter";
constexpr char kCameraTraceCategoryHdrnet[] = "camera.hdrnet";

// Common keys used to annotate camera trace events.
constexpr char kCameraTraceKeyFrameNumber[] = "frame_number";
constexpr char kCameraTraceKeyBufferId[] = "buffer_id";
constexpr char kCameraTraceKeyCameraId[] = "camera_id";
constexpr char kCameraTraceKeyStreamId[] = "stream_id";
constexpr char kCameraTraceKeyWidth[] = "width";
constexpr char kCameraTraceKeyHeight[] = "height";
constexpr char kCameraTraceKeyFormat[] = "format";

constexpr char kCameraTraceKeyStreamConfigurations[] = "stream_configurations";
constexpr char kCameraTraceKeyCaptureInfo[] = "capture_info";
constexpr char kCameraTraceKeyCaptureType[] = "capture_type";
constexpr char kCameraTraceKeyPartialResult[] = "partial_result";
constexpr char kCameraTraceKeyInputBuffer[] = "input_buffer";
constexpr char kCameraTraceKeyOutputBuffers[] = "output_buffers";

}  // namespace cros

constexpr std::string_view TraceCameraEventName(const char* pretty_function) {
  std::string_view sv(pretty_function);
  size_t paren = sv.rfind('(');
  size_t space = sv.rfind(' ', paren) + 1;
  auto name = sv.substr(space, paren - space);
  if (name.rfind("cros::", 0) == 0) {
    name = name.substr(6);
  }
  return name;
}
#define TRACE_CAMERA_EVENT_NAME TraceCameraEventName(__PRETTY_FUNCTION__)

#define TRACE_EVENT_AUTOGEN(category, ...)                                \
  static const std::string event_##__LINE__(TRACE_CAMERA_EVENT_NAME);     \
  TRACE_EVENT(category, perfetto::StaticString(event_##__LINE__.c_str()), \
              ##__VA_ARGS__)

PERFETTO_DEFINE_CATEGORIES(
    perfetto::Category(cros::kCameraTraceCategoryCommon)
        .SetDescription("Events from common CrOS Camera library"),
    perfetto::Category(cros::kCameraTraceCategoryGcamAe)
        .SetDescription("Events from CrOS Gcam AE pipeline"),
    perfetto::Category(cros::kCameraTraceCategoryGpu)
        .SetDescription("Events from CrOS Camera GPU operations"),
    perfetto::Category(cros::kCameraTraceCategoryHalAdapter)
        .SetDescription("Events from CrOS Camera HAL adapter"),
    perfetto::Category(cros::kCameraTraceCategoryHdrnet)
        .SetDescription("Events from CrOS HDRnet pipeline"));

#endif  // CAMERA_INCLUDE_CROS_CAMERA_TRACING_H_
