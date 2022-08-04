/*
 * Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_HAL_ADAPTER_CAMERA_TRACE_EVENT_H_
#define CAMERA_HAL_ADAPTER_CAMERA_TRACE_EVENT_H_

#include <string>

#include <base/strings/stringprintf.h>

#include "cros-camera/tracing.h"

namespace cros {

enum class HalAdapterTraceEvent {
  kCapture,
};

#define TRACE_CAMERA_COMBINE_NAME(X, Y) X##Y
#define TRACE_NAME TRACE_CAMERA_COMBINE_NAME(trace_, __LINE__)

#define TRACE_CAMERA_SCOPED(...)                            \
  static const std::string trace_name =                     \
      base::StringPrintf("%s_L%d", __FUNCTION__, __LINE__); \
  TRACE_EVENT(kHalAdapterTraceCategory,                     \
              perfetto::StaticString(trace_name.c_str()), ##__VA_ARGS__);

#define TRACE_CAMERA_EVENT_BEGIN(event, track, ...) \
  TRACE_EVENT_BEGIN(kHalAdapterTraceCategory, event, track, ##__VA_ARGS__);

#define TRACE_CAMERA_EVENT_END(track) \
  TRACE_EVENT_END(kHalAdapterTraceCategory, track);

// Generates unique track by given |event|, |primary_id| and |secondary_id|. For
// |secondary_id|, only the last 16 bits will be used.
perfetto::Track GetTraceTrack(HalAdapterTraceEvent event,
                              int primary_id = 0,
                              int secondary_id = 0);

perfetto::StaticString ToString(HalAdapterTraceEvent event);

}  // namespace cros

#endif  // CAMERA_HAL_ADAPTER_CAMERA_TRACE_EVENT_H_
