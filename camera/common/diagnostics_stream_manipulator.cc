// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "common/diagnostics_stream_manipulator.h"

#include <drm_fourcc.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>

#include <base/check.h>
#include <base/numerics/safe_conversions.h>
#include <base/numerics/safe_math.h>
#include <libyuv/scale.h>

#include "camera/mojo/camera_diagnostics.mojom.h"
#include "common/camera_hal3_helpers.h"
#include "cros-camera/camera_buffer_manager.h"
#include "cros-camera/camera_metadata_utils.h"

namespace cros {

namespace {
// Minimum pixel count in a frame required by diagnostics service.
constexpr uint64_t kMinPixelCount = 640 * 480;
// Maximum pixel count in a frame allowed by diagnostics service.
constexpr uint64_t kMaxPixelCount = 1920 * 1080;
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
  available_yuv_stream_sizes_.clear();
  if (static_info != nullptr) {
    auto stream_configs = GetRoMetadataAsSpan<int32_t>(
        static_info, ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS);
    for (size_t i = 0; i + 3 < stream_configs.size(); i += 4) {
      const int32_t format = stream_configs[i];
      const int32_t width = stream_configs[i + 1];
      const int32_t height = stream_configs[i + 2];
      const int32_t direction = stream_configs[i + 3];
      if (direction == ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT &&
          (format == HAL_PIXEL_FORMAT_YCbCr_420_888 ||
           format == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED) &&
          width > 0 && height > 0) {
        available_yuv_stream_sizes_.insert(
            Size(static_cast<uint32_t>(width), static_cast<uint32_t>(height)));
      }
    }
  }
  return true;
}

bool DiagnosticsStreamManipulator::ConfigureStreams(
    Camera3StreamConfiguration* stream_config) {
  Reset();
  if (!stream_config) {
    return false;
  }
  // Select the stream with the smallest width.
  for (const camera3_stream_t* stream : stream_config->GetStreams()) {
    if (!stream || stream->stream_type != CAMERA3_STREAM_OUTPUT ||
        stream->format != HAL_PIXEL_FORMAT_YCbCr_420_888 ||
        (stream->usage & kStillCaptureUsageFlag) == kStillCaptureUsageFlag ||
        (stream->usage & GRALLOC_USAGE_HW_CAMERA_ZSL) ==
            GRALLOC_USAGE_HW_CAMERA_ZSL) {
      continue;
    }
    Size stream_size(stream->width, stream->height);
    base::CheckedNumeric<uint32_t> stream_nv12_size = stream->width;
    stream_nv12_size *= stream->height;
    stream_nv12_size = stream_nv12_size * 3 / 2;
    if (!stream_size.is_valid() || stream_size.width % 2 != 0 ||
        stream_size.height % 2 != 0 || !stream_nv12_size.IsValid() ||
        !base::IsValueInRangeForNumericType<int>(stream->width) ||
        !base::IsValueInRangeForNumericType<int>(stream->height)) {
      continue;
    }
    if (!available_yuv_stream_sizes_.empty() &&
        !available_yuv_stream_sizes_.contains(stream_size)) {
      continue;
    }
    uint64_t stream_pixel_count = stream_size.area();
    uint64_t selected_pixel_count =
        selected_stream_
            ? Size(selected_stream_->width, selected_stream_->height).area()
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
  if (!diagnostics_client_->AddCameraSession(
          Size{selected_stream_->width, selected_stream_->height})) {
    selected_stream_ = nullptr;
    return true;
  }
  VLOGF(1) << "Selected stream for diagnostics "
           << GetDebugString(selected_stream_);
  return true;
}

bool DiagnosticsStreamManipulator::OnConfiguredStreams(
    Camera3StreamConfiguration* stream_config) {
  if (selected_stream_ &&
      (!stream_config ||
       !std::ranges::contains(stream_config->GetStreams(), selected_stream_))) {
    Reset();
  }
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
  if (!out_frame || !out_frame->shm_handle.is_valid() ||
      !target_size.is_valid() || target_size.width % 2 != 0 ||
      target_size.height % 2 != 0 || target_size.area() < kMinPixelCount ||
      target_size.area() > kMaxPixelCount) {
    VLOGF(1) << "Invalid target_size or output frame buffer";
    return false;
  }

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
  if (src_width == 0 || src_height == 0 ||
      !base::IsValueInRangeForNumericType<int>(src_width) ||
      !base::IsValueInRangeForNumericType<int>(src_height) ||
      !base::IsValueInRangeForNumericType<int>(target_size.width) ||
      !base::IsValueInRangeForNumericType<int>(target_size.height)) {
    VLOGF(1) << "Out-of-range dimensions for NV12Scale";
    return false;
  }

  base::CheckedNumeric<uint32_t> checked_y_size = target_size.width;
  checked_y_size *= target_size.height;
  base::CheckedNumeric<uint32_t> checked_nv12_size = checked_y_size * 3 / 2;
  if (!checked_y_size.IsValid() || !checked_nv12_size.IsValid()) {
    LOGF(ERROR) << "Overflow calculating NV12 buffer size for "
                << target_size.ToString();
    return false;
  }

  uint32_t y_size = checked_y_size.ValueOrDie();
  uint32_t nv12_data_size = checked_nv12_size.ValueOrDie();
  uint32_t y_stride = target_size.width;
  uint32_t uv_stride = y_stride;

  if (out_frame->size < nv12_data_size ||
      out_frame->shm_handle->GetSize() < nv12_data_size) {
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
  if (!selected_stream_ || frame.is_null() || !frame->is_empty ||
      frame->stream.is_null() || frame->buffer.is_null() ||
      !frame->buffer->shm_handle.is_valid() ||
      frame->stream->pixel_format != camera_diag::mojom::PixelFormat::kYuv420) {
    return false;
  }
  Size selected_size = {selected_stream_->width, selected_stream_->height};
  Size diag_frame_size = {frame->stream->width, frame->stream->height};
  if (!selected_size.is_valid() || !diag_frame_size.is_valid() ||
      diag_frame_size.width % 2 != 0 || diag_frame_size.height % 2 != 0 ||
      diag_frame_size.area() < kMinPixelCount ||
      diag_frame_size.area() > kMaxPixelCount) {
    return false;
  }
  // Aspect ratio should be the same.
  // We could compare the ratios with integers to be precise, but that would
  // make it slow. This error margin is good enough for us.
  constexpr float kAspectRatioMargin = 0.004;
  return std::fabs(selected_size.aspect_ratio() -
                   diag_frame_size.aspect_ratio()) < kAspectRatioMargin;
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
