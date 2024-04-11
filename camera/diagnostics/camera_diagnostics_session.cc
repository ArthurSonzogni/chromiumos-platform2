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
    auto stream_config = std::move(result->get_stream());
    LOGF(INFO) << "Selected stream config: " << stream_config->width << "x"
               << stream_config->height;
    // TODO(imranziad): Allocate buffers.
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
