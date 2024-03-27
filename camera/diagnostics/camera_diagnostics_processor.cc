// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/camera_diagnostics_processor.h"

#include <utility>

#include <base/functional/bind.h>
#include <base/location.h>
#include <base/sequence_checker.h>
#include <base/synchronization/lock.h>
#include <base/task/sequenced_task_runner.h>
#include <base/threading/thread_checker.h>
#include <chromeos/mojo/service_constants.h>

#include "camera/mojo/camera_diagnostics.mojom.h"
#include "cros-camera/common.h"
#include "cros-camera/future.h"
#include "diagnostics/camera_diagnostics_session.h"

namespace cros {

namespace {
// Buffer time to prepare result after finishing analysis. We should not exceed
// the duration configured by the client.
constexpr uint32_t kSessionTimeoutOffsetMs = 200;

inline bool IsValidDuration(
    camera_diag::mojom::FrameAnalysisConfigPtr& config) {
  return (config->duration_ms >=
              camera_diag::mojom::FrameAnalysisConfig::kMinDurationMs &&
          config->duration_ms <=
              camera_diag::mojom::FrameAnalysisConfig::kMaxDurationMs);
}

}  // namespace

CameraDiagnosticsProcessor::CameraDiagnosticsProcessor()
    : thread_("CameraDiagnostics") {
  CHECK(thread_.Start());
}

void CameraDiagnosticsProcessor::RunFrameAnalysis(
    camera_diag::mojom::FrameAnalysisConfigPtr config,
    RunFrameAnalysisCallback callback) {
  LOGF(INFO) << "RunFrameAnalysis called";
  base::AutoLock lock(session_lock_);
  if (current_session_) {
    ReturnErrorResult(std::move(callback),
                      camera_diag::mojom::ErrorCode::kAlreadyRunningAnalysis);
    return;
  }
  if (!IsValidDuration(config)) {
    // TODO(imranziad): Add StructTraits<T> validation instead.
    ReturnErrorResult(std::move(callback),
                      camera_diag::mojom::ErrorCode::kInvalidDuration);
    return;
  }
  thread_.PostTaskAsync(
      FROM_HERE,
      base::BindOnce(&CameraDiagnosticsProcessor::RunFrameAnalysisOnThread,
                     base::Unretained(this), std::move(config),
                     std::move(callback)));
}

void CameraDiagnosticsProcessor::QueueFrame(
    camera_diag::mojom::CameraFramePtr frame) {
  base::AutoLock lock(session_lock_);
  if (!current_session_) {
    VLOGF(1) << "No active session, dropping frame";
    return;
  }
  current_session_->QueueFrame(std::move(frame));
}

void CameraDiagnosticsProcessor::RunFrameAnalysisOnThread(
    camera_diag::mojom::FrameAnalysisConfigPtr config,
    RunFrameAnalysisCallback callback) {
  DCHECK(thread_.IsCurrentThread());

  auto future = Future<void>::Create(nullptr);
  base::ScopedClosureRunner session_resetter(base::BindOnce(
      &CameraDiagnosticsProcessor::ResetSession, base::Unretained(this)));

  // Don't hold the lock for too long.
  {
    base::AutoLock lock(session_lock_);
    current_session_ = std::make_unique<CameraDiagnosticsSession>(future);
    current_session_->RunFrameAnalysis(config->Clone());
  }

  // Failing this means our validation didn't work.
  CHECK_GE(config->duration_ms, kSessionTimeoutOffsetMs);
  future->Wait(config->duration_ms - kSessionTimeoutOffsetMs);

  base::AutoLock lock(session_lock_);
  std::move(callback).Run(current_session_->StopAndGetResult());
}

void CameraDiagnosticsProcessor::ReturnErrorResult(
    RunFrameAnalysisCallback callback, camera_diag::mojom::ErrorCode error) {
  LOGF(ERROR) << "Failed to run new frame analysis! Error " << error;
  auto result = camera_diag::mojom::FrameAnalysisResult::NewError(error);
  std::move(callback).Run(std::move(result));
}

void CameraDiagnosticsProcessor::ResetSession() {
  base::AutoLock lock(session_lock_);
  current_session_.reset();
}

}  // namespace cros