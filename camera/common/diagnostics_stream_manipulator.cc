// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "common/diagnostics_stream_manipulator.h"

#include <utility>

#include <base/check.h>
#include <drm_fourcc.h>
#include <libyuv/scale.h>

#include "cros-camera/camera_buffer_manager.h"

namespace {
// Target width of the downscaled buffer.
constexpr uint32_t kTargetStreamWidth = 640;
}  // namespace

namespace cros {

DiagnosticsStreamManipulator::DiagnosticsStreamManipulator(
    CameraDiagnosticsClient* diagnostics_client)
    : camera_buffer_manager_(cros::CameraBufferManager::GetInstance()),
      diagnostics_client_(diagnostics_client) {}

DiagnosticsStreamManipulator::~DiagnosticsStreamManipulator() {
  DCHECK(diagnostics_client_);
  Reset();
}

bool DiagnosticsStreamManipulator::Initialize(
    const camera_metadata_t* static_info,
    StreamManipulator::Callbacks callbacks) {
  callbacks_ = std::move(callbacks);
  return true;
}

bool DiagnosticsStreamManipulator::ConfigureStreams(
    Camera3StreamConfiguration* stream_config) {
  Reset();
  // Select the stream with the smallest width.
  for (const camera3_stream_t* stream : stream_config->GetStreams()) {
    if (stream->stream_type != CAMERA3_STREAM_OUTPUT ||
        stream->format != HAL_PIXEL_FORMAT_YCbCr_420_888 ||
        (stream->usage & GRALLOC_USAGE_PRIVATE_1) ||
        (stream->usage & GRALLOC_USAGE_HW_CAMERA_ZSL)) {
      continue;
    }
    if (!selected_stream_ || (stream->width < selected_stream_->width &&
                              stream->width >= kTargetStreamWidth)) {
      selected_stream_ = stream;
    }
  }
  if (!selected_stream_) {
    VLOGF(1) << "No YUV stream found, diagnostics will be ignored";
    return true;
  }
  DCHECK_GE(selected_stream_->width, kTargetStreamWidth);
  // We don't need to be accurate, just choose a size that works for us.
  constexpr float kAspectRatioMargin = 0.04;
  constexpr float kTargetAspectRatio16_9 = 1.778;
  constexpr float kTargetAspectRatio4_3 = 1.333;

  float aspect_ratio = static_cast<float>(selected_stream_->width) /
                       static_cast<float>(selected_stream_->height);

  if (std::fabs(kTargetAspectRatio16_9 - aspect_ratio) < kAspectRatioMargin) {
    target_frame_size_ = {kTargetStreamWidth, 360};
  } else if (std::fabs(kTargetAspectRatio4_3 - aspect_ratio) <
             kAspectRatioMargin) {
    target_frame_size_ = {kTargetStreamWidth, 480};
  } else {
    VLOGF(1) << "Aspect ratio not supported, diagnostics will be ignored";
    // TODO(imranziad): Test and enable for any aspect ratio.
    selected_stream_ = nullptr;
    return true;
  }
  diagnostics_client_->AddCameraSession(target_frame_size_);
  VLOGF(1) << "Selected stream for diagnostics "
           << GetDebugString(selected_stream_)
           << ", target=" << target_frame_size_.ToString();
  return true;
}

bool DiagnosticsStreamManipulator::OnConfiguredStreams(
    Camera3StreamConfiguration* stream_config) {
  return true;
}

bool DiagnosticsStreamManipulator::ConstructDefaultRequestSettings(
    android::CameraMetadata* default_request_settings, int type) {
  return true;
}

bool DiagnosticsStreamManipulator::ProcessCaptureRequest(
    Camera3CaptureDescriptor* request) {
  return true;
}

bool DiagnosticsStreamManipulator::ProcessCaptureResult(
    Camera3CaptureDescriptor result) {
  DCHECK(diagnostics_client_);
  if (!diagnostics_client_->IsFrameAnalysisEnabled()) {
    callbacks_.result_callback.Run(std::move(result));
    return true;
  }
  // TODO(imranziad): Select and copy an output buffer.
  callbacks_.result_callback.Run(std::move(result));
  return true;
}

void DiagnosticsStreamManipulator::Notify(camera3_notify_msg_t msg) {
  callbacks_.notify_callback.Run(std::move(msg));
}

bool DiagnosticsStreamManipulator::Flush() {
  return true;
}

void DiagnosticsStreamManipulator::Reset() {
  if (selected_stream_) {
    // Removing a session we did not setup may override a current session.
    diagnostics_client_->RemoveCameraSession();
  }
  selected_stream_ = nullptr;
  target_frame_size_ = {0, 0};
}

}  // namespace cros
