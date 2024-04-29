// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/tests/fake_cros_camera_controller.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>

#include <base/functional/bind.h>
#include <base/location.h>
#include <base/notimplemented.h>
#include <base/task/sequenced_task_runner.h>
#include <base/time/time.h>
#include <chromeos/mojo/service_constants.h>

#include "camera/mojo/camera_diagnostics.mojom.h"
#include "cros-camera/common.h"
#include "cros-camera/common_types.h"

namespace cros::tests {

namespace {
constexpr int kInterFrameDelayMs = 30;  // A little faster than 30fps.
}

FakeCrosCameraController::FakeCrosCameraController(
    mojo::PendingRemote<chromeos::mojo_service_manager::mojom::ServiceManager>
        service_manager)
    : service_manager_(std::move(service_manager)),
      task_runner_(base::SequencedTaskRunner::GetCurrentDefault()) {}

void FakeCrosCameraController::Initialize() {
  cros_camera_provider_.Register(
      service_manager_.get(), chromeos::mojo_services::kCrosCameraController);
  service_manager_->Request(
      chromeos::mojo_services::kCrosCameraDiagnosticsService, std::nullopt,
      diag_service_.BindNewPipeAndPassReceiver().PassPipe());
}

void FakeCrosCameraController::OpenCamera(
    camera_diag::mojom::CameraStreamPtr stream, FrameType frame_type) {
  stream_ = std::move(stream);
  next_frame_number_ = 0;
  if (frame_type_ != frame_type) {
    // Clear the cached frame if the color has changed.
    cached_nv12_data_.clear();
    frame_type_ = frame_type;
  }
}

bool FakeCrosCameraController::ValidateDiagnosticsFrame(
    const camera_diag::mojom::CameraFramePtr& frame) {
  if (!stream_ || frame.is_null() || !frame->is_empty) {
    return false;
  }
  Size selected_size = {stream_->width, stream_->height};
  Size diag_frame_size = {frame->stream->width, frame->stream->height};
  constexpr float kAspectRatioMargin = 0.004;
  return (selected_size.is_valid() && diag_frame_size.is_valid() &&
          std::fabs(selected_size.aspect_ratio() -
                    diag_frame_size.aspect_ratio()) < kAspectRatioMargin);
}

void FakeCrosCameraController::FillFrame(
    camera_diag::mojom::CameraFramePtr& frame, const FrameType& frame_type) {
  CHECK(frame->is_empty);
  const int y_size = (frame->stream->width * frame->stream->height);
  const int nv12_size = y_size + y_size / 2;

  mojo::ScopedSharedBufferMapping nv12_mapping =
      frame->buffer->shm_handle->Map(nv12_size);

  if (nv12_mapping == nullptr) {
    LOGF_THROTTLED(ERROR, 5) << "Failed to map the diagnostics buffer, frame "
                             << frame->frame_number.value_or(-1);
    return;
  }

  if (cached_nv12_data_.size() == nv12_size) {
    uint8_t* nv12_data = static_cast<uint8_t*>(nv12_mapping.get());
    std::copy(cached_nv12_data_.begin(), cached_nv12_data_.end(), nv12_data);
    frame->is_empty = false;
    return;
  }

  cached_nv12_data_.resize(nv12_size);

  uint8_t y_value, u_value, v_value;

  switch (frame_type) {
    case cros::tests::FrameType::kAny:
    case cros::tests::FrameType::kBlack:
      y_value = 0;              // Black luminance
      u_value = v_value = 128;  // Neutral gray
      break;
    case cros::tests::FrameType::kBlurry:
      y_value = 255;            // White luminance
      u_value = v_value = 128;  // Neutral gray
      break;
    case cros::tests::FrameType::kGreen:
      y_value = 128;  // Green luminance
      u_value = 80;   // Green chrominance U
      v_value = 180;  // Green chrominance V
      break;
    default:
      NOTIMPLEMENTED();
  }

  // Fill the Y plane (luma) with Y value
  std::fill(cached_nv12_data_.begin(), cached_nv12_data_.begin() + y_size,
            y_value);

  if (u_value == v_value) {
    std::fill(cached_nv12_data_.begin() + y_size, cached_nv12_data_.end(),
              u_value);
  } else {
    // Fill the UV plane with alternating U and V values
    for (size_t i = y_size; i < nv12_size; i += 2) {
      cached_nv12_data_[i] = u_value;
      cached_nv12_data_[i + 1] = v_value;
    }
  }

  uint8_t* nv12_data = static_cast<uint8_t*>(nv12_mapping.get());
  std::copy(cached_nv12_data_.begin(), cached_nv12_data_.end(), nv12_data);
  frame->is_empty = false;
}

void FakeCrosCameraController::StartStreaming(
    camera_diag::mojom::StreamingConfigPtr config,
    StartStreamingCallback callback) {
  if (stream_.is_null()) {
    // Camera closed.
    std::move(callback).Run(camera_diag::mojom::StartStreamingResult::NewError(
        camera_diag::mojom::ErrorCode::kCameraClosed));
    return;
  }

  frame_interval_ = config->frame_interval;

  std::move(callback).Run(
      camera_diag::mojom::StartStreamingResult::NewStream(stream_->Clone()));
}

void FakeCrosCameraController::StopStreaming() {
  // Do nothing.
}

void FakeCrosCameraController::RequestFrame(
    camera_diag::mojom::CameraFramePtr frame) {
  if (stream_.is_null()) {
    // Drop the frame request.
    return;
  }
  task_runner_->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&FakeCrosCameraController::SendFrame,
                     base::Unretained(this), std::move(frame)),
      base::Milliseconds(frame_interval_ * kInterFrameDelayMs));
}

void FakeCrosCameraController::SendFrame(
    camera_diag::mojom::CameraFramePtr frame) {
  if (!ValidateDiagnosticsFrame(frame)) {
    LOGF_THROTTLED(ERROR, 5) << "Invalid diagnostics frame";
    frame->is_empty = true;
    diag_service_->SendFrame(std::move(frame));
    return;
  }
  frame->frame_number = next_frame_number_;
  next_frame_number_ += frame_interval_;
  FillFrame(frame, frame_type_);
  frame->source = camera_diag::mojom::DataSource::kCameraService;
  diag_service_->SendFrame(std::move(frame));
}

}  // namespace cros::tests
