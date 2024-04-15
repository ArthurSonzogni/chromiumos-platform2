// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "camera/diagnostics/analyzers/privacy_shutter_analyzer.h"

#include "camera/mojo/camera_diagnostics.mojom.h"
#include "cros-camera/common.h"

namespace cros {

namespace {
constexpr int kMaxThreshold = 86;
constexpr int kMeanThreshold = 26;
constexpr int kVarThreshold = 29;
// Number of frames to fail consecutively to consider privacy shutter as ON.
constexpr int kNoOfFramesToConsiderAFailure = 5;
}  // namespace

bool PrivacyShutterAnalyzer::IsValidFrame(
    const camera_diag::mojom::CameraFramePtr& frame) {
  return (frame->stream->pixel_format ==
              camera_diag::mojom::PixelFormat::kYuv420 &&
          frame->stream->width > 0 && frame->stream->height > 0);
}

void PrivacyShutterAnalyzer::AnalyzeFrame(
    const camera_diag::mojom::CameraFramePtr& frame) {
  VLOGF(1) << "Running privacy shutter on frame "
           << frame->frame_number.value_or(-1);

  if (!IsValidFrame(frame)) {
    return;
  }

  // We always keep latest consecutive shutter detected count.
  if (DetectPrivacyShutter(frame)) {
    shutter_detected_on_frames_count_++;
  } else {
    shutter_detected_on_frames_count_ = 0;
  }
}

camera_diag::mojom::AnalyzerResultPtr PrivacyShutterAnalyzer::GetResult() {
  LOGF(INFO) << "PrivacyShutterAnalyzer: total analyzed "
             << analyzed_frames_count_ << " frames, shutter detected on "
             << shutter_detected_on_frames_count_ << " frames";
  auto result = camera_diag::mojom::AnalyzerResult::New();
  result->type = type_;

  // When fewer than |kNoOfFramesToConsiderAFailure| frames were analyzed.
  result->status = camera_diag::mojom::AnalyzerStatus::kNotRun;

  if (shutter_detected_on_frames_count_ >= kNoOfFramesToConsiderAFailure) {
    result->status = camera_diag::mojom::AnalyzerStatus::kFailed;
  } else if (analyzed_frames_count_ >= kNoOfFramesToConsiderAFailure) {
    // |kNoOfFramesToConsiderAFailure| frames were run but shutter was not
    // detected.
    result->status = camera_diag::mojom::AnalyzerStatus::kPassed;
  }

  return result;
}

bool PrivacyShutterAnalyzer::DetectPrivacyShutter(
    const camera_diag::mojom::CameraFramePtr& frame) {
  int width = frame->stream->width;
  int height = frame->stream->height;

  uint32_t y_size = width * height;
  uint32_t y_stride = width;

  mojo::ScopedSharedBufferMapping y_mapping =
      frame->buffer->shm_handle->Map(y_size);

  uint8_t* y_data = static_cast<uint8_t*>(y_mapping.get());

  analyzed_frames_count_++;

  double y_sum = 0;
  for (uint32_t y = 0; y < height; y++) {
    for (uint32_t x = 0; x < width; x++) {
      auto offset = y_stride * y + x;
      y_sum += y_data[offset];
      if (kMaxThreshold < y_data[offset]) {
        VLOGF(2) << "The image has a bright spot: "
                 << static_cast<int>(y_data[offset]);
        return false;
      }
    }
  }

  double y_mean = y_sum / (width * height);
  if (kMeanThreshold < y_mean) {
    VLOGF(2) << "The image is overall bright: " << y_mean;
    return false;
  }

  double y_var = 0;
  for (uint32_t y = 0; y < height; y++) {
    for (uint32_t x = 0; x < width; x++) {
      auto offset = y_stride * y + x;
      double diff = y_data[offset] - y_mean;
      y_var += (diff * diff);
    }
  }

  y_var /= (width * height);
  if (kVarThreshold < y_var) {
    VLOGF(2) << "Variance is over threshold: " << y_var;
    return false;
  }

  return true;
}

}  // namespace cros
