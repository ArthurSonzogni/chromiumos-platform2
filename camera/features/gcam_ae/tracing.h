/*
 * Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_FEATURES_GCAM_AE_TRACING_H_
#define CAMERA_FEATURES_GCAM_AE_TRACING_H_

#include "cros-camera/tracing.h"

#define TRACE_GCAM_AE(...) \
  TRACE_EVENT_AUTOGEN(kCameraTraceCategoryGcamAe, ##__VA_ARGS__)

#define TRACE_GCAM_AE_BEGIN(...) \
  TRACE_EVENT_BEGIN(kCameraTraceCategoryGcamAe, ##__VA_ARGS__)

#define TRACE_GCAM_AE_END() TRACE_EVENT_END(kCameraTraceCategoryGcamAe)

#endif  // CAMERA_FEATURES_GCAM_AE_TRACING_H_
