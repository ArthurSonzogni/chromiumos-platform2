// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/camera_diagnostics_session.h"

#include <cmath>
#include <cstdint>
#include <numeric>
#include <utility>

#include <base/check.h>
#include <base/functional/bind.h>
#include <base/location.h>
#include <base/sequence_checker.h>
#include <base/task/bind_post_task.h>
#include <base/task/sequenced_task_runner.h>
#include <base/threading/thread_checker.h>
#include <chromeos/mojo/service_constants.h>

#include "camera/mojo/camera_diagnostics.mojom.h"
#include "cros-camera/common.h"
#include "cros-camera/common_types.h"
#if USE_DLC
#include "diagnostics/analyzers/dirty_lens_analyzer.h"
#endif  // USE_DLC
#include "diagnostics/analyzers/privacy_shutter_analyzer.h"
#include "diagnostics/camera_diagnostics_mojo_manager.h"

namespace cros {

namespace {
// We set this low because FPS can drop due to long exposure in dark
// environment.
constexpr int kStreamingFrameIntervalDefault = 10;  // every 10th frame
// |DirtyLensAnalyzer| requires frames to have at least 640*480 pixels. So, we
// set this as a diagnostics service requirement.
constexpr int kMinPixelCount = 640 * 480;
// We don't want to process too large frames. This is sufficient for all the
// analyzers.
constexpr int kMaxPixelCount = 1920 * 1080;

// Calculates the smallest frame dimension with the same aspect ratio as that of
// |size| having pixel count >= |kMinPixelCount|. Input dimension must be even.
Size GetSmallestDimension(const Size& size) {
  CHECK(size.is_valid());
  // We can reduce/increase the dimension by GCD(width,height) times without
  // having to handle fractions.
  int gcd = std::gcd(size.width, size.height);
  CHECK_EQ(gcd % 2, 0);  // Sanity check that input is even.
  uint32_t dw = size.width / gcd;
  uint32_t dh = size.height / gcd;
  // We can safely increase it without having to worry about |std::ceil()|
  // conversion errors.
  uint32_t times =
      1 + std::sqrt(kMinPixelCount / static_cast<float>((dw * dh)));
  times += (times % 2);  // Making it even will ensure that the result is even.
  return {dw * times, dh * times};
}

// Creates an empty camera frame of |camera_diag::mojom::PixelFormat::kYuv420|.
camera_diag::mojom::CameraFramePtr CreateEmptyCameraFrame(
    const camera_diag::mojom::CameraStreamPtr& stream) {
  Size stream_size = {stream->width, stream->height};
  if (stream->pixel_format != camera_diag::mojom::PixelFormat::kYuv420 ||
      !stream_size.is_valid() || stream_size.width % 2 ||
      stream_size.height % 2) {
    LOGF(ERROR) << "Can not create camera frame with invalid stream size: "
                << stream_size.ToString();
    return nullptr;
  }
  Size frame_size = GetSmallestDimension(stream_size);
  if (frame_size.area() < kMinPixelCount ||
      frame_size.area() > kMaxPixelCount) {
    LOGF(ERROR) << "Out of bounds frame size. Original "
                << stream_size.ToString() << ", target "
                << frame_size.ToString();
    return nullptr;
  }
  LOGF(INFO) << "Target frame size: " << frame_size.ToString()
             << ", area: " << frame_size.area();
  if (frame_size.area() > stream_size.area()) {
    // TODO(imranziad): Disable analyzers that needs bigger frames for good
    // analysis.
    LOGF(WARNING) << "Frames will be upscaled, some analyzers might not run.";
  }
  auto frame = camera_diag::mojom::CameraFrame::New();
  frame->stream = stream.Clone();
  frame->stream->width = frame_size.width;
  frame->stream->height = frame_size.height;
  frame->source = camera_diag::mojom::DataSource::kCameraDiagnostics;
  frame->is_empty = true;
  frame->buffer = camera_diag::mojom::CameraFrameBuffer::New();
  // Only NV12 frames are supported now. Average of 12 bits per pixel.
  frame->buffer->size = (frame->stream->width * frame->stream->height * 3) / 2;
  frame->buffer->shm_handle =
      mojo::SharedBufferHandle::Create(frame->buffer->size);
  if (!frame->buffer->shm_handle->is_valid()) {
    LOGF(ERROR) << "Failed to create SharedBufferHandle for stream size: "
                << frame->stream->width << "x" << frame->stream->height;
    return nullptr;
  }
  return frame;
}

}  // namespace

CameraDiagnosticsSession::CameraDiagnosticsSession(
    CameraDiagnosticsMojoManager* mojo_manager,
    const base::FilePath& blur_detector_dlc_path,
    scoped_refptr<Future<void>> notify_finish)
    : thread_("CameraDiagSession"),
      camera_service_controller_(mojo_manager),
      notify_finish_(notify_finish) {
  CHECK(thread_.Start());

  frame_analyzers_.push_back(std::make_unique<PrivacyShutterAnalyzer>());
  LOGF(INFO) << "PrivacyShutterAnalyzer enabled";

#if USE_DLC
  auto dirty_lens_analyzer = std::make_unique<DirtyLensAnalyzer>();
  if (dirty_lens_analyzer->Initialize(blur_detector_dlc_path)) {
    frame_analyzers_.push_back(std::move(dirty_lens_analyzer));
    LOGF(INFO) << "DirtyLensAnalyzer enabled";
  } else {
    LOGF(INFO) << "DirtyLensAnalyzer disabled";
  }
#endif  // USE_DLC
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
  VLOGF(2) << "Frame received, frame_number "
           << frame->frame_number.value_or(-1);
  thread_.PostTaskAsync(
      FROM_HERE, base::BindOnce(&CameraDiagnosticsSession::QueueFrameOnThread,
                                base::Unretained(this), std::move(frame)));
}

void CameraDiagnosticsSession::QueueFrameOnThread(
    camera_diag::mojom::CameraFramePtr frame) {
  DCHECK(thread_.IsCurrentThread());
  if (frame->is_empty) {
    // Frame is not filled up properly, resend to camera service.
    camera_service_controller_.RequestFrame(std::move(frame));
    return;
  }
  for (auto& analyzer : frame_analyzers_) {
    analyzer->AnalyzeFrame(frame);
  }
  // Resend the frame to camera service to fill up again.
  frame->is_empty = true;
  camera_service_controller_.RequestFrame(std::move(frame));
}

void CameraDiagnosticsSession::RunFrameAnalysisOnThread(
    camera_diag::mojom::FrameAnalysisConfigPtr config) {
  DCHECK(thread_.IsCurrentThread());
  LOGF(INFO) << "FrameAnalysis started in session";
  auto start_stream_config = camera_diag::mojom::StreamingConfig::New();
  // TODO(imranziad): Adjust the interval based on |config->duration_ms|.
  start_stream_config->frame_interval = kStreamingFrameIntervalDefault;
  // This callback needs to run on session thread.
  auto callback = base::BindPostTask(
      thread_.task_runner(),
      base::BindOnce(&CameraDiagnosticsSession::OnStartStreaming,
                     base::Unretained(this)));
  camera_service_controller_.StartStreaming(std::move(start_stream_config),
                                            std::move(callback));
}

// Run on session thread so that we don't block the IPC thread during frame
// allocation.
void CameraDiagnosticsSession::OnStartStreaming(
    camera_diag::mojom::StartStreamingResultPtr result) {
  DCHECK(thread_.IsCurrentThread());
  base::AutoLock lock(lock_);
  // Successfully started streaming.
  if (result->is_stream()) {
    // Send an empty frame with shared buffer to camera service to fill up.
    auto selected_stream = std::move(result->get_stream());
    LOGF(INFO) << "Camera service selected stream " << selected_stream->width
               << "x" << selected_stream->height
               << ", format: " << selected_stream->pixel_format;
    auto frame = CreateEmptyCameraFrame(selected_stream);
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
  camera_service_controller_.StopStreaming();
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

  for (auto& analyzer : frame_analyzers_) {
    camera_diag::mojom::AnalyzerResultPtr analyzer_result =
        analyzer->GetResult();

    VLOGF(1) << "Analyzer " << analyzer_result->type
             << ", status: " << analyzer_result->status;

    // Prioritized first analyzer's failure as the suggested issue.
    if (diag_result->suggested_issue ==
            camera_diag::mojom::CameraIssue::kNone &&
        analyzer_result->status ==
            camera_diag::mojom::AnalyzerStatus::kFailed) {
      switch (analyzer_result->type) {
        case cros::camera_diag::mojom::AnalyzerType::kPrivacyShutterSwTest:
          diag_result->suggested_issue =
              camera_diag::mojom::CameraIssue::kPrivacyShutterOn;
          break;
        case cros::camera_diag::mojom::AnalyzerType::kDirtyLens:
          diag_result->suggested_issue =
              camera_diag::mojom::CameraIssue::kDirtyLens;
          break;
        default:
          break;
      }
    }

    diag_result->analyzer_results.push_back(std::move(analyzer_result));
  }

  result_ =
      camera_diag::mojom::FrameAnalysisResult::NewRes(std::move(diag_result));
}

}  // namespace cros
