/*
 * Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_COMMON_PRIVACY_SHUTTER_DETECTOR_IMPL_H_
#define CAMERA_COMMON_PRIVACY_SHUTTER_DETECTOR_IMPL_H_

#include "cros-camera/privacy_shutter_detector.h"

namespace cros {

// Implementation of PrivacyShutterDetector. This class is not thread-safe.
class PrivacyShutterDetectorImpl : public PrivacyShutterDetector {
 public:
  PrivacyShutterDetectorImpl();
  ~PrivacyShutterDetectorImpl() override;

  bool DetectPrivacyShutterFromHandle(buffer_handle_t input,
                                      int width,
                                      int height,
                                      bool* isShutterClosed) override;

 private:
  bool DetectPrivacyShutterFromHandleInternal(uint8_t* yData,
                                              uint32_t yStride,
                                              int width,
                                              int height);
};

}  // namespace cros

#endif  // CAMERA_COMMON_PRIVACY_SHUTTER_DETECTOR_IMPL_H_
