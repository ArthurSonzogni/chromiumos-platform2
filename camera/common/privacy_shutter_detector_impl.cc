/*
 * Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common/privacy_shutter_detector_impl.h"

#include <memory>

#include "cros-camera/camera_buffer_manager.h"
#include "cros-camera/common.h"

namespace cros {

const int kVarThreshold = 4;
const int kMeanThreshold = 16;
const int kMaxThreshold = 50;

std::unique_ptr<PrivacyShutterDetector> PrivacyShutterDetector::New() {
  return std::make_unique<PrivacyShutterDetectorImpl>();
}

PrivacyShutterDetectorImpl::PrivacyShutterDetectorImpl() = default;

PrivacyShutterDetectorImpl::~PrivacyShutterDetectorImpl() = default;

bool PrivacyShutterDetectorImpl::DetectPrivacyShutterFromHandle(
    buffer_handle_t input, int width, int height, bool* isShutterClosed) {
  cros::CameraBufferManager* buffer_manager =
      cros::CameraBufferManager::GetInstance();

  struct android_ycbcr mapped_input;
  auto status = buffer_manager->LockYCbCr(input, 0, 0, 0, 0, 0, &mapped_input);
  if (status != 0) {
    LOGF(ERROR) << "Failed to lock buffer handle to detect privacy shutter.";
    return false;
  }
  auto* yData = static_cast<uint8_t*>(mapped_input.y);
  uint32_t yStride = mapped_input.ystride / sizeof(*yData);

  *isShutterClosed =
      DetectPrivacyShutterFromHandleInternal(yData, yStride, width, height);

  status = buffer_manager->Unlock(input);
  if (status != 0) {
    LOGF(WARNING)
        << "Failed to unlock buffer handle to detect privacy shutter.";
  }

  return true;
}

bool PrivacyShutterDetectorImpl::DetectPrivacyShutterFromHandleInternal(
    uint8_t* yData, uint32_t yStride, int width, int height) {
  double ySum = 0;
  for (uint32_t y = 0; y < height; y++) {
    for (uint32_t x = 0; x < width; x++) {
      auto offset = yStride * y + x;
      ySum += yData[offset];
      if (kMaxThreshold < yData[offset]) {
        LOGF(ERROR) << "The image has a bright spot: "
                    << static_cast<int>(yData[offset]);
        return false;
      }
    }
  }

  double yMean = ySum / width / height;
  if (yMean > kMeanThreshold) {
    LOGF(ERROR) << "The image is overall bright: " << yMean;
    return false;
  }

  int64_t yVar = 0;
  for (uint32_t y = 0; y < height; y++) {
    for (uint32_t x = 0; x < width; x++) {
      auto offset = yStride * y + x;
      yVar += pow(yData[offset] - yMean, 2);
    }
  }

  yVar /= width * height;
  if (yVar > kVarThreshold) {
    LOGF(ERROR) << "Variance is over threshold: " << yVar;
    return false;
  }

  return true;
}

}  // namespace cros
