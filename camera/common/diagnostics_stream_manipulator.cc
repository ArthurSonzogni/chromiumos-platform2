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
#include "common/camera_hal3_helpers.h"
#include "cros-camera/camera_buffer_manager.h"

namespace cros {

namespace {
// Minimum pixel count in a frame required by diagnostics service.
constexpr int kMinPixelCount = 640 * 480;
}  // namespace

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
        (stream->usage & kStillCaptureUsageFlag) == kStillCaptureUsageFlag ||
        (stream->usage & GRALLOC_USAGE_HW_CAMERA_ZSL) ==
            GRALLOC_USAGE_HW_CAMERA_ZSL) {
      continue;
    }
    uint32_t stream_pixel_count = stream->width * stream->height;
    uint32_t selected_pixel_count =
        selected_stream_ ? selected_stream_->width * selected_stream_->height
                         : 0;
    // Prefer the smallest stream with pixel count >= |kMinPixelCount|. Select
    // the largest when all streams are smaller than |kMinPixelCount|.
    if ((stream_pixel_count >= kMinPixelCount &&
         stream_pixel_count < selected_pixel_count) ||
        (stream_pixel_count > selected_pixel_count &&
         selected_pixel_count < kMinPixelCount)) {
      selected_stream_ = stream;
    }
  }
  if (!selected_stream_) {
    VLOGF(1) << "No YUV stream found, diagnostics will be ignored";
    return true;
  }
  diagnostics_client_->AddCameraSession(
      Size{selected_stream_->width, selected_stream_->height});
  VLOGF(1) << "Selected stream for diagnostics "
           << GetDebugString(selected_stream_);
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

    if (!ValidateDiagnosticsFrame(diag_buffer)) {
      VLOGF(1) << "Invalid diagnostics frame, skip!";
      diagnostics_client_->SendFrame(std::move(diag_buffer));
      break;
    }

    diag_buffer->frame_number = result.frame_number();
    diag_buffer->source = camera_diag::mojom::DataSource::kCameraService;
    diag_buffer->is_empty = true;

    VLOGF(1) << "Processing buffer for frame " << result.frame_number();

    if (FillDiagnosticsBuffer(
            Size{diag_buffer->stream->width, diag_buffer->stream->height},
            buffer, diag_buffer->buffer)) {
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
    const Size& target_size,
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

  uint32_t y_size = target_size.height * target_size.width;
  uint32_t nv12_data_size = y_size * 3 / 2;
  uint32_t y_stride = target_size.width;
  uint32_t uv_stride = y_stride;

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
           << target_size.width << "x" << target_size.height;

  // TODO(imranziad): Use GPU scaling.
  int ret = libyuv::NV12Scale(
      mapping_src.plane(0).addr, mapping_src.plane(0).stride,
      mapping_src.plane(1).addr, mapping_src.plane(1).stride, src_width,
      src_height, static_cast<uint8_t*>(y_mapping.get()), y_stride,
      static_cast<uint8_t*>(uv_mapping.get()), uv_stride, target_size.width,
      target_size.height, libyuv::kFilterBilinear);

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

bool DiagnosticsStreamManipulator::ValidateDiagnosticsFrame(
    const camera_diag::mojom::CameraFramePtr& frame) {
  if (!selected_stream_ || frame.is_null() || !frame->is_empty) {
    return false;
  }
  Size selected_size = {selected_stream_->width, selected_stream_->height};
  Size diag_frame_size = {frame->stream->width, frame->stream->height};
  // Aspect ratio should be the same.
  // We could compare the ratios with integers to be precise, but that would
  // make it slow. This error margin is good enough for us.
  constexpr float kAspectRatioMargin = 0.004;
  return (selected_size.is_valid() && diag_frame_size.is_valid() &&
          std::fabs(selected_size.aspect_ratio() -
                    diag_frame_size.aspect_ratio()) < kAspectRatioMargin);
}

void DiagnosticsStreamManipulator::Reset() {
  if (selected_stream_) {
    // Removing a session we did not setup may override a current session.
    diagnostics_client_->RemoveCameraSession();
  }
  selected_stream_ = nullptr;
  next_target_frame_number_ = 0;
}

}  // namespace cros
