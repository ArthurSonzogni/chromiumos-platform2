// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/camera_diagnostics_session.h"

#include <utility>

#include <base/functional/bind.h>
#include <base/check.h>
#include <base/location.h>
#include <base/sequence_checker.h>
#include <base/task/sequenced_task_runner.h>
#include <base/threading/thread_checker.h>
#include <chromeos/mojo/service_constants.h>

#include "camera/mojo/camera_diagnostics.mojom.h"
#include "cros-camera/common.h"
#include "diagnostics/camera_diagnostics_mojo_manager.h"

namespace {
constexpr int kStreamingFrameIntervalDefault = 30;  // every 30th frame
}  // namespace

namespace cros {

namespace {
constexpr int kMaxStreamWidth = 640;
constexpr int kMaxStreamHeight = 480;

// Creates an empty camera frame of |camera_diag::mojom::PixelFormat::kYuv420|.
camera_diag::mojom::CameraFramePtr CreateEmptyCameraFrame(
    const camera_diag::mojom::CameraStreamPtr& stream) {
  if (!(stream->pixel_format == camera_diag::mojom::PixelFormat::kYuv420 &&
        stream->width > 0 && stream->height > 0 &&
        stream->width <= kMaxStreamWidth &&
        stream->height <= kMaxStreamHeight && stream->width % 2 == 0 &&
        stream->height % 2 == 0)) {
    LOGF(ERROR) << "Can not create camera frame with invalid stream size: "
                << stream->width << "x" << stream->height;
    return nullptr;
  }
  // TODO(imranziad): Validate stream size.
  auto frame = camera_diag::mojom::CameraFrame::New();
  frame->stream = stream.Clone();
  frame->source = camera_diag::mojom::DataSource::kCameraDiagnostics;
  frame->is_empty = true;
  frame->buffer = camera_diag::mojom::CameraFrameBuffer::New();
  // Only NV12 frames are supported now. Average of 12 bits per pixel.
  frame->buffer->size = (stream->width * stream->height * 3) / 2;
  frame->buffer->shm_handle =
      mojo::SharedBufferHandle::Create(frame->buffer->size);
  if (!frame->buffer->shm_handle->is_valid()) {
    LOGF(ERROR) << "Failed to create SharedBufferHandle for stream size: "
                << stream->width << "x" << stream->height;
    return nullptr;
  }
  return frame;
}

}  // namespace

CameraDiagnosticsSession::CameraDiagnosticsSession(
    CameraDiagnosticsMojoManager* mojo_manager,
    scoped_refptr<Future<void>> notify_finish)
    : thread_("CameraDiagSession"),
      camera_service_controller_(mojo_manager),
      notify_finish_(notify_finish) {
  CHECK(thread_.Start());
}

void CameraDiagnosticsSession::RunFrameAnalysis(
    camera_diag::mojom::FrameAnalysisConfigPtr config) {
  thread_.PostTaskAsync(
      FROM_HERE,
      base::BindOnce(&CameraDiagnosticsSession::RunFrameAnalysisOnThread,
                     base::Unretained(this), std::move(config)));
}

void CameraDiagnosticsSession::QueueFrame(
    camera_diag::mojom::CameraFramePtr frame) {
  // Process frame
  VLOGF(1) << "Frame received";
}

void CameraDiagnosticsSession::RunFrameAnalysisOnThread(
    camera_diag::mojom::FrameAnalysisConfigPtr config) {
  DCHECK(thread_.IsCurrentThread());
  LOGF(INFO) << "FrameAnalysis started in session";
  auto start_stream_config = camera_diag::mojom::StreamingConfig::New();
  // TODO(imranziad): Adjust the interval based on |config->duration_ms|.
  start_stream_config->frame_interval = kStreamingFrameIntervalDefault;
  camera_service_controller_.StartStreaming(
      std::move(start_stream_config),
      base::BindOnce(&CameraDiagnosticsSession::OnStartStreaming,
                     base::Unretained(this)));
}

void CameraDiagnosticsSession::OnStartStreaming(
    camera_diag::mojom::StartStreamingResultPtr result) {
  base::AutoLock lock(lock_);
  // Successfully started streaming.
  if (result->is_stream()) {
    selected_stream_ = std::move(result->get_stream());
    LOGF(INFO) << "Selected stream config: " << selected_stream_.value()->width
               << "x" << selected_stream_.value()->height;
    auto frame = CreateEmptyCameraFrame(selected_stream_.value());
    if (frame.is_null()) {
      result_ = camera_diag::mojom::FrameAnalysisResult::NewError(
          camera_diag::mojom::ErrorCode::kDiagnosticsInternal);
      notify_finish_->Set();
      return;
    }
    camera_service_controller_.RequestFrame(std::move(frame));
    return;
  }
  // Failed to start streaming. Set an error result and finish the session.
  if (result->get_error() ==
      camera_diag::mojom::ErrorCode::kCrosCameraControllerNotRegistered) {
    auto diag_result = camera_diag::mojom::DiagnosticsResult::New();
    diag_result->suggested_issue =
        camera_diag::mojom::CameraIssue::kCameraServiceDown;
    result_ =
        camera_diag::mojom::FrameAnalysisResult::NewRes(std::move(diag_result));
  } else {
    result_ =
        camera_diag::mojom::FrameAnalysisResult::NewError(result->get_error());
  }
  notify_finish_->Set();
}

camera_diag::mojom::FrameAnalysisResultPtr
CameraDiagnosticsSession::StopAndGetResult() {
  PrepareResult();
  base::AutoLock lock(lock_);
  return result_.value()->Clone();
}

void CameraDiagnosticsSession::PrepareResult() {
  base::AutoLock lock(lock_);
  if (result_.has_value()) {
    return;
  }
  auto diag_result = camera_diag::mojom::DiagnosticsResult::New();
  diag_result->suggested_issue = camera_diag::mojom::CameraIssue::kNone;
  result_ =
      camera_diag::mojom::FrameAnalysisResult::NewRes(std::move(diag_result));
}

}  // namespace cros
