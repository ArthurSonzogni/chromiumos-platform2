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

namespace cros {

CameraDiagnosticsSession::CameraDiagnosticsSession(
    scoped_refptr<Future<void>> notify_finish)
    : thread_("CameraDiagSession"), notify_finish_(notify_finish) {
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
  // TODO(imranziad): Start streaming in camera service.
  // Placeholder for testing.
  result_ = camera_diag::mojom::FrameAnalysisResult::NewError(
      camera_diag::mojom::ErrorCode::kCameraClosed);
}

camera_diag::mojom::FrameAnalysisResultPtr
CameraDiagnosticsSession::StopAndGetResult() {
  PrepareResult();
  return result_.value()->Clone();
}

void CameraDiagnosticsSession::PrepareResult() {
  if (result_.has_value()) {
    return;
  }
  auto diag_result = camera_diag::mojom::DiagnosticsResult::New();
  diag_result->suggested_issue = camera_diag::mojom::CameraIssue::kNone;
  result_ =
      camera_diag::mojom::FrameAnalysisResult::NewRes(std::move(diag_result));
}

}  // namespace cros
