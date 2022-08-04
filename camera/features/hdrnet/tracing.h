/*
 * Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_FEATURES_HDRNET_TRACING_H_
#define CAMERA_FEATURES_HDRNET_TRACING_H_

#include "cros-camera/tracing.h"

#define TRACE_HDRNET(...) \
  TRACE_EVENT_AUTOGEN(kCameraTraceCategoryHdrnet, ##__VA_ARGS__)

#define TRACE_HDRNET_BEGIN(...) \
  TRACE_EVENT_BEGIN(kCameraTraceCategoryHdrnet, ##__VA_ARGS__)

#define TRACE_HDRNET_END() TRACE_EVENT_END(kCameraTraceCategoryHdrnet)

#endif  // CAMERA_FEATURES_HDRNET_TRACING_H_
