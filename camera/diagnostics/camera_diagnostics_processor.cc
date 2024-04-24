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
#include <ml_core/dlc/dlc_ids.h>

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

CameraDiagnosticsProcessor::CameraDiagnosticsProcessor(
    CameraDiagnosticsMojoManager* mojo_manager)
    : thread_("CameraDiagnostics"), mojo_manager_(mojo_manager) {
  CHECK(thread_.Start());
#if USE_DLC
  // Install blur detector library at startup, since it might take some time to
  // download the DLC.
  mojo_manager_->GetTaskRunner()->PostTask(
      FROM_HERE,
      base::BindOnce(
          &CameraDiagnosticsProcessor::InstallBlurDetectorDlcOnIpcThread,
          base::Unretained(this)));
#endif  // USE_DLC
}

CameraDiagnosticsProcessor::~CameraDiagnosticsProcessor() {
  // This makes sure analysis is stopped before destroying any objects.
  thread_.Stop();
}

void CameraDiagnosticsProcessor::InstallBlurDetectorDlcOnIpcThread() {
  DCHECK(mojo_manager_->GetTaskRunner()->RunsTasksInCurrentSequence());
  // base::Unretained(this) is safe because |this| outlives |dlc_client_|.
  blur_detector_dlc_client_ = DlcClient::Create(
      cros::dlc_client::kBlurDetectorDlcId,
      base::BindOnce(&CameraDiagnosticsProcessor::OnBlurDetectorDlcSuccess,
                     base::Unretained(this)),
      base::BindOnce(&CameraDiagnosticsProcessor::OnBlurDetectorDlcFailure,
                     base::Unretained(this)));
  if (!blur_detector_dlc_client_) {
    OnBlurDetectorDlcFailure("error creating DlcClient");
    return;
  }
  blur_detector_dlc_client_->InstallDlc();
}

void CameraDiagnosticsProcessor::OnBlurDetectorDlcSuccess(
    const base::FilePath& dlc_path) {
  blur_detector_dlc_root_path_ = dlc_path;
}

void CameraDiagnosticsProcessor::OnBlurDetectorDlcFailure(
    const std::string& error_msg) {
  LOGF(ERROR) << "BlurDetector DLC failed to install. Error: " << error_msg;
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

  // Don't hold the lock for too long.
  {
    base::AutoLock lock(session_lock_);
    current_session_ = std::make_unique<CameraDiagnosticsSession>(
        mojo_manager_, blur_detector_dlc_root_path_, future);
    current_session_->RunFrameAnalysis(config->Clone());
  }

  // Failing this means our validation didn't work.
  CHECK_GE(config->duration_ms, kSessionTimeoutOffsetMs);
  future->Wait(config->duration_ms - kSessionTimeoutOffsetMs);

  base::AutoLock lock(session_lock_);
  camera_diag::mojom::FrameAnalysisResultPtr result =
      current_session_->StopAndGetResult();
  // Clean up before running the callback, since remotes need to unbind on IPC
  // thread. Once we return the callback, IPC thread might exit without waiting
  // for the reset.
  current_session_.reset();

  std::move(callback).Run(std::move(result));
}

void CameraDiagnosticsProcessor::ReturnErrorResult(
    RunFrameAnalysisCallback callback, camera_diag::mojom::ErrorCode error) {
  LOGF(ERROR) << "Failed to run new frame analysis! Error " << error;
  auto result = camera_diag::mojom::FrameAnalysisResult::NewError(error);
  std::move(callback).Run(std::move(result));
}

}  // namespace cros
