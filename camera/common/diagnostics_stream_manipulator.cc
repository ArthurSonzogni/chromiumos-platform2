// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "common/diagnostics_stream_manipulator.h"

#include <cstdint>
#include <utility>

#include <base/check.h>
#include <drm_fourcc.h>
#include <libyuv/scale.h>

#include "camera/mojo/camera_diagnostics.mojom.h"
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
  if (!diagnostics_client_->IsFrameAnalysisEnabled() || !selected_stream_ ||
      result.frame_number() < next_target_frame_number_) {
    callbacks_.result_callback.Run(std::move(result));
    return true;
  }

  for (auto& buffer : result.GetMutableOutputBuffers()) {
    if (buffer.stream() != selected_stream_) {
      continue;
    }

    auto requested_buffer = diagnostics_client_->RequestEmptyFrame();
    if (!requested_buffer.has_value()) {
      VLOGF(2) << "Failed to get an empty buffer from diag client, skip!";
      break;
    }

    auto diag_buffer = std::move(requested_buffer.value());
    diag_buffer->stream->width = target_frame_size_.width;
    diag_buffer->stream->height = target_frame_size_.height;
    diag_buffer->frame_number = result.frame_number();
    diag_buffer->source = camera_diag::mojom::DataSource::kCameraService;
    diag_buffer->is_empty = true;

    VLOGF(1) << "Processing buffer for frame " << result.frame_number();

    if (FillDiagnosticsBuffer(buffer, diag_buffer->buffer)) {
      diag_buffer->is_empty = false;
      next_target_frame_number_ =
          result.frame_number() + diagnostics_client_->frame_interval();
      VLOGF(1) << "Output buffer processed in frame " << result.frame_number()
               << ", next_target_frame_number_: " << next_target_frame_number_;
    }

    diagnostics_client_->SendFrame(std::move(diag_buffer));
    break;
  }

  callbacks_.result_callback.Run(std::move(result));
  return true;
}

bool DiagnosticsStreamManipulator::FillDiagnosticsBuffer(
    Camera3StreamBuffer& stream_buffer,
    camera_diag::mojom::CameraFrameBufferPtr& out_frame) {
  constexpr int kSyncWaitTimeoutMs = 300;
  if (!stream_buffer.WaitOnAndClearReleaseFence(kSyncWaitTimeoutMs)) {
    LOGFID(ERROR, 1) << "Timed out waiting for acquiring output buffer";
    return false;
  }
  auto mapping_src = ScopedMapping(*stream_buffer.buffer());
  if (!mapping_src.is_valid() || mapping_src.drm_format() != DRM_FORMAT_NV12) {
    VLOGF(1) << "Invalid mapping_src. Can not process frame";
    return false;
  }

  uint32_t src_width = stream_buffer.stream()->width;
  uint32_t src_height = stream_buffer.stream()->height;

  uint32_t y_size = target_frame_size_.height * target_frame_size_.width;
  uint32_t nv12_data_size = y_size * 3 / 2;
  uint8_t y_stride = target_frame_size_.height;
  uint8_t uv_stride = target_frame_size_.width / 2;

  if (out_frame->shm_handle->GetSize() < nv12_data_size) {
    // Soft ignore the invalid diagnsotics frame instead of CHECKs.
    VLOGF(1) << "Too small size of CameraFrameBufferPtr, "
             << out_frame->shm_handle->GetSize() << " vs " << nv12_data_size;
    return false;
  }

  mojo::ScopedSharedBufferMapping y_mapping =
      out_frame->shm_handle->Map(y_size);
  mojo::ScopedSharedBufferMapping uv_mapping =
      out_frame->shm_handle->MapAtOffset(nv12_data_size - y_size, y_size);

  if (y_mapping == nullptr || uv_mapping == nullptr) {
    VLOGF(1) << "Failed to map the output buffer";
    return false;
  }

  VLOGF(1) << "Downscaling " << src_width << "x" << src_height << " -> "
           << target_frame_size_.width << "x" << target_frame_size_.height;

  // TODO(imranziad): Use GPU scaling.
  int ret = libyuv::NV12Scale(
      mapping_src.plane(0).addr, mapping_src.plane(0).stride,
      mapping_src.plane(1).addr, mapping_src.plane(1).stride, src_width,
      src_height, static_cast<uint8_t*>(y_mapping.get()), y_stride,
      static_cast<uint8_t*>(uv_mapping.get()), uv_stride,
      target_frame_size_.width, target_frame_size_.height,
      libyuv::kFilterBilinear);

  if (ret != 0) {
    LOGF(ERROR) << "DiagnosticsSM: libyuv::NV12Scale() failed: " << ret;
    return false;
  }

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
  next_target_frame_number_ = 0;
}

}  // namespace cros
