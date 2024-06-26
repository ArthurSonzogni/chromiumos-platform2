// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CAMERA_FEATURES_KIOSK_VISION_TRACING_H_
#define CAMERA_FEATURES_KIOSK_VISION_TRACING_H_

#include "cros-camera/tracing.h"

#define TRACE_KIOSK_VISION(...) \
  TRACE_EVENT_AUTOGEN(kCameraTraceCategoryKioskVision, ##__VA_ARGS__)

#endif  // CAMERA_FEATURES_KIOSK_VISION_TRACING_H_
